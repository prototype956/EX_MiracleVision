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
 *   c → 切换识别颜色（红/蓝），HUD 实时显示
 *
 * 调试基础设施（窗口 / Trackbar / FPS / 写回 YAML）
 * 均由 src/tool/debug/debug_session.hpp 提供，本文件仅含视觉业务逻辑。
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
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

// ── 辅助函数 1：解析命令行参数 ─────────────────────────────────────────────

std::pair<YAML::Node, bool> ParseArgs(int argc, char** argv) {
  YAML::Node cam_cfg;
  bool is_file_source = false;

  if (argc > 1) {
    const std::string SRC(argv[1]);
    const bool IS_INDEX =
        !SRC.empty() && std::all_of(SRC.begin(), SRC.end(),
                                    [](unsigned char chr) { return std::isdigit(chr) != 0; });
    if (IS_INDEX) {
      cam_cfg["source"] = std::stoi(SRC);
    } else {
      cam_cfg["source"] = SRC;
      is_file_source = true;
    }
  } else {
    cam_cfg["source"] = 0;
  }

  return {cam_cfg, is_file_source};
}

// ── 辅助函数 2：初始化算法模块 ─────────────────────────────────────────────

bool InitModules(mv::modules::BasicArmorDetector& detector, mv::modules::PnpSolver& solver,
                 mv::modules::SimplePredictor& predictor, const YAML::Node& root_cfg) {
  return detector.Init(root_cfg) && solver.Init(root_cfg) && predictor.Init(root_cfg);
}

// ── 辅助函数 3：视频帧采集失败时的重开逻辑 ────────────────────────────────

/**
 * @brief 视频播完后尝试重新打开以循环播放。
 * @return true → 应 continue 主循环；false → 应 break（无法重开或非视频文件）
 */
bool HandleEndOfSource(bool loop_video, bool is_file_source, mv::hal::OpenCvCamera& camera,
                       const YAML::Node& cam_cfg, mv::modules::SimplePredictor& predictor,
                       const YAML::Node& root_cfg) {
  if (!loop_video || !is_file_source) {
    return false;
  }
  camera.Close();
  if (!camera.Open(cam_cfg)) {
    MV_LOG_ERROR("video-test", "视频重新打开失败，退出");
    return false;
  }
  predictor.Init(root_cfg);
  MV_LOG_INFO("video-test", "视频循环重新播放");
  return true;
}

// ── 辅助函数 4：注册 Trackbar 参数 ─────────────────────────────────────────

void RegisterDetectorParams(mv::tool::DebugSession& dbg,
                            mv::modules::BasicArmorDetector::Params& params) {
  dbg.AddParam({"Thresh         ", "light_thresh", params.light_thresh, 255,
                [&params](int val) { params.light_thresh = val; },
                [&params] { return static_cast<double>(params.light_thresh); }});

  dbg.AddParam({"GreenThresh    ", "green_thresh", params.green_thresh, 255,
                [&params](int val) { params.green_thresh = val; },
                [&params] { return static_cast<double>(params.green_thresh); }});

  dbg.AddParam({"WhiteThresh    ", "white_thresh", params.white_thresh, 255,
                [&params](int val) { params.white_thresh = val; },
                [&params] { return static_cast<double>(params.white_thresh); }});

  dbg.AddParam({"MaxAngle x10   ", "max_light_angle",
                static_cast<int>(params.max_light_angle * 10.F), 900,
                [&params](int val) { params.max_light_angle = static_cast<float>(val) / 10.F; },
                [&params] { return static_cast<double>(params.max_light_angle); }});

  dbg.AddParam({"MaxLightR x100 ", "max_light_ratio",
                static_cast<int>(params.max_light_ratio * 100.F), 100,
                [&params](int val) { params.max_light_ratio = static_cast<float>(val) / 100.F; },
                [&params] { return static_cast<double>(params.max_light_ratio); }});

  dbg.AddParam({"MinLightR x100 ", "min_light_ratio",
                static_cast<int>(params.min_light_ratio * 100.F), 100,
                [&params](int val) { params.min_light_ratio = static_cast<float>(val) / 100.F; },
                [&params] { return static_cast<double>(params.min_light_ratio); }});

  dbg.AddParam({"MinArmorR x10  ", "min_armor_ratio",
                static_cast<int>(params.min_armor_ratio * 10.F), 100,
                [&params](int val) { params.min_armor_ratio = static_cast<float>(val) / 10.F; },
                [&params] { return static_cast<double>(params.min_armor_ratio); }});

  dbg.AddParam({"MaxArmorR x10  ", "max_armor_ratio",
                static_cast<int>(params.max_armor_ratio * 10.F), 100,
                [&params](int val) { params.max_armor_ratio = static_cast<float>(val) / 10.F; },
                [&params] { return static_cast<double>(params.max_armor_ratio); }});

  dbg.AddParam({"AngleDiff x10  ", "max_angle_diff", static_cast<int>(params.max_angle_diff * 10.F),
                900, [&params](int val) { params.max_angle_diff = static_cast<float>(val) / 10.F; },
                [&params] { return static_cast<double>(params.max_angle_diff); }});

  dbg.AddParam({"MinArea        ", "min_area", static_cast<int>(params.min_area), 500,
                [&params](int val) { params.min_area = static_cast<float>(val); },
                [&params] { return static_cast<double>(params.min_area); }});
}

}  // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  try {
    // ── 1. 解析命令行参数 ────────────────────────────────────────────────────
    const auto [cam_cfg, is_file_source] = ParseArgs(argc, argv);

    // ── 2. 加载配置 + 初始化 Logger ─────────────────────────────────────────
    auto& cfg = mv::ConfigManager::Instance();
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
    mv::Logger::Instance().Init("logs", spdlog::level::info, true);
    MV_LOG_INFO("video-test", "══════════ mv-video-test 启动 ══════════");

    // ── 3. 初始化算法模块 ────────────────────────────────────────────────────
    const YAML::Node ROOT_CFG = cfg.Subtree();

    mv::modules::BasicArmorDetector detector;
    mv::modules::PnpSolver solver;
    mv::modules::SimplePredictor predictor;

    if (!InitModules(detector, solver, predictor, ROOT_CFG)) {
      MV_LOG_ERROR("video-test", "模块初始化失败");
      return EXIT_FAILURE;
    }

    // ── 读取调试参数覆盖文件（s 键保存的上次调参结果）──────────────────
    const std::string OVERRIDE_YAML =
        std::string(CONFIG_FILE_PATH) + "/debug/debug_override.yaml";
    if (std::filesystem::exists(OVERRIDE_YAML)) {
      try {
        const YAML::Node OV = YAML::LoadFile(OVERRIDE_YAML);
        if (OV && OV["detector"]) {
          detector.Init(OV);  // 用覆盖文件的 detector 节点重新初始化
          MV_LOG_INFO("video-test", "已应用调试参数覆盖: {}", OVERRIDE_YAML);
        }
      } catch (const std::exception& e) {
        MV_LOG_WARN("video-test", "读取调试覆盖文件失败（忽略）: {}", e.what());
      }
    }
    detector.EnableDebug(true);
    MV_LOG_INFO("video-test", "所有模块初始化成功");

    // ── 4. 打开相机/视频 ─────────────────────────────────────────────────────
    mv::hal::OpenCvCamera camera;
    if (!camera.Open(cam_cfg)) {
      MV_LOG_ERROR("video-test", "无法打开图像源");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("video-test", "图像源打开成功");

    // ── 5. 配置 DebugSession ─────────────────────────────────────────────────
    mv::tool::DebugSession dbg;
    dbg.Init({.main_window = "mv-video-test",
              .debug_window = "mv-video-debug",
              .save_yaml = std::string(CONFIG_FILE_PATH) + "/debug/debug_override.yaml"});

    auto params = detector.GetParams();
    RegisterDetectorParams(dbg, params);

    dbg.BindKey('s', [&dbg] { dbg.SaveParams(); });

    bool loop_video = is_file_source;
    dbg.BindKey('l', [&loop_video] { loop_video = !loop_video; });

    // ── 6. 读取敌方颜色 + 切换键 ────────────────────────────────────────────
    const auto ENEMY_STR_INIT = cfg.Get<std::string>("auto_aim.enemy_color", "red");
    mv::ArmorColor enemy_color =
        (ENEMY_STR_INIT == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
    MV_LOG_INFO("video-test", "敌方颜色: {}", ENEMY_STR_INIT);

    dbg.BindKey('c', [&enemy_color] {
      enemy_color =
          (enemy_color == mv::ArmorColor::RED) ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
    });

    // ── 7. 主循环 ────────────────────────────────────────────────────────────
    cv::Mat frame;

    while (true) {
      const auto [quit, paused] = dbg.Poll();
      if (quit) {
        break;
      }
      if (paused) {
        continue;
      }

      dbg.ApplyParams();
      detector.SetParams(params);

      if (!camera.Grab(frame) || frame.empty()) {
        if (HandleEndOfSource(loop_video, is_file_source, camera, cam_cfg, predictor, ROOT_CFG)) {
          continue;
        }
        MV_LOG_INFO("video-test", "视频结束或相机断开");
        break;
      }

      const auto T_FRAME = std::chrono::steady_clock::now();
      auto detections = detector.Detect(frame, enemy_color);
      for (auto& det : detections) {
        solver.Solve(det);
      }
      const mv::GimbalControl CTRL = predictor.Predict(detections, T_FRAME, enemy_color);

      const std::string STATUS = std::string("Enemy: ") +
                                 ((enemy_color == mv::ArmorColor::RED) ? "RED" : "BLUE") +
                                 std::string("  [c]toggle");
      dbg.TickFrame(!detections.empty(), static_cast<int>(detections.size()));
      dbg.Feed(frame, detector.GetDebugData(), detections, CTRL, detector.GetParams(), STATUS);
    }

    // ── 8. 统计输出 + 清理 ───────────────────────────────────────────────────
    dbg.PrintStats();
    camera.Close();
    MV_LOG_INFO("video-test", "══════════ mv-video-test 结束 ══════════");

  } catch (const std::exception& exc) {
    std::cerr << "[video-test] FATAL: " << exc.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
