/**
 * @file video_pipeline_test.cpp
 * @brief 离线视频检测链路测试（单线程串联）
 *
 * 【用法】
 * @code
 *   mv-video-test /path/to/armor_video.mp4   # 视频文件
 *   mv-video-test 0                           # 摄像头索引
 *   mv-video-test                             # 缺省：摄像头 0
 * @endcode
 *
 * 【按键（内置于 DebugSession）】
 *   q / ESC → 退出             空格 → 暂停/继续
 *   1 → 检测结果视图           2 → 通道差分图
 *   3 → 二值化图               4 → 灯条可视化图
 *   s → 将当前参数写入 debug_override.yaml
 *   l → 切换视频循环播放（默认开启，仅对视频文件有效）
 *
 * 调试基础设施（窗口 / Trackbar / FPS / 写回 YAML）
 * 均由 src/tool/debug_session.hpp 提供，本文件仅含视觉业务逻辑。
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "interfaces/types.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "modules/simple_predictor/simple_predictor.hpp"
#include "tool/debug/debug_session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

int main(int argc, char** argv) {
  // ── 1. 解析相机/视频源 ───────────────────────────────────────────────────
  YAML::Node cam_cfg;
  bool is_file_source = false;  // 是否为视频文件（而非摄像头索引）
  if (argc > 1) {
    const std::string src(argv[1]);
    const bool is_index = !src.empty() && std::all_of(src.begin(), src.end(), [](unsigned char c) {
      return std::isdigit(c) != 0;
    });
    if (is_index) {
      cam_cfg["source"] = std::stoi(src);
    } else {
      cam_cfg["source"] = src;
      is_file_source = true;
    }
  } else {
    cam_cfg["source"] = 0;
  }

  // ── 2. 加载配置 + 初始化 Logger ─────────────────────────────────────────
  auto& cfg = mv::ConfigManager::Instance();
  try {
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
  } catch (const std::exception& exc) {
    std::cerr << "[video-test] FATAL: " << exc.what() << "\n";
    return EXIT_FAILURE;
  }
  mv::Logger::Instance().Init("logs", spdlog::level::info, true);
  MV_LOG_INFO("video-test", "══════════ mv-video-test 启动 ══════════");

  // ── 3. 初始化算法模块 ────────────────────────────────────────────────────
  const YAML::Node root_cfg = cfg.Subtree();

  mv::modules::BasicArmorDetector detector;
  mv::modules::PnpSolver solver;
  mv::modules::SimplePredictor predictor;

  if (!detector.Init(root_cfg) || !solver.Init(root_cfg) || !predictor.Init(root_cfg)) {
    MV_LOG_ERROR("video-test", "模块初始化失败");
    return EXIT_FAILURE;
  }
  detector.EnableDebug(true);
  MV_LOG_INFO("video-test", "所有模块初始化成功");

  // ── 4. 打开相机/视频 ─────────────────────────────────────────────────────
  mv::hal::OpenCvCamera camera;
  if (!camera.Open(cam_cfg)) {
    MV_LOG_ERROR("video-test", "无法打开图像源，请通过 argv[1] 指定视频路径或摄像头索引");
    return EXIT_FAILURE;
  }
  MV_LOG_INFO("video-test", "图像源打开成功");

  // ── 5. 配置 DebugSession ─────────────────────────────────────────────────
  mv::tool::DebugSession dbg;
  dbg.Init({.main_window = "mv-video-test",
            .debug_window = "mv-video-debug",
            .save_yaml = std::string(CONFIG_FILE_PATH) + "/debug_override.yaml"});

  // 注册可调参数（lambda 按引用捕获本地 params，每帧由 ApplyParams() 推送）
  auto params = detector.GetParams();
  dbg.AddParam({"Thresh         ", "light_thresh", params.light_thresh, 255,
                [&params](int v) { params.light_thresh = v; },
                [&params] { return static_cast<double>(params.light_thresh); }});
  dbg.AddParam({"MaxAngle x10   ", "max_light_angle",
                static_cast<int>(params.max_light_angle * 10.F), 900,
                [&params](int v) { params.max_light_angle = static_cast<float>(v) / 10.F; },
                [&params] { return static_cast<double>(params.max_light_angle); }});
  dbg.AddParam({"MinArmorR x10  ", "min_armor_ratio",
                static_cast<int>(params.min_armor_ratio * 10.F), 100,
                [&params](int v) { params.min_armor_ratio = static_cast<float>(v) / 10.F; },
                [&params] { return static_cast<double>(params.min_armor_ratio); }});
  dbg.AddParam({"MaxArmorR x10  ", "max_armor_ratio",
                static_cast<int>(params.max_armor_ratio * 10.F), 100,
                [&params](int v) { params.max_armor_ratio = static_cast<float>(v) / 10.F; },
                [&params] { return static_cast<double>(params.max_armor_ratio); }});
  dbg.AddParam({"AngleDiff x10  ", "max_angle_diff", static_cast<int>(params.max_angle_diff * 10.F),
                900, [&params](int v) { params.max_angle_diff = static_cast<float>(v) / 10.F; },
                [&params] { return static_cast<double>(params.max_angle_diff); }});

  // 'q'/'ESC'/空格/1-4 已由 DebugSession 内置注册
  dbg.BindKey('s', [&dbg] { dbg.SaveParams(); });

  // 'l'：切换视频循环播放（仅当 is_file_source 时生效）
  bool loop_video = is_file_source;  // 视频文件时默认开启
  dbg.BindKey('l', [&loop_video] {
    loop_video = !loop_video;
    MV_LOG_INFO("video-test", "循环播放: {}", loop_video ? "开" : "关");
  });

  // ── 6. 读取敌方颜色 ──────────────────────────────────────────────────────
  const std::string ENEMY_STR = cfg.Get<std::string>("auto_aim.enemy_color", "red");
  const mv::ArmorColor ENEMY = (ENEMY_STR == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
  MV_LOG_INFO("video-test", "敌方颜色: {}", ENEMY_STR);

  // ── 7. 主循环 ────────────────────────────────────────────────────────────
  cv::Mat frame;

  while (true) {
    // 处理按键；paused 时不采集新帧
    const auto [quit, paused] = dbg.Poll();
    if (quit) {
      break;
    }
    if (paused) {
      continue;
    }

    // Trackbar → params → detector
    dbg.ApplyParams();
    detector.SetParams(params);

    // 采集帧
    if (!camera.Grab(frame) || frame.empty()) {
      if (loop_video && is_file_source) {
        // 视频播放完毕 → 关闭并重新打开，从头开始
        camera.Close();
        if (!camera.Open(cam_cfg)) {
          MV_LOG_ERROR("video-test", "视频重新打开失败，退出");
          break;
        }
        predictor.Init(root_cfg);  // 重置预测器，避免跨帧状态污染
        MV_LOG_INFO("video-test", "视频循环重新播放");
        continue;
      }
      MV_LOG_INFO("video-test", "视频结束或相机断开");
      break;
    }

    // 检测 → 解算 → 预测
    const auto t_frame = std::chrono::steady_clock::now();
    auto detections = detector.Detect(frame, ENEMY);
    for (auto& det : detections) {
      solver.Solve(det);
    }
    const mv::GimbalControl ctrl = predictor.Predict(detections, t_frame, ENEMY);

    // 统计 + 渲染
    dbg.TickFrame(!detections.empty(), static_cast<int>(detections.size()));
    dbg.Feed(frame, detector.GetDebugData(), detections, ctrl, detector.GetParams());
  }

  // ── 8. 统计输出 + 清理 ───────────────────────────────────────────────────
  dbg.PrintStats();
  camera.Close();
  // cv::destroyAllWindows() 由 DebugSession 析构时自动调用
  MV_LOG_INFO("video-test", "══════════ mv-video-test 结束 ══════════");
  return EXIT_SUCCESS;
}
