# sim_camera CMake 接入说明

本次改动在 mv-hal-camera 目标中新增 sim_camera.cpp，用于编译仿真输入后端。

## 变更点

文件: src/hal/camera/CMakeLists.txt

- add_library(mv-hal-camera ...) 中新增 sim_camera.cpp。
- target_link_libraries(... PRIVATE ...) 中新增 opencv_imgcodecs。

## 原因

sim_camera.cpp 需要使用 cv::imdecode 解码 JPEG 流，符号由 opencv_imgcodecs 提供。

## 影响范围

- 不改变现有 OpenCvCamera/MindVisionCamera 的接口或构建方式。
- USE_MINDVISION_SDK 开关行为保持不变。

## 验证

构建 mv-hal-camera 与主程序时应无未解析符号错误，并可在 camera.backend=sim 配置下启动。
