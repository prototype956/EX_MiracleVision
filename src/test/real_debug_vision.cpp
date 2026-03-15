/**
 * @file real_debug_vision.cpp
 * @brief 步兵实机联调程序（检测 → 预测 → 决策 → 串口下发）
 *
 * 【功能概述】
 *   以 MindVision 工业相机为图像来源，完整串联传统视觉检测链路：
 *     MindVisionCamera → BasicArmorDetector → PnpSolver
 *         → EkfPredictor → CooldownVoter → RmShooter → UartSerial
 *
 *   同时通过两路调试通道实时观察中间状态：
 *   - 终端 HUD（TerminalHUD）：固定行刷新，显示 FPS / 检测 / 锁定 / 角度
 *   - Foxglove Studio（FoxgloveSink）：WebSocket ws://HOST:8765，推送图像、
 *     检测框 3D、跟踪状态、云台指令等七路 Topic
 *
 * 【上行帧利用】
 *   每帧接收 MCU 上行帧（28 字节，协议见 rm_protocol.hpp）：
 *   - 四元数 → EkfPredictor::SetGimbalOrientation()（世界系建模）
 *   - 弹速   → 实时更新 RmShooter 弹道补偿参数
 *   - 颜色   → 动态切换敌方颜色（支持哨兵自动颜色切换）
 *
 * 【调试参数（运行时可热改，无需重启）】
 *   Foxglove 参数面板可修改：
 *     enemy_color / ekf.* / voter.* （字段与 predict_voter_test 完全一致）
 *
 * 【配置来源】
 *   运行参数统一从 configs/vision.yaml 读取：
 *   - auto_aim.enemy_color
 *   - debug.foxglove.port
 *   - serial.* / camera.* 等硬件参数
 *
 * 【关键设计决策】
 *   1. 串口接收在主循环中非阻塞 Recv，不单独起线程，保证时序简单；
 *   2. Foxglove 按帧号节流（每 N 帧发一次），算法始终全速运行；
 *   3. CooldownVoter auto_fire 默认 false，可通过 Foxglove 参数面板开启；
 *   4. 上行帧解析失败时沿用上一帧的四元数，不重置预测器。
 */

// ============================================================================
// Module 1: includes + 全局信号处理
// ============================================================================

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/camera/mindvision_camera.hpp"
#include "hal/serial/rm_protocol.hpp"
#include "hal/serial/uart_serial.hpp"
#include "interfaces/types.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/armor_detector/roi_manager.hpp"
#include "modules/cooldown_voter/cooldown_voter.hpp"
#include "modules/ekf_predictor/ekf_predictor.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "modules/rm_shooter/rm_shooter.hpp"
#include "tool/debug/metrics_tracker.hpp"
#include "tool/debug/terminal_hud.hpp"
#include "tool/foxglove/foxglove_sink.hpp"
// PredictParamManager 复用离线调试的参数管理（双向 Foxglove 同步）
#include "tool/debug/predict_param_manager.hpp"
#include "tool/foxglove/detail/serial_visualizer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

// ── 全局退出标志（Ctrl+C 触发）─────────────────────────────────────────────
namespace {

std::atomic<bool>& QuitFlag() {
  static std::atomic<bool> quit_flag{false};
  return quit_flag;
}

extern "C" void SigIntHandler(int /*sig*/) noexcept {
  QuitFlag().store(true, std::memory_order_relaxed);
}

// ============================================================================
// Module 2: 上行帧解析 + 运行时共享状态
// ============================================================================

/**
 * @brief 运行时共享状态（单线程，主循环直接读写，无需锁）
 *
 * 上行帧每帧更新：四元数、弹速、敌方颜色。
 * 下行帧每帧写入：seq 计数。
 */
struct RuntimeState {
  Eigen::Quaterniond gimbal_quat{Eigen::Quaterniond::Identity()};  ///< MCU 上报四元数
  float bullet_speed{15.0F};                                       ///< MCU 上报弹速(m/s)
  mv::ArmorColor enemy_color{mv::ArmorColor::RED};                 ///< 当前识别颜色
  bool serial_alive{false};  ///< 上行帧最近一帧是否成功解析
};

void UpdateRuntimeStateFromUpFrame(const mv::protocol::UpFrame& up_frame,
                                   RuntimeState& runtime_state) {
  constexpr double QUATERNION_SCALE = 1.0 / 10000.0;
  runtime_state.gimbal_quat =
      Eigen::Quaterniond(up_frame.q_w * QUATERNION_SCALE, up_frame.q_x * QUATERNION_SCALE,
                         up_frame.q_y * QUATERNION_SCALE, up_frame.q_z * QUATERNION_SCALE);
  runtime_state.gimbal_quat.normalize();

  if (up_frame.bullet_speed > 0) {
    runtime_state.bullet_speed = static_cast<float>(up_frame.bullet_speed) * 0.01F;
  }

  if (up_frame.color == 0U) {
    runtime_state.enemy_color = mv::ArmorColor::BLUE;
  } else if (up_frame.color == 1U) {
    runtime_state.enemy_color = mv::ArmorColor::RED;
  }

  runtime_state.serial_alive = true;
}

}  // namespace

// ============================================================================
// Module 3: main() — 配置加载 + 所有对象创建 + Foxglove 初始化
// ============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity,readability-function-size,readability-identifier-naming)
int main() {
  std::signal(SIGINT, SigIntHandler);

  try {
    // ── 日志初始化 ─────────────────────────────────────────────────────────
    // 控制台仅 info 及以上，避免 debug 消息滚动破坏 TerminalHUD 固定行；
    // 文件写入 trace 级别，保留完整记录供复盘。
    mv::Logger::Instance().Init("logs", spdlog::level::info, true);

    // ── 加载配置文件 ────────────────────────────────────────────────────────
    auto& cfg = mv::ConfigManager::Instance();
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");

    const auto ENEMY_COLOR_NAME = cfg.Get<std::string>("auto_aim.enemy_color", "red");
    const mv::ArmorColor CONFIG_ENEMY_COLOR =
        (ENEMY_COLOR_NAME == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
    const uint16_t FOXGLOVE_PORT = static_cast<uint16_t>(cfg.Get<int>("debug.foxglove.port", 8765));

    MV_LOG_INFO("real-debug", "══════════ mv-real-debug-vision 启动 ══════════");
    MV_LOG_INFO("real-debug", "Foxglove 端口: {}  初始颜色: {}", FOXGLOVE_PORT,
                CONFIG_ENEMY_COLOR == mv::ArmorColor::BLUE ? "BLUE" : "RED");

    // ── 参数管理器（复用 predict_voter_test 的双向 Foxglove 参数同步）───────
    mv::tool::PredictParamManager param_manager;
    param_manager.State().enemy_color = CONFIG_ENEMY_COLOR;

    YAML::Node root_cfg = cfg.Subtree();
    param_manager.InjectParamsToYaml(root_cfg);

    // ── 创建算法模块 ─────────────────────────────────────────────────────────
    mv::modules::BasicArmorDetector detector;
    mv::modules::PnpSolver solver;
    mv::modules::EkfPredictor predictor;
    mv::modules::CooldownVoter voter;
    mv::modules::RmShooter shooter;

    if (!detector.Init(root_cfg) || !solver.Init(root_cfg)) {
      MV_LOG_ERROR("real-debug", "检测器/解算器初始化失败，退出");
      return EXIT_FAILURE;
    }
    if (!predictor.Init(root_cfg)) {
      MV_LOG_ERROR("real-debug", "EkfPredictor 初始化失败，退出");
      return EXIT_FAILURE;
    }
    if (!voter.Init(root_cfg)) {
      MV_LOG_ERROR("real-debug", "CooldownVoter 初始化失败，退出");
      return EXIT_FAILURE;
    }
    if (!shooter.Init(root_cfg)) {
      MV_LOG_ERROR("real-debug", "RmShooter 初始化失败，退出");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("real-debug", "算法模块初始化完成");

    // ── 打开工业相机 ─────────────────────────────────────────────────────────
    mv::hal::MindVisionCamera camera;
    if (!camera.Open(cfg.Subtree("camera"))) {
      MV_LOG_ERROR("real-debug", "MindVision 相机打开失败，请检查硬件连接和驱动");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("real-debug", "MindVision 相机打开成功");

    // ── 打开串口 ─────────────────────────────────────────────────────────────
    mv::hal::UartSerial serial;
    YAML::Node serial_cfg;
    serial_cfg["device"] = cfg.Get<std::string>("serial.device", "/dev/ttyUSB0");
    serial_cfg["baudrate"] = cfg.Get<int>("serial.baudrate", 115200);
    // 支持配置中指定的备选端口
    const auto FALLBACK_DEVICES = cfg.Get<std::vector<std::string>>("serial.fallback_devices", {});
    if (!FALLBACK_DEVICES.empty()) {
      serial_cfg["fallback_devices"] = FALLBACK_DEVICES;
    }

    if (!serial.Open(serial_cfg)) {
      // 串口失败时仅告警，不退出——可在无下位机环境下做纯视觉调试
      MV_LOG_WARN("real-debug", "串口打开失败，将在无串口模式下运行（仅视觉 + Foxglove）");
    } else {
      MV_LOG_INFO("real-debug", "串口 {} @ {} baud 打开成功",
                  cfg.Get<std::string>("serial.device", "/dev/ttyUSB0"),
                  cfg.Get<int>("serial.baudrate", 115200));
    }

    // ── Foxglove 初始化 ─────────────────────────────────────────────────────
    mv::tool::FoxgloveSinkConfig fox_cfg;
    fox_cfg.port = FOXGLOVE_PORT;
    fox_cfg.use_jpeg = true;
    fox_cfg.jpeg_quality = cfg.Get<int>("debug.foxglove.jpeg_quality", 50);
    // 实机调试时用较小的推流分辨率，降低 WebSocket 带宽占用
    fox_cfg.publish_width = cfg.Get<int>("debug.foxglove.display_width", 640);
    fox_cfg.publish_height = cfg.Get<int>("debug.foxglove.display_height", 480);
    fox_cfg.armor_small_half_w = cfg.Get<double>("armor.small_half_w", 0.0675);
    fox_cfg.armor_big_half_w = cfg.Get<double>("armor.big_half_w", 0.115);
    fox_cfg.armor_half_h = cfg.Get<double>("armor.half_h", 0.0275);

    mv::tool::FoxgloveSink sink{fox_cfg};
    sink.Start();
    MV_LOG_INFO("real-debug", "Foxglove 服务已启动 (ws://0.0.0.0:{})", FOXGLOVE_PORT);

    // 注册参数双向同步：Foxglove 端修改参数时触发 EKF/Voter 重初始化
    param_manager.Register(sink);
    param_manager.PushToFoxglove(sink);

    // 发布静态 TF：gimbal → camera（外参固定，只发一次）
    {
      Eigen::Matrix3d r_c2g =
          (Eigen::Matrix3d() << 1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0).finished();

      if (root_cfg["calibration"] && root_cfg["calibration"]["R_camera_to_gimbal"]) {
        try {
          auto rotation_values =
              root_cfg["calibration"]["R_camera_to_gimbal"].as<std::vector<double>>();
          if (rotation_values.size() == 9) {
            r_c2g = Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(
                rotation_values.data());
          }
        } catch (...) {
          MV_LOG_WARN("real-debug", "解析 R_camera_to_gimbal 失败，使用 Y 翻转默认");
        }
      }

      Eigen::Matrix4d t_g2c = Eigen::Matrix4d::Identity();
      t_g2c.block<3, 3>(0, 0) = r_c2g.transpose();
      const int64_t TIMESTAMP_NS =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
      sink.PublishTransform("gimbal", "camera", t_g2c, TIMESTAMP_NS);
      MV_LOG_INFO("real-debug", "已发布静态 TF: gimbal → camera");
    }

    // ── 工具对象 ─────────────────────────────────────────────────────────────
    mv::modules::RoiManager roi_mgr;
    mv::tool::MetricsTracker metrics{30};
    mv::tool::TerminalHUD hud;
    mv::tool::SerialVisualizer serial_viz;

    RuntimeState rt_state;
    rt_state.enemy_color = param_manager.State().enemy_color;

    // Foxglove 发布节流：算法全速跑，可视化按 target_fps 降频推送
    const int FOX_TARGET_FPS = cfg.Get<int>("debug.foxglove.target_fps", 30);
    int fox_interval = 4;  // 动态调整：每隔多少帧发一次
    uint64_t last_fox_frame = 0;
    uint64_t frame_idx = 0;
    (void)FOX_TARGET_FPS;  // 后续可用于动态调整 fox_interval

    // 弹速热更新节流（每 60 帧更新一次，避免频繁重初始化 RmShooter）
    float last_injected_speed = rt_state.bullet_speed;
    uint64_t last_speed_frame = 0;

    std::cout << "\n[real-debug-vision] 已就绪：\n"
              << "  Foxglove Studio → ws://localhost:" << FOXGLOVE_PORT << "\n"
              << "  按 Ctrl+C 退出\n\n";

    // ========================================================================
    // Module 4: 主循环 — 串口接收 + 检测 + PnP + 预测 + 决策
    // ========================================================================

    cv::Mat frame;

    while (!QuitFlag().load(std::memory_order_relaxed)) {
      // ── Step 1: 参数热更新（Foxglove 端修改参数时触发）─────────────────────
      if (param_manager.ConsumeReinitEkf()) {
        param_manager.InjectParamsToYaml(root_cfg);
        predictor.Reset();
        voter.Reset();
        roi_mgr.Reset();

        if (!predictor.Init(root_cfg)) {
          MV_LOG_WARN("real-debug", "EkfPredictor 重初始化失败，跳过本次");
          continue;
        }
        if (!voter.Init(root_cfg)) {
          MV_LOG_WARN("real-debug", "CooldownVoter 重初始化失败，跳过本次");
          continue;
        }
        MV_LOG_INFO("real-debug", "EkfPredictor + CooldownVoter 参数已热更新");
      }
      // 从参数管理器同步敌方颜色（Foxglove 端可修改）
      rt_state.enemy_color = param_manager.State().enemy_color;

      // ── Step 2: 串口接收上行帧（非阻塞）──────────────────────────────────
      if (serial.IsOpen()) {
        constexpr std::size_t RX_BUFFER_SIZE = 256;
        std::array<uint8_t, RX_BUFFER_SIZE> rx_buffer{};
        std::size_t received = 0;
        if (serial.Recv(rx_buffer.data(), rx_buffer.size(), received) && received > 0) {
          mv::protocol::UpFrame up_frame{};
          const bool PARSE_OK =
              mv::protocol::TryParseUpFrame(rx_buffer.data(), received, &up_frame);
          serial_viz.OnRxData(rx_buffer.data(), received, PARSE_OK ? &up_frame : nullptr, PARSE_OK);
          if (PARSE_OK) {
            UpdateRuntimeStateFromUpFrame(up_frame, rt_state);
            // 四元数注入 EKF（每帧必须在 Predict() 前调用）
            predictor.SetGimbalOrientation(rt_state.gimbal_quat);
          } else {
            rt_state.serial_alive = false;
          }
        }

        // 弹速热更新：每 60 帧（约 2s）同步一次，避免频繁重初始化
        const float BULLET_SPEED_DIFF = std::abs(rt_state.bullet_speed - last_injected_speed);
        if (frame_idx - last_speed_frame >= 60 && BULLET_SPEED_DIFF > 0.5F) {
          param_manager.State().ekf_bullet_speed = static_cast<double>(rt_state.bullet_speed);
          param_manager.InjectParamsToYaml(root_cfg);
          root_cfg["auto_aim"]["shooter"]["bullet_speed"] = rt_state.bullet_speed;
          if (shooter.Init(root_cfg)) {
            MV_LOG_INFO("real-debug", "弹速更新: {:.1f} m/s", rt_state.bullet_speed);
          }
          last_injected_speed = rt_state.bullet_speed;
          last_speed_frame = frame_idx;
        }
      }

      // ── Step 3: 抓帧 ──────────────────────────────────────────────────────
      if (!camera.Grab(frame) || frame.empty()) {
        MV_LOG_WARN("real-debug", "Grab 失败，跳过本帧");
        continue;
      }
      // 帧时间戳：在 Grab() 返回后立即记录，用于 EKF 时基
      const auto FRAME_TIMESTAMP = std::chrono::steady_clock::now();
      ++frame_idx;

      // ── Step 4: ROI 自适应裁剪 + 检测 ─────────────────────────────────────
      auto [roi_frame, roi_offset] = roi_mgr.Crop(frame);
      auto dets = detector.Detect(roi_frame, rt_state.enemy_color);
      // 坐标还原到全图坐标系，并更新 ROI 状态（连续无目标时自动回退全图）
      roi_mgr.RestoreAndUpdate(dets, roi_offset, frame.size());

      // ── Step 5: PnP 解算 ──────────────────────────────────────────────────
      for (auto& det : dets) {
        solver.Solve(det);
      }
      // 过滤解算失败的检测（is_solved=false 的目标 EKF 无法使用）
      std::vector<mv::Detection> solved_dets;
      solved_dets.reserve(dets.size());
      for (const auto& det : dets) {
        if (det.is_solved) {
          solved_dets.push_back(det);
        }
      }

      // ── Step 6: EKF 预测 ──────────────────────────────────────────────────
      auto ctrl = predictor.Predict(solved_dets, FRAME_TIMESTAMP, rt_state.enemy_color);
      const auto TRACK_TARGET = predictor.GetTrackTarget();

      // ── Step 7: 开火决策 ──────────────────────────────────────────────────
      ctrl.fire = voter.Vote(TRACK_TARGET, ctrl);

      // MetricsTracker 统计
      metrics.Tick(!dets.empty(), static_cast<int>(dets.size()));

      // ========================================================================
      // Module 5: 串口下发 + Foxglove 调试输出 + TerminalHUD
      // ========================================================================

      // ── Step 8: 串口下发（通过 RmShooter 打包帧并发送）──────────────────
      if (serial.IsOpen()) {
        if (!shooter.Send(serial, ctrl)) {
          MV_LOG_WARN("real-debug", "串口发送失败（第 {} 帧）", frame_idx);
        }
      }

      // ── Step 9: Foxglove 调试推送（节流：每 fox_interval 帧推一次）───────
      const int64_t TIMESTAMP_NS =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());

      const bool SHOULD_PUBLISH_FOXGLOVE =
          sink.HasClients() && (frame_idx - last_fox_frame >= static_cast<uint64_t>(fox_interval));
      if (SHOULD_PUBLISH_FOXGLOVE) {
        last_fox_frame = frame_idx;

        // 动态调整节流间隔（保持 Foxglove 端约 kFoxTargetFps 帧率）
        const double CURRENT_FPS = metrics.CurrentFps();
        if (CURRENT_FPS > 1.0) {
          fox_interval = std::max(1, static_cast<int>(CURRENT_FPS / FOX_TARGET_FPS));
        }

        sink.PublishImage(frame, "camera/raw", "camera", TIMESTAMP_NS);
        sink.PublishDetections(dets, TIMESTAMP_NS);
        sink.PublishPnpResult(solved_dets, frame, TIMESTAMP_NS);
        sink.PublishGimbalControl(ctrl, TIMESTAMP_NS);
        sink.PublishTrackingVisuals(
            TRACK_TARGET,
            ctrl.tracking ? Eigen::Vector3d{ctrl.distance, 0.0, 0.0} : Eigen::Vector3d::Zero(),
            "world", TIMESTAMP_NS);

        // 追踪与决策 JSON（Raw Message 面板）
        nlohmann::json track_json;
        track_json["is_tracking"] = TRACK_TARGET.is_tracking;
        track_json["tracker_state"] = TRACK_TARGET.tracker_state;
        track_json["yaw_predicted"] = TRACK_TARGET.yaw_predicted;
        track_json["pitch_predicted"] = TRACK_TARGET.pitch_predicted;
        track_json["velocity"] = {TRACK_TARGET.velocity.x(), TRACK_TARGET.velocity.y(),
                                  TRACK_TARGET.velocity.z()};
        sink.PublishJson("tracking/target_state", track_json, TIMESTAMP_NS);

        nlohmann::json voter_json;
        voter_json["fire"] = ctrl.fire;
        voter_json["tracking"] = ctrl.tracking;
        voter_json["yaw_rad"] = ctrl.yaw;
        voter_json["pitch_rad"] = ctrl.pitch;
        voter_json["distance_m"] = ctrl.distance;
        voter_json["serial_alive"] = rt_state.serial_alive;
        voter_json["bullet_speed"] = rt_state.bullet_speed;
        voter_json["fps"] = CURRENT_FPS;
        sink.PublishJson("voter/decision", voter_json, TIMESTAMP_NS);

        // 串口上行帧可视化（三路 Topic：rx_status / rx_raw_hex / stats）
        serial_viz.Publish(sink, TIMESTAMP_NS);

        // 线程健康：单进程单线程模式下只有一个"MainLoop"节点
        mv::tool::FoxgloveSink::ThreadMetrics main_metrics;
        main_metrics.node_name = "MainLoop";
        main_metrics.fps = CURRENT_FPS;
        main_metrics.latency_ms = (CURRENT_FPS > 1.0) ? (1000.0 / CURRENT_FPS) : 0.0;
        main_metrics.is_alive = true;
        sink.PublishThreadMetrics({main_metrics}, TIMESTAMP_NS);
      }

      // ── Step 10: TerminalHUD（按 200ms 自动节流）──────────────────────────
      {
        mv::tool::TerminalHUD::NodeMetrics node_metrics;
        node_metrics.node_name = "Loop";
        node_metrics.fps = metrics.CurrentFps();
        node_metrics.is_alive = true;
        node_metrics.error_msg = rt_state.serial_alive ? "" : "串口无数据";
        std::vector<mv::tool::TerminalHUD::NodeMetrics> node_metrics_list{node_metrics};
        hud.Update(metrics.CurrentFps(), dets, &ctrl, &node_metrics_list);
      }

    }  // while (!QuitFlag)

    metrics.PrintStats();
    MV_LOG_INFO("real-debug", "正常退出");

  } catch (const std::exception& e) {
    std::cerr << "[real-debug-vision] 异常退出: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
// NOLINTEND(readability-function-cognitive-complexity,readability-function-size,readability-identifier-naming)
