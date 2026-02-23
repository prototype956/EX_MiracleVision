/**
 * @file MiracleVision.hpp
 * @author dgsyrc (yrcminecraft@foxmail.com)
 * @brief Main Function // 主函数/程序入口
 * @date 2024-02-20
 * @copyright Copyright (c) 2024 dgsyrc
 */
#pragma once

// C++ 标准库头文件
#include <chrono>  // 用于时间点和时间间隔
#include <ctime>   // 用于时间相关函数（如 time_t）
#include <string>  // 用于字符串操作

// fmt 库，用于格式化输出
#include <fmt/color.h>  // 颜色和样式
#include <fmt/core.h>   // 核心格式化

// OpenCV 库
#include <opencv2/core.hpp>  // 核心功能（如 Mat, Size）

// 自定义模块头文件
#ifdef HAS_MVSDK
#include "devices/camera/mv_video_capture.hpp"  // MindVision 相机视频捕获（需要 SDK）
#endif
#include "devices/serial/uart_serial.hpp"      // UART 串口通信
#include "module/angle_solve/angle_solve.hpp"  // 角度解算模块
#include "module/armor/basic_armor.hpp"        // 基础装甲板检测
// DNN_armor.hpp 暂未在主程序中启用，待重构后引入
// #include "module/armor/DNN_armor.hpp"
#include "module/buff/basic_buff.hpp"  // 基础能量机关检测
#include "module/buff/new_buff.hpp"  // 新版能量机关检测模块（未在主程序中直接使用）
#include "module/camera/camera_calibration.hpp"  // 相机标定模块
// onnx_inferring.hpp 暂未在主程序中启用，待重构后引入
// #include "module/ml/onnx_inferring.hpp"
#include "module/roi/basic_roi.hpp"   // ROI (Region of Interest) 区域处理
#include "utils/debug_tools.hpp"      // 调试工具（如 FPS 计算）
#include "utils/plotter.hpp"          // 数据绘图工具（未在主程序中直接使用）
#include "utils/reset_mv_camera.hpp"  // 重置 MindVision 相机的工具函数

// 全局常量和变量定义

// 标识符，用于在终端打印信息时显示，带绿色粗体
auto idntifier = fmt::format(fg(fmt::color::green) | fmt::emphasis::bold, "MiracleVision");

// 视频帧率（捕获或录制时使用）
int cap_fps = 30;
// OpenCV 视频写入对象，用于录制视频
cv::VideoWriter writer;
// 当前时间（秒）
time_t time_now = time(0);
// 瞄准成功/是否开火标志
bool fire;
// FPS 测试是否完成标志
bool test_fps;

// 已处理的帧计数器
long long rec_time = 0;

// 录制/FPS 测试帧数计数器
int rec_cnt = 0;
