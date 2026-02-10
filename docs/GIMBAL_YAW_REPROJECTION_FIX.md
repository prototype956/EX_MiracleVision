# Gimbal Yaw 修改不影响重投影结果的问题分析

## 📅 分析日期
2026年1月31日

## 🔴 问题描述

**现象**: 在 Foxglove Studio 中修改 `gimbal_yaw` 参数后，画面中的重投影结果(绿色装甲板框)没有变化。

## 🔍 根本原因分析

### 代码执行流程

在 `test/minimum_vision.cpp` 的主循环中:

```cpp
while (!video_finished) {
    cap_ >> src_img;
    
    // ============ 步骤 1: 获取当前云台姿态 ============
    uart::Receive_Data current_mock_data;
    {
        std::lock_guard<std::mutex> lock(mock_data_mutex);
        current_mock_data = mock_data;  // 复制当前值
    }
    
    // ============ 步骤 2: 设置 Solver 的云台姿态 ============
    {
        double yaw_rad = current_mock_data.yaw * (M_PI / 180.0);
        double pitch_rad = current_mock_data.pitch * (M_PI / 180.0);
        double roll_rad = 0.0;
        
        Eigen::Quaterniond q_init = 
            Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
            Eigen::AngleAxisd(pitch_rad, Eigen::Vector3d::UnitY()) *
            Eigen::AngleAxisd(roll_rad, Eigen::Vector3d::UnitX());
        
        solver.set_R_gimbal2world(q_init);  // ⚠️ 在这里设置!
    }
    
    // ... 装甲板检测和追踪 ...
    
    // ============ 步骤 3: 重投影绘制 (在这里使用) ============
    if (!targets.empty()) {
        auto& target = targets.front();
        auto armor_xyza_list = target.armor_xyza_list();
        
        for (const Eigen::Vector4d& xyza : armor_xyza_list) {
            // 使用 solver 进行重投影
            auto image_points = solver.reproject_armor(
                xyza.head(3),      // xyz 位置
                xyza[3],           // 装甲板角度
                target.armor_type,
                target.name
            );
            
            // 绘制绿色多边形
            tools::draw_points(src_img, image_points, {0, 255, 0});
        }
    }
}
```

### 问题所在

1. **`solver.set_R_gimbal2world()` 在每帧开始时调用**
2. **使用的是 `current_mock_data` 的快照**
3. **即使 Foxglove 修改了 `gimbal_yaw`**:
   - 修改会更新 `mock_data.yaw`
   - 但 `current_mock_data` 已经是旧值的副本
   - 当前帧的重投影仍然使用旧的云台姿态

### 时序图

```
时间轴: ─────────────────────────────────────────────────>

帧 N:
  1. 读取 mock_data (yaw=0)
  2. 设置 solver.R_gimbal2world (yaw=0)
  3. 检测装甲板
  4. 重投影绘制 (使用 yaw=0)
  5. 显示图像
  
  [用户在 Foxglove 修改 yaw=10]
  
帧 N+1:
  1. 读取 mock_data (yaw=10) ✅ 新值
  2. 设置 solver.R_gimbal2world (yaw=10) ✅ 更新
  3. 检测装甲板
  4. 重投影绘制 (使用 yaw=10) ✅ 生效
  5. 显示图像
```

**结论**: 修改会在**下一帧**生效,但如果视频暂停或帧率很低,您可能感觉不到变化。

## ✅ 解决方案

### 方案 1: 实时更新云台姿态 (推荐)

在**重投影之前**再次更新 `solver` 的云台姿态:

```cpp
// 在重投影绘制之前
if (!targets.empty()) {
    auto& target = targets.front();
    auto armor_xyza_list = target.armor_xyza_list();
    
    // ✅ 重新获取最新的云台姿态
    uart::Receive_Data latest_mock_data;
    {
        std::lock_guard<std::mutex> lock(mock_data_mutex);
        latest_mock_data = mock_data;
    }
    
    // ✅ 重新设置云台姿态
    {
        double yaw_rad = latest_mock_data.yaw * (M_PI / 180.0);
        double pitch_rad = latest_mock_data.pitch * (M_PI / 180.0);
        double roll_rad = 0.0;
        
        Eigen::Quaterniond q = 
            Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
            Eigen::AngleAxisd(pitch_rad, Eigen::Vector3d::UnitY()) *
            Eigen::AngleAxisd(roll_rad, Eigen::Vector3d::UnitX());
        
        solver.set_R_gimbal2world(q);
    }
    
    // 现在重投影使用最新的云台姿态
    for (const Eigen::Vector4d& xyza : armor_xyza_list) {
        auto image_points = solver.reproject_armor(...);
        tools::draw_points(src_img, image_points, {0, 255, 0});
    }
}
```

### 方案 2: 添加调试信息

添加日志查看云台姿态是否真的更新了:

```cpp
// 在设置云台姿态后添加
solver.set_R_gimbal2world(q_init);
fmt::print("[Gimbal] Yaw: {:.2f}°, Pitch: {:.2f}°\n", 
           current_mock_data.yaw, current_mock_data.pitch);
```

### 方案 3: 暂停视频测试

如果视频在播放,很难看到单次修改的效果。可以:
1. 按空格暂停视频
2. 修改 `gimbal_yaw` 参数
3. 按任意键继续(播放下一帧)
4. 观察重投影是否改变

## 🔧 详细实现 (方案 1)

让我直接修改代码:

```cpp
// 在 test/minimum_vision.cpp 中,找到重投影绘制部分
// 大约在 line 445 附近

if (!targets.empty())
{
    auto& target = targets.front();
    
    // === 关键修复: 重新获取最新云台姿态 ===
    uart::Receive_Data realtime_mock_data;
    {
        std::lock_guard<std::mutex> lock(mock_data_mutex);
        realtime_mock_data = mock_data;
    }
    
    // 重新设置 Solver 的云台姿态 (用于重投影)
    {
        double yaw_rad = realtime_mock_data.yaw * (M_PI / 180.0);
        double pitch_rad = realtime_mock_data.pitch * (M_PI / 180.0);
        double roll_rad = 0.0;
        
        Eigen::Quaterniond q_realtime = 
            Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
            Eigen::AngleAxisd(pitch_rad, Eigen::Vector3d::UnitY()) *
            Eigen::AngleAxisd(roll_rad, Eigen::Vector3d::UnitX());
        
        solver.set_R_gimbal2world(q_realtime);
        
        // 可选: 打印调试信息
        // fmt::print("[Reprojection] Using Yaw: {:.2f}°, Pitch: {:.2f}°\n", 
        //            realtime_mock_data.yaw, realtime_mock_data.pitch);
    }
    
    // 现在的重投影会使用最新的云台姿态
    Eigen::VectorXd x = target.ekf_x();
    auto armor_xyza_list = target.armor_xyza_list();
    
    // ... 后续的重投影代码保持不变 ...
}
```

## 📊 验证方法

### 1. 编译运行
```bash
cd build && make -j4
LD_LIBRARY_PATH=./lib:../3rdparty/foxglove/lib ./bin/minimum_vision
```

### 2. 在 Foxglove 中测试
1. 连接到服务器
2. 暂停视频 (按空格)
3. 修改 `gimbal_yaw` 从 0 改为 10
4. 按任意键继续播放一帧
5. **应该看到**: 绿色装甲板框的位置发生变化

### 3. 预期效果
- ✅ 修改 `gimbal_yaw` → 重投影立即改变
- ✅ 绿色框会随着云台角度旋转
- ✅ 终端显示: `[Param] Updated gimbal_yaw: 10.0`

## 🎓 为什么会这样?

### 云台坐标系的作用

`R_gimbal2world` 矩阵描述了**云台坐标系到世界坐标系的旋转**。

在重投影时:
```
世界坐标 (装甲板) 
    ↓ R_gimbal2world^T
云台坐标
    ↓ R_camera2gimbal^T
相机坐标
    ↓ 相机内参 + 畸变
图像坐标 (像素)
```

**如果 `R_gimbal2world` 不更新**,即使装甲板的世界坐标正确,投影到图像时也会使用错误的云台姿态,导致重投影位置错误。

### 为什么需要两次设置?

1. **第一次设置** (帧开始): 
   - 用于 PnP 求解和追踪
   - 需要保持一致性
   
2. **第二次设置** (重投影前):
   - 用于可视化
   - 可以使用实时更新的值

## 🔮 进一步优化

### 1. 添加视觉反馈
在画面上显示当前云台姿态:
```cpp
tools::draw_text(src_img,
                 fmt::format("Gimbal: Yaw {:.1f}° Pitch {:.1f}°",
                             realtime_mock_data.yaw,
                             realtime_mock_data.pitch),
                 cv::Point(10, 180),
                 cv::Scalar(255, 255, 0),
                 0.6, 2);
```

### 2. 添加重投影误差显示
```cpp
// 计算重投影误差
double error = /* 计算逻辑 */;
tools::draw_text(src_img,
                 fmt::format("Reproj Error: {:.2f} px", error),
                 cv::Point(10, 210),
                 cv::Scalar(0, 255, 255),
                 0.6, 2);
```

## ✅ 总结

**问题**: `gimbal_yaw` 修改不影响重投影
**原因**: 重投影使用的是帧开始时的云台姿态快照
**解决**: 在重投影前重新获取并设置最新的云台姿态

修改后,您在 Foxglove 中调整 `gimbal_yaw` 参数时,应该能**立即**看到绿色装甲板框的位置变化。
