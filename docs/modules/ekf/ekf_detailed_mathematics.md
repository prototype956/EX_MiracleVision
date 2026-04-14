# EKF 数学与工程直觉详解

本文回答"EKF 在这个项目里到底算了什么"这个问题，通过公式与代码映射帮助理解每个步骤的工程目的。

## 1. 问题定义：为什么需要滤波器

装甲板检测结果有抖动和丢帧，直接用检测点作为瞄准点会导致枪口跳跃、命中率下降。滤波器的目的是在有噪声和不完整观测的条件下，给出连续、可预测的状态估计。

EKF（扩展卡尔曼滤波）相比简单滤波的优势是，它同时估计位置和速度，所以能在丢帧时仍然给出合理的外推。

## 2. 状态向量：一次性估计什么

EKF 在这个项目里是 11 维的，同时估计目标的运动和几何形状：

$$
\mathbf{x} = [c_x, \dot{c}_x, c_y, \dot{c}_y, c_z, \dot{c}_z, \alpha, \dot{\alpha}, r, \Delta l, \Delta h]^T
$$

**物理含义**（坐标系：世界系, 单位：m 和 rad）

| 维 | 符号 | 含义 | 单位 | 说明 |
| --- | --- | --- | --- | --- |
| 0 | $c_x$ | 旋转中心 X 坐标 | m | 目标几何中心的世界系 X |
| 1 | $\dot{c}_x$ | 中心 X 速度 | m/s | 可观测（从多帧位置差）或不可观测（从单帧） |
| 2~5 | $c_y, \dot{c}_y, c_z, \dot{c}_z$ | Y/Z 坐标与速度 | m, m/s | 同上 |
| 6 | $\alpha$ | 装甲板朝向角（yaw） | rad | 从装甲板在图像中的位置反算 |
| 7 | $\dot{\alpha}$ | 角速度 | rad/s | 小陀螺、吊打、平面转时值较大 |
| 8 | $r$ | 装甲板到中心的距离（主轴半径） | m | 固定或缓变，初始值约 0.27m |
| 9 | $\Delta l$ | 长短轴半径差 | m | 四板车的大小板差异，前哨站固定为 0 |
| 10 | $\Delta h$ | 大小板高度差 | m | 同上 |

**代码位置**：[ekf_track_target.cpp L50-80](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

## 3. 预测步骤：从上一时刻推到现在

### 3.1 状态转移模型

预测是"如果没有新观测，状态应该怎么演进"。这个项目用的是**常速模型**（CV模型），就是假设在短时间 $\Delta t$ 内位置线性变化、角度线性转动：

$$
\mathbf{x}^{-} = f(\mathbf{x}_{k-1})
$$

其中 $f$ 是非线性状态转移函数。写成线性近似（用雅可比 $F$）：

$$
\mathbf{x}^{-} \approx F \mathbf{x}_{k-1}
$$

**$F$ 矩阵的结构**（11×11，分块）：

$$
F = \begin{bmatrix}
1 & \Delta t & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0\\
0 & 1 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0\\
0 & 0 & 1 & \Delta t & 0 & 0 & 0 & 0 & 0 & 0 & 0\\
0 & 0 & 0 & 1 & 0 & 0 & 0 & 0 & 0 & 0 & 0\\
0 & 0 & 0 & 0 & 1 & \Delta t & 0 & 0 & 0 & 0 & 0\\
0 & 0 & 0 & 0 & 0 & 1 & 0 & 0 & 0 & 0 & 0\\
0 & 0 & 0 & 0 & 0 & 0 & 1 & \Delta t & 0 & 0 & 0\\
0 & 0 & 0 & 0 & 0 & 0 & 0 & 1 & 0 & 0 & 0\\
0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 1 & 0 & 0\\
0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 1 & 0\\
0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 0 & 1
\end{bmatrix}
$$

**直觉**：前三个分块（位置与速度）遵循标准匀速模型；角度与角速度独立处理；几何参数（半径、高度差）认为不变。

**代码位置**：[ekf_track_target.cpp 第100-140 行](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

### 3.2 过程噪声：对模型的不信任

现实中目标不可能完全遵循匀速模型（会加速、减速、小陀螺转速变化等），所以滤波器需要一个噪声项 $Q$ 来表达"模型能有多错"：

$$
P^{-} = F P_{k-1} F^T + Q
$$

这里的 $P$ 是状态估计的协方差（不确定度），$Q$ 越大表示"我对匀速模型越不放心"。

**$Q$ 的典型采用**（分段白噪声模型 PWNM）：

目标转移（线性加速度方差为 $v$）时，过程噪声矩阵的一个 2×2 分块是：

$$
Q_{\text{block}} = \begin{bmatrix}
\frac{\Delta t^4}{4} v & \frac{\Delta t^3}{2} v\\
\frac{\Delta t^3}{2} v & \Delta t^2 v
\end{bmatrix}
$$

所以完整的 $Q$ 就是把这种分块对角排列：位置有一个 $v_1$，角度有另一个 $v_2$：

$$
Q = \text{blockdiag}(Q_x, Q_y, Q_z, Q_\alpha, 0, 0, 0)
$$

**参数含义**：
- $v_1 = 100$ m²/s⁴（普通车位置加速度方差）
- $v_2 = 400$ rad²/s⁴（普通车角加速度方差）
- 前哨站：$v_1 = 10$, $v_2 = 0.1$（转速近似匀速）

**代码位置**：[ekf_track_target.cpp L115-140](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

### 3.3 预测公式总结

$$
\hat{\mathbf{x}}^{-}_k = f(\hat{\mathbf{x}}_{k-1})
$$
$$
P^{-}_k = F P_{k-1} F^T + Q
$$

## 4. 更新步骤：用观测纠正预测

预测得到了先验估计 $(\hat{\mathbf{x}}^{-}, P^{-})$，现在有一个新观测 $\mathbf{z}_k$（相机检测到的装甲板）。更新步骤计算一个增益 $K$，然后用观测与预测的偏差来修正：

### 4.1 观测函数

观测不是直接测量状态向量，而是通过非线性映射 $h$ 得到的：

$$
\mathbf{z}_k = h(\mathbf{x}_k) + \mathbf{v}_k
$$

其中 $\mathbf{v}_k$ 是观测噪声（假设高斯分布）。

**观测向量是 4 维**（来自一个检测到的装甲板）：

$$
\mathbf{z} = [y_{\text{camera}}, p_{\text{camera}}, d_{\text{camera}}, \alpha_{\text{armor}}]^T
$$

| 分量 | 含义 | 单位 | 获取方式 |
| --- | --- | --- | --- |
| $y_{\text{camera}}$ | 装甲板在相机中的 yaw 角 | rad | atan2(装甲板中心 xy) |
| $p_{\text{camera}}$ | 装甲板在相机中的 pitch 角 | rad | atan2(装甲板中心 z / 水平距离) |
| $d_{\text{camera}}$ | 装甲板中心到相机的距离 | m | sqrt(x²+y²+z²) |
| $\alpha_{\text{armor}}$ | 装甲板本身的朝向角度 | rad | PnP 解算出的板姿态 |

**对应到状态的映射函数** $h(\mathbf{x})$：

给定状态 $\mathbf{x}$，可以计算出每个装甲板的世界坐标，再反投影回相机：

$$
\mathbf{p}_{\text{armor}, i} = \begin{bmatrix} c_x - r \cos(\alpha + \frac{2\pi i}{N}) \\ c_y - r \sin(\alpha + \frac{2\pi i}{N}) \\ c_z + h_z(i) \end{bmatrix}
$$

其中 $i$ 是装甲板编号，$h_z(i)$ 是高度修正（四板车有多个装甲板可看，算法选最合适的）。

**代码位置**：[ekf_track_target.cpp L150-230](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

### 4.2 观测噪声矩阵

$$
R = \text{diag}(r_1, r_2, r_3, r_4)
$$

这个项目中 $R$ 是自适应的，取决于观测的置信度：

$$
R_y = 4 \times 10^{-3} \text{ rad}^2
$$
$$
R_p = 4 \times 10^{-3} \text{ rad}^2
$$
$$
R_d = \log(|\delta \alpha| + 1) + 1 \text{ m}^2 \quad (\text{距离误差随角度偏离增大})
$$
$$
R_\alpha = \frac{\log(|d_{\text{dist}}| + 1)}{200} + 9 \times 10^{-2} \text{ rad}^2
$$

**直觉**：如果装甲板离预测位置很远（大 $\delta \alpha$），我们对这个观测的信任度更低，所以 $R$ 定值更大。

### 4.3 卡尔曼增益与状态更新

卡尔曼增益是预测协方差与观测噪声的加权平衡：

$$
K = P^{-} H^T (H P^{-} H^T + R)^{-1}
$$

然后状态更新为：

$$
\hat{\mathbf{x}}_k = \hat{\mathbf{x}}^{-}_k + K (\mathbf{z}_k - h(\hat{\mathbf{x}}^{-}_k))
$$

协方差更新（Joseph 形式，数值稳定）：

$$
P_k = (I - K H) P^{-}_k
$$

**直觉**：
- 如果 $P^{-}$ 很大（预测很不确定），$K$ 就接近 $H^T(H P^{-} H^T)^{-1}$（全部信任观测）
- 如果 $R$ 很大（观测很不确定），$K$ 就接近 0（几乎不改观测）
- 如果 $P^{-}$ 很小（我对预测很确定），$K$ 也很小（观测改变不了多少）

**代码位置**：[ekf_track_target.cpp L185-210](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

## 5. 雅可比矩阵：非线性的本地线性化

观测函数 $h$ 本身是非线性的（包含 atan2、sqrt、cos/sin），所以卡尔曼增益计算需要雅可比矩阵 $H = \frac{\partial h}{\partial \mathbf{x}}$（4×11）。

**关键理解**：雅可比不是"用于预测未来"，而是"用于本次更新的线性化点"。它描述的是"在当前状态估计点，观测函数对状态的偏导"。

$$
H = \begin{bmatrix}
\frac{\partial y}{\partial c_x} & \frac{\partial y}{\partial \dot{c}_x} & \cdots \\
\frac{\partial p}{\partial c_x} & \frac{\partial p}{\partial \dot{c}_x} & \cdots \\
\frac{\partial d}{\partial c_x} & \frac{\partial d}{\partial \dot{c}_x} & \cdots \\
\frac{\partial \alpha_{\text{armor}}}{\partial c_x} & \frac{\partial \alpha_{\text{armor}}}{\partial \dot{c}_x} & \cdots
\end{bmatrix}
$$

**计算方式**（链式法则）：

$$
H = \frac{\partial h(\mathbf{x})}{\partial \mathbf{x}} = \frac{\partial h}{\partial \mathbf{p}_{\text{armor}}} \cdot \frac{\partial \mathbf{p}_{\text{armor}}}{\partial \mathbf{x}}
$$

前一项是"相机坐标到角度距离的偏导"（包含 atan2 与 norm 的偏导）；  
后一项是"装甲板位置对状态向量的偏导"（包含 r、c、α 的耦合）。

**代码位置**：[ekf_track_target.cpp L260-310](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

## 6. 角度归一化：跨越 ±π 边界的陷阱

角度变量（$\alpha$、$y$、$p$）的范围是 $[-\pi, \pi]$。如果不小心处理，$-\pi$ 和 $+\pi$ 两点在数值上看起来距离很远，会导致滤波器算出假的大误差。

**例子**：如果预测角度是 $+3.0$ rad（接近 $+\pi$），观测角度是 $-3.0$ rad（接近 $-\pi$），它们其实只差 $0.28$ rad，但直接相减得 $-6$ rad。

**解决方案**：对所有角度变量做模 $2\pi$ 归一化到 $[-\pi, \pi]$：

```cpp
// limit_rad 函数（来自工具库）
double limit_rad(double rad) {
  while (rad > M_PI) rad -= 2 * M_PI;
  while (rad < -M_PI) rad += 2 * M_PI;
  return rad;
}
```

**应用位置**：
1. 状态融合时：`x_add` 函数中对 $\alpha$ 做 `limit_rad`
2. 预测后：对新的 $\alpha$ 状态做 `limit_rad`
3. 观测与预测求差时：对 $y, p, \alpha_{\text{armor}}$ 的差分做 `limit_rad`

**代码位置**：[ekf_track_target.cpp L44, L114, L207](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp)

## 7. 发散与一致性检测

EKF 的陷阱是"看起来收敛了，其实已经错"（滤波发散）。这个项目用 **NIS（Normalized Innovation Squared）** 来检测：

$$
\text{NIS} = (\mathbf{z}_k - h(\hat{\mathbf{x}}^{-}_k))^T (H P^{-} H^T + R)^{-1} (\mathbf{z}_k - h(\hat{\mathbf{x}}^{-}_k))
$$

**直觉**：NIS 是一个标准卡方分布（如果匹配正确），应该大约 70% 的时间 < 7.8（4 自由度的 3σ 门限）。如果经常 > 7.8，说明"预测与观测长期不符"，可能是初始化错、坐标系统配置错或目标确实在做异常运动。

**应用**：维护一个滑动窗口来统计 NIS 失败的频率。若失败率 > 40%，说明发散，回到 LOST 状态重新初始化。

**代码位置**：[ekf_tracker.cpp L100-110](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp)

同时，还有协方差发散检测（半径估计超出物理范围）：

```cpp
bool diverged = !(radius_ok && delta_radius_ok);
```

## 8. 常见误解

**误解 1**："雅可比是下一时刻的雅可比"  
错。雅可bili 是在当前状态估计点的局部导数，用于本次的线性化。下一时刻会重新用新的状态计算新的雅可比。

**误解 2**："增大过程噪声一定更稳"  
错。过程噪声太大会让滤波器过度追随观测，反而抖动。正确的做法是在"响应速度"和"稳定性"之间平衡。

**误解 3**："角度值可以直接用浮点数比较"  
错。如例子所示，359° 和 -359° 在数值上差大，但角度上几乎相同。必须做归一化。

**误解 4**："NIS > 阈值就一定是分散"  
不准确。单次 NIS 大可能是瞬间观测噪声，但如果长期高（50+ 帧平均 > 7.8），才是真的发散。

## 9. 总线路图

```
Detection
    ↓ (装甲板在相机中的像素坐标与类别)
[PnP解算] → xyz_gimbal + 四元数
    ↓ (坐标转换)
[EkfPredictor 坐标变换] → xyz_world
    ↓ (追踪状态机)
[EkfTracker] LOST/DETECTING/TRACKING/TEMP_LOST
    ├─ SetTarget: 初始化 x̂, P
    └─ UpdateTarget: 
       ├─ 调用 Predict(dt)
       │  ├─ x̂^- = F x̂
       │  └─ P^- = FPF^T + Q
       ├─ 调用 Update(detection):
       │  ├─ 计算 h(x̂^-), H
       │  ├─ 计算 K = P^- H^T (HP^-H^T+R)^{-1}
       │  ├─ x̂ = x̂^- + K(z - h(x̂^-))
       │  ├─ P = (I-KH)P^-
       │  ├─ 检查 NIS 与发散
       │  └─ 角度归一化
       └─ 输出 TrackTarget (state snapshot)
    ↓
[TrajectorySolver] → 瞄准点 + 延迟补偿
    ↓
GimbalControl (yaw, pitch, tracking, fire=false)
    ↓
[Voter] 决定 fire = true/false
    ↓
SerialNode 发包
```

## 10. 进阶阅读

想深入理解卡尔曼滤波的数学基础，推荐参考：
- 官方教科书：Kalman et al. (1960)，或现代教材如 Bar-Shalom 系列
- 工程视角：Welch & Bishop 的卡尔曼滤波教程（在线免费）

本项目的具体实现细节请参考[参数调参指南](ekf_tuning_guide.md)和[坐标转换调参](coordinate_transformation_tuning.md)。
