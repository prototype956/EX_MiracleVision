# BasicArmor 检测器：原始实现分析 & 重构优化说明

> 文档版本：2026-03-07  
> 对应提交：refactor/core-infra（≥600dacc）

---

## 1. 原始项目整体主循环（`base/MiracleVision.cpp`）

```
while (true):
  1. 捕获帧（MindVision SDK 或 cv::VideoCapture 后备）
  2. serial_.updateReceiveInformation()         // 从串口读取模式 + 我方颜色
  3. switch (uart::AUTO_AIM):
       AUTO_AIM  → basic_armor_.runBasicArmor(src_img, receive_data)
                     └─ runImage()
                          └─ findLight()
                               └─ fittingArmor()
                                    └─ finalArmor()
                   solution.angleSolve(armor_rect, …)
                   serial_.updataWriteData(num, fire, yaw, pitch, center, 0)
       ENERGY_BUFF → basic_buff_.runTask()
                     buff_solution.angleSolve()
  4. basic_armor_.freeMemory()   // 清空 light_ / armor_ 向量
  5. 看门狗：若 FPS > 500 则重启相机
```

**注意**：模式和颜色由串口实时推送，与算法完全解耦——`runBasicArmor` 只拿颜色标志，不关心模式切换逻辑。

---

## 2. 原始三路融合预处理（`runImage` → `fuseImage`）

原始项目在图像预处理阶段采用**三路独立生成掩码再 AND 融合**，这是抑制"灯条白色中心"误判的核心设计。

```
┌───────────────────────────────────────────────────────────┐
│                     输入帧 (BGR)                           │
└──────────────────┬────────────────┬───────────────────────┘
                   │                │                │
           路径 1:灰度           路径 2:BGR         路径 3:白色
           grayPretreat        bgrPretreat        whitePretreat
                   │                │                │
         灰度阈值二值图      双通道差值AND图      极亮掩码(取反)
                   │                │                │
                   └────── AND ─────┘                │
                           │                         │
                           └─────────── AND ─────────┘
                                       │
                               三路融合二值图
                                       │
                           dilate + medianBlur
```

### 2.1 路径 1：`grayPretreat` — 灰度亮度门控

```cpp
cv::cvtColor(src, gray, BGR2GRAY);
cv::threshold(gray, bin_gray, gray_th, 255, THRESH_BINARY);
```

- **识别蓝灯**（我方红）：`gray_th = blue_armor_gray_th`（默认约 190）
- **识别红灯**（我方蓝）：`gray_th = red_armor_gray_th`（默认约 160）

**作用**：灯条必须足够亮才能进入后续流程，先pass一个亮度门限。

### 2.2 路径 2：`bgrPretreat` — **双通道差值 AND（白色抑制关键）**

#### 识别蓝灯时（我方颜色 = RED）：

```cpp
cv::subtract(channels[0], channels[2], color_mask);  // B - R → 蓝色主导
cv::subtract(channels[0], channels[1], green_mask);  // B - G → 蓝色强于绿色
cv::threshold(color_mask, color_mask, blue_color_th,  255, THRESH_BINARY);
cv::threshold(green_mask, green_mask, green_color_th, 255, THRESH_BINARY);
cv::bitwise_and(green_mask, color_mask, color_mask);  // 两者取 AND
```

#### 识别红灯时（我方颜色 = BLUE）：

```cpp
cv::subtract(channels[2], channels[0], color_mask);  // R - B
cv::subtract(channels[2], channels[1], green_mask);  // R - G
// 同样 AND 操作
```

#### 白色抑制原理（为什么双通道 AND 有效）

| 像素类型 | R    | G    | B    | R-B  | R-G  | AND结果 |
|---------|------|------|------|------|------|---------|
| 纯红灯  | 255  | 0    | 0    | 255  | 255  | **通过** |
| 橙红灯  | 255  | 100  | 0    | 255  | 155  | **通过** |
| 白色    | 255  | 255  | 255  | ≈0   | ≈0   | **被抑制** |
| 白色偏红| 255  | 240  | 220  | 35   | 15   | **被抑制**（低于阈值）|
| 纯蓝灯  | 0    | 0    | 255  | −(0) | −(0) | 用B-R/B-G同理 |

**结论**：单通道差值（R-B）对白色（R≈B≈255 → 差≈0）完全失效；双通道差值 AND 要求像素在**两个维度**都纯，天然抑制白色过曝区。

### 2.3 路径 3：`whitePretreat` — 极亮白色硬屏蔽

```cpp
cv::cvtColor(src, gray, BGR2GRAY);
cv::threshold(gray, white_mask, white_th, 255, THRESH_BINARY);  // white_th ≈ 200~220
cv::bitwise_not(white_mask, white_mask);   // 取反：灰度 > white_th 的像素被清零
```

**作用**：对于极亮白色区域（灰度 > white_th）直接硬屏蔽，防止双通道 AND 因数值波动漏检。

### 2.4 `fuseImage` 融合 + 形态学

```cpp
cv::bitwise_and(bin_gray,  bin_color, bin_result);   // 灰度掩码 & 颜色掩码
cv::bitwise_and(bin_result, white_mask, bin_result); // & 白色反掩码
cv::dilate(bin_result, bin_result, kernel);
cv::medianBlur(bin_result, bin_result, 3);
```

---

## 3. 原始 `findLight` 灯条提取

```
findContours(bin_result, RETR_EXTERNAL)
for each contour:
    if perimeter ∉ [perimeter_min, perimeter_max]: continue
    box = fitEllipse(contour)           ← 注意：原项目用椭圆拟合，而非 minAreaRect
    colorCheck(box, src, color)         ← ROI BGR 均值验证（见 §3.1）
    if angle ∉ [angle_min, angle_max]: continue
    if ratio_w_h ∉ [ratio_min, ratio_max]: continue
    if area ≥ 30000: continue           ← 排除超大干扰物
    light_.push_back(box)
```

### 3.1 `colorCheck` — ROI 颜色均值验证（原始独有，重构版缺失）

```cpp
// 取轮廓 boundingRect 裁出 ROI
// 分离 B/G/R 通道，计算各通道均值
// 判断均值是否在预设范围内：
//   [b_min, b_max] + [g_min, g_max] + [r_min, r_max]
// 三个通道均满足 → 颜色合法
```

**作用**：在几何过滤之后再做一次颜色精确验证，防止颜色相近的非灯条区域（如环境光斑）通过。

---

## 4. 原始 `fittingArmor` 装甲板匹配

```
for each (light_i, light_j) pair:
    lightJudge(i, j):
        高宽比差 ≤ ratio_diff_threshold
        Y 轴中心差 ≤ y_diff_threshold
        高度差 ≤ height_diff_threshold
        角度差 ≤ angle_diff_threshold
    averageColor(bin_gray, armor_ROI) < 30       ← 装甲内部应为暗区（关键！）
    宽高比 ∈ [small_armor_aspect_min, big_armor_aspect_max]
    → armor_.push_back(armor)
```

**`averageColor` < 30 的意义**：在灰度二值图上，装甲板内部（两灯条之间）应该是背景暗区，若该区域平均灰度 ≥ 30，说明有亮斑干扰，该配对被丢弃——这是防误识别的强力过滤器。

---

## 5. 原始 `finalArmor` 目标选择

```cpp
// 按距图像中心的欧氏距离升序排序
std::sort(armor_.begin(), armor_.end(),
          [](const auto& a, const auto& b) { return a.dist < b.dist; });
target = armor_[0];  // 取最近目标
```

---

## 6. 重构版 vs 原始版对比

| 特性 | 原始版 | 重构版（优化前） | 重构版（优化后）|
|------|--------|----------------|----------------|
| 通道差值 | **双通道 AND**（R-B AND R-G）| 单通道（R-B）| **双通道 AND**（修复）|
| 白色硬掩码 | ✅ `whitePretreat` | ❌ 缺失 | ✅ 增加（修复）|
| 灰度亮度门控 | ✅ 独立 `grayPretreat` | ❌ 合并为单阈值 | 合并保留（单阈值足够）|
| 形态学 | dilate + medianBlur | dilate only | dilate + **morphClose**（填孔）|
| 轮廓拟合 | `fitEllipse` | `minAreaRect` | `minAreaRect`（保留，精度够）|
| ROI 颜色验证 | ✅ `colorCheck` | ❌ 缺失 | ❌ 暂不加（复杂度换简洁）|
| 装甲内部暗区验证 | ✅ `averageColor < 30` | ❌ 缺失 | ❌ 暂不加（留后续迭代）|
| 目标选择 | 距图像中心最近 | 全部输出（由外层选） | 全部输出（保持接口一致）|
| 可调参数 | YAML 静态配置 | YAML + Trackbar 热调 | YAML + Trackbar 热调 |

---

## 7. 重构优化：`BuildChannelDiff` 三路融合

### 7.1 新增 `Params` 字段

```cpp
struct Params {
  int   light_thresh{160};   // 单通道差值（主颜色）二值阈值
  int   green_thresh{50};    // 第二通道差值（vs绿色）二值阈值 ← 新增
  int   white_thresh{200};   // 灰度超过此值视为纯白，直接屏蔽 ← 新增
  // ... 其余几何参数不变
};
```

### 7.2 新预处理流程

```
输入 BGR 帧
    │
    ├── split → B/G/R 通道
    │
    ├── 主差值：R-B（红）或 B-R（蓝）→ threshold(light_thresh) → mask_main
    │
    ├── 辅差值：R-G（红）或 B-G（蓝）→ threshold(green_thresh)  → mask_green
    │
    ├── mask_color = mask_main AND mask_green                  ← 双通道AND
    │
    ├── gray → threshold(white_thresh) → BINARY → bitwise_NOT → mask_white
    │
    └── result = mask_color AND mask_white                     ← 白色硬屏蔽
```

### 7.3 阈值调参指南

| 参数 | 默认值 | 含义 | 调大效果 | 调小效果 |
|------|--------|------|---------|---------|
| `light_thresh` | 160 | 主通道差值门限 | 只保留颜色很纯的灯条 | 保留更多候选（增噪声）|
| `green_thresh` | 50 | 辅通道差值门限 | 更严格的白色抑制 | 允许略偏白的灯条通过 |
| `white_thresh` | 200 | 灰度白色门限 | 只屏蔽最极亮的白色 | 屏蔽更多亮区（可能误杀灯条）|

**经验**：`green_thresh` 在 30~80 之间调节；`white_thresh` 建议 180~220，不要低于 160 以免误杀真灯条。

---

## 8. 相关文件索引

| 文件 | 说明 |
|------|------|
| `base/MiracleVision.cpp` | 原始主循环 |
| `module/armor/basic_armor.cpp` | 原始检测实现（共 949 行）|
| `module/armor/basic_armor.hpp` | 原始配置结构体（Armor_Config 等）|
| `src/modules/armor_detector/basic_armor_detector.hpp` | 重构接口 + Params + DebugData |
| `src/modules/armor_detector/basic_armor_detector.cpp` | 重构实现（优化后）|
| `src/test/video_pipeline_test.cpp` | 离线调试主程序（含 Trackbar）|
| `configs/vision.yaml` | 主配置（`detector:` 节点）|
