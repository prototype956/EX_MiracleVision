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
 *   运行参数统一从基础配置 + 可选覆盖配置合并得到：
 *   - 默认基础配置：configs/vision.yaml
 *   - 默认调试覆盖：configs/debug/debug_override.yaml（若存在则自动叠加）
 *   - 也可通过 --config <yaml> 指定当前车/兵种的配置文件
 *
 *   程序退出时，会把当前热调后的持久化参数回写到本次实际使用的目标 YAML。
 *
 *   合并后的字段来源包括：
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
#include "tool/debug/pnp_param_manager.hpp"
#include "tool/debug/shooter_param_manager.hpp"
#include "tool/debug/terminal_hud.hpp"
#include "tool/foxglove/foxglove_sink.hpp"
// PredictParamManager 复用离线调试的参数管理（双向 Foxglove 同步）
#include "tool/debug/predict_param_manager.hpp"
// ArmorDetectorParamManager 装甲检测器参数热调管理
#include "tool/debug/armor_param_manager.hpp"
#include "tool/foxglove/detail/serial_visualizer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

struct CliArgs {
  std::string base_config_path{CONFIG_FILE_PATH "/vision.yaml"};
  std::string profile_config_path;
  std::string save_target_path{CONFIG_FILE_PATH "/vision.yaml"};
};

void MergeYamlNode(YAML::Node& dest, const YAML::Node& from) {
  if (!from || from.IsNull()) {
    return;
  }
  if (!from.IsMap()) {
    dest = YAML::Clone(from);
    return;
  }
  if (!dest || !dest.IsMap()) {
    dest = YAML::Node(YAML::NodeType::Map);
  }

  for (const auto& item : from) {
    const auto KEY = item.first.as<std::string>();
    if (dest[KEY] && dest[KEY].IsMap() && item.second.IsMap()) {
      YAML::Node child = dest[KEY];
      MergeYamlNode(child, item.second);
    } else {
      dest[KEY] = YAML::Clone(item.second);
    }
  }
}

CliArgs ParseCliArgs(int argc, char** argv) {
  CliArgs args;
  const auto DEFAULT_PROFILE = std::string(CONFIG_FILE_PATH) + "/debug/debug_override.yaml";
  if (std::filesystem::exists(DEFAULT_PROFILE)) {
    args.profile_config_path = DEFAULT_PROFILE;
    args.save_target_path = DEFAULT_PROFILE;
  }

  bool has_profile_arg = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--config 需要跟一个 YAML 文件路径");
      }
      args.profile_config_path = argv[++i];
      args.save_target_path = args.profile_config_path;
      has_profile_arg = true;
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      throw std::invalid_argument(
          "用法: mv-real-debug-vision [--config <yaml>]\n"
          "  默认基础配置: src/config/vision.yaml\n"
          "  默认调试覆盖: src/config/debug/debug_override.yaml（若存在则自动叠加）\n"
          "  --config 可指定当前车/兵种 YAML，程序退出时会回写到该文件");
    }
    if (!has_profile_arg) {
      args.profile_config_path = arg;
      args.save_target_path = arg;
      has_profile_arg = true;
      continue;
    }
    throw std::invalid_argument("仅支持一个位置参数 YAML 文件，或使用 --config <yaml>");
  }

  if (args.profile_config_path == args.base_config_path) {
    args.profile_config_path.clear();
    args.save_target_path = args.base_config_path;
  }
  return args;
}

YAML::Node LoadMergedConfigRoot(const CliArgs& args) {
  YAML::Node root = YAML::LoadFile(args.base_config_path);
  if (!args.profile_config_path.empty()) {
    MergeYamlNode(root, YAML::LoadFile(args.profile_config_path));
  }
  return root;
}

bool SaveMergedConfigRoot(const YAML::Node& root, const std::string& yaml_path) {
  std::filesystem::path output_path(yaml_path);
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path());
  }

  std::ofstream ofs(yaml_path);
  if (!ofs.is_open()) {
    return false;
  }
  ofs << root;
  return true;
}

// ── 弧度转角度常数（常量名符合 Google Style Guide）─────────────────────
constexpr double RAD_TO_DEG = 57.295779513082323;

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

  const uint8_t NORMALIZED_COLOR = mv::protocol::NormalizeUpColor(up_frame.color);
  if (NORMALIZED_COLOR == mv::protocol::UP_COLOR_RED) {
    runtime_state.enemy_color = mv::ArmorColor::BLUE;
  } else if (NORMALIZED_COLOR == mv::protocol::UP_COLOR_BLUE) {
    runtime_state.enemy_color = mv::ArmorColor::RED;
  }

  runtime_state.serial_alive = true;
}

}  // namespace

// ============================================================================
// Module 3: main() — 配置加载 + 所有对象创建 + Foxglove 初始化
// ============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity,readability-function-size,readability-identifier-naming)
// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
int main(int argc, char** argv) {
  std::signal(SIGINT, SigIntHandler);

  try {
    CliArgs cli_args = ParseCliArgs(argc, argv);

    // ── 日志初始化 ─────────────────────────────────────────────────────────
    // 控制台仅 info 及以上，避免 debug 消息滚动破坏 TerminalHUD 固定行；
    // 文件写入 trace 级别，保留完整记录供复盘。
    mv::Logger::Instance().Init("logs", spdlog::level::info, true);

    // ── 加载配置文件 ────────────────────────────────────────────────────────
    auto& cfg = mv::ConfigManager::Instance();
    cfg.Load(cli_args.base_config_path);
    if (!cli_args.profile_config_path.empty()) {
      cfg.Load(cli_args.profile_config_path);
    }

    const auto ENEMY_COLOR_NAME = cfg.Get<std::string>("auto_aim.enemy_color", "red");
    const mv::ArmorColor CONFIG_ENEMY_COLOR =
        (ENEMY_COLOR_NAME == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
    const uint16_t FOXGLOVE_PORT = static_cast<uint16_t>(cfg.Get<int>("debug.foxglove.port", 8765));

    MV_LOG_INFO("real-debug", "══════════ mv-real-debug-vision 启动 ══════════");
    MV_LOG_INFO("real-debug", "基础配置: {}", cli_args.base_config_path);
    if (!cli_args.profile_config_path.empty()) {
      MV_LOG_INFO("real-debug", "叠加配置: {}", cli_args.profile_config_path);
    }
    MV_LOG_INFO("real-debug", "退出回写目标: {}", cli_args.save_target_path);
    MV_LOG_INFO("real-debug", "Foxglove 端口: {}  初始颜色: {}", FOXGLOVE_PORT,
                CONFIG_ENEMY_COLOR == mv::ArmorColor::BLUE ? "BLUE" : "RED");

    // ── 参数管理器（复用 predict_voter_test 的双向 Foxglove 参数同步）───────
    mv::tool::PredictParamManager param_manager;
    param_manager.State().enemy_color = CONFIG_ENEMY_COLOR;

    YAML::Node root_cfg = cfg.Subtree();
    param_manager.InjectParamsToYaml(root_cfg);
    mv::tool::PnpParamManager pnp_param_mgr;
    pnp_param_mgr.LoadFromYaml(root_cfg);
    pnp_param_mgr.InjectParamsToYaml(root_cfg);
    mv::tool::ShooterParamManager shooter_param_mgr;
    shooter_param_mgr.LoadFromYaml(root_cfg);
    shooter_param_mgr.InjectParamsToYaml(root_cfg);

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
    detector.EnableDebug(true);

    // ── 装甲检测器参数管理器（参数热调 + 写回）──────────────────────────────────
    mv::tool::ArmorDetectorParamManager armor_param_mgr(&detector);

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

    // 统一参数分发：FoxgloveSink 仅支持单回调，按模块路由到各参数管理器。
    sink.SetParameterCallback(
        [&](const std::string& name, const nlohmann::json& /*raw*/) {
          const bool HANDLED_PREDICT = param_manager.HandleParameter(sink, name);
          const bool HANDLED_ARMOR = armor_param_mgr.HandleParameter(sink, name);
          const bool HANDLED_PNP = pnp_param_mgr.HandleParameter(sink, name);
          const bool HANDLED_SHOOTER = shooter_param_mgr.HandleParameter(sink, name);
          if (!HANDLED_PREDICT && !HANDLED_ARMOR && !HANDLED_PNP && !HANDLED_SHOOTER) {
            MV_LOG_DEBUG("real-debug", "未处理的 Foxglove 参数: {}", name);
          }
        });

    param_manager.PushToFoxglove(sink);
    armor_param_mgr.PushToFoxglove(sink);
    pnp_param_mgr.PushToFoxglove(sink);
    shooter_param_mgr.PushToFoxglove(sink);
    MV_LOG_INFO("real-debug", "检测器 / PnP / 预测器 / Voter / Shooter 参数热调已启用");

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

    // 跟踪失锁统计：用于区分检测链路问题与 EKF 重置问题
    std::unordered_map<std::string, uint64_t> lost_reason_counters;
    uint64_t lost_total_events = 0;
    uint64_t no_detection_frames = 0;
    uint64_t unsolved_only_frames = 0;
    uint64_t rejected_by_reproj_total = 0;
    std::string prev_tracker_state = "lost";

    // 弹速热更新节流（每 60 帧更新一次，避免频繁重初始化 RmShooter）
    float last_injected_speed = rt_state.bullet_speed;
    uint64_t last_speed_frame = 0;
    std::chrono::steady_clock::time_point last_serial_ok_time{};
    bool has_serial_ok_time = false;
    constexpr auto SERIAL_LIVE_TIMEOUT = std::chrono::milliseconds(120);

    std::cout << "\n[real-debug-vision] 已就绪：\n"
              << "  Foxglove Studio → ws://localhost:" << FOXGLOVE_PORT << "\n"
              << "  按 Ctrl+C 退出\n\n";

    // ========================================================================
    // Module 4: 主循环 — 串口接收 + 检测 + PnP + 预测 + 决策
    // ========================================================================

    cv::Mat frame;

    while (!QuitFlag().load(std::memory_order_relaxed)) {
      // ── Step 1: 参数热更新（Foxglove 端修改参数时触发）─────────────────────
      const bool REINIT_EKF = param_manager.ConsumeReinitEkf();
      const bool REINIT_VOTER = param_manager.ConsumeReinitVoter();
      if (REINIT_EKF || REINIT_VOTER) {
        param_manager.InjectParamsToYaml(root_cfg);
        if (REINIT_EKF) {
          predictor.Reset();
          roi_mgr.Reset();

          if (!predictor.Init(root_cfg)) {
            MV_LOG_WARN("real-debug", "EkfPredictor 重初始化失败，跳过本次");
            continue;
          }
        }

        if (REINIT_VOTER) {
          voter.Reset();
          if (!voter.Init(root_cfg)) {
            MV_LOG_WARN("real-debug", "CooldownVoter 重初始化失败，跳过本次");
            continue;
          }
        }

        MV_LOG_INFO("real-debug", "预测 / 决策参数已热更新 (ekf={} voter={})", REINIT_EKF,
                    REINIT_VOTER);
      }

      if (shooter_param_mgr.ConsumeReinit()) {
        shooter_param_mgr.InjectParamsToYaml(root_cfg);
        if (!shooter.Init(root_cfg)) {
          MV_LOG_WARN("real-debug", "RmShooter 重初始化失败，跳过本次");
          continue;
        }
        shooter_param_mgr.PushToFoxglove(sink);
        MV_LOG_INFO("real-debug", "Shooter 参数已热更新");
      }

      if (pnp_param_mgr.ConsumeReinit()) {
        pnp_param_mgr.InjectParamsToYaml(root_cfg);
        if (!solver.Init(root_cfg)) {
          MV_LOG_WARN("real-debug", "PnpSolver 重初始化失败，跳过本次");
          continue;
        }
        pnp_param_mgr.PushToFoxglove(sink);
        MV_LOG_INFO("real-debug", "PnpSolver 参数已热更新");
      }

      // 从参数管理器同步敌方颜色（Foxglove 端可修改）
      rt_state.enemy_color = param_manager.State().enemy_color;

      // ── Step 2: 串口接收上行帧（非阻塞）──────────────────────────────────
      if (serial.IsOpen()) {
        constexpr std::size_t RX_BUFFER_SIZE = 256;
        std::array<uint8_t, RX_BUFFER_SIZE> rx_buffer{};
        std::size_t received = 0;
        if (serial.Recv(rx_buffer.data(), rx_buffer.size(), received) && received > 0) {
          const auto FEED_RESULT = serial_viz.FeedBytes(rx_buffer.data(), received);
          if (FEED_RESULT.parsed_any) {
            UpdateRuntimeStateFromUpFrame(FEED_RESULT.latest_frame, rt_state);
            last_serial_ok_time = std::chrono::steady_clock::now();
            has_serial_ok_time = true;
            // 四元数注入 EKF（每帧必须在 Predict() 前调用）
            predictor.SetGimbalOrientation(rt_state.gimbal_quat);
          }
        }

        if (has_serial_ok_time) {
          const auto NOW = std::chrono::steady_clock::now();
          rt_state.serial_alive = (NOW - last_serial_ok_time) <= SERIAL_LIVE_TIMEOUT;
        } else {
          rt_state.serial_alive = false;
        }

        // 弹速热更新：每 60 帧（约 2s）同步一次，避免频繁重初始化
        const float BULLET_SPEED_DIFF = std::abs(rt_state.bullet_speed - last_injected_speed);
        if (frame_idx - last_speed_frame >= 60 && BULLET_SPEED_DIFF > 0.5F) {
          param_manager.State().ekf_bullet_speed = static_cast<double>(rt_state.bullet_speed);
          param_manager.InjectParamsToYaml(root_cfg);
          shooter_param_mgr.SetBulletSpeed(rt_state.bullet_speed, false);
          shooter_param_mgr.InjectParamsToYaml(root_cfg);
          if (shooter.Init(root_cfg)) {
            shooter_param_mgr.PushToFoxglove(sink);
            MV_LOG_INFO("real-debug", "弹速更新: {:.1f} m/s", rt_state.bullet_speed);
          }
          last_injected_speed = rt_state.bullet_speed;
          last_speed_frame = frame_idx;
        }
      }
      if (!serial.IsOpen()) {
        rt_state.serial_alive = false;
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
      // 过滤解算失败或重投影误差过大的检测，避免抖动角点污染 EKF。
      const auto PNP_PARAMS = pnp_param_mgr.GetParams();
      const double MAX_REPROJ_ERROR_PX = static_cast<double>(PNP_PARAMS.max_reproj_error_px);
      std::vector<mv::Detection> solved_dets;
      solved_dets.reserve(dets.size());
      int rejected_by_reproj_this_frame = 0;
      for (const auto& det : dets) {
        if (det.is_solved && det.reproj_error <= MAX_REPROJ_ERROR_PX) {
          solved_dets.push_back(det);
        } else if (det.is_solved) {
          ++rejected_by_reproj_this_frame;
        }
      }
      rejected_by_reproj_total += static_cast<uint64_t>(rejected_by_reproj_this_frame);

      // ── Step 6: EKF 预测 ──────────────────────────────────────────────────
      auto ctrl = predictor.Predict(solved_dets, FRAME_TIMESTAMP, rt_state.enemy_color);
      const auto TRACK_TARGET = predictor.GetTrackTarget();

      if (dets.empty()) {
        ++no_detection_frames;
      }
      if (!dets.empty() && solved_dets.empty()) {
        ++unsolved_only_frames;
      }
      if (TRACK_TARGET.tracker_state == "lost" && prev_tracker_state != "lost") {
        ++lost_total_events;
        const std::string LOST_REASON =
            TRACK_TARGET.tracker_lost_reason.empty() ? "unknown" : TRACK_TARGET.tracker_lost_reason;
        ++lost_reason_counters[LOST_REASON];
      }
      prev_tracker_state = TRACK_TARGET.tracker_state;

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
        const auto& detector_debug_data = detector.GetDebugData();
        const auto& detector_params = detector.GetParams();
        mv::tool::FoxgloveSink::TraditionalVisionLightVisParams light_vis_params;
        light_vis_params.min_light_ratio = detector_params.min_light_ratio;
        light_vis_params.max_light_ratio = detector_params.max_light_ratio;
        light_vis_params.max_light_angle = detector_params.max_light_angle;
        light_vis_params.min_area = detector_params.min_area;
        light_vis_params.stabilize_diff_binary =
          cfg.Get<bool>("debug.foxglove.stabilize_diff_binary", true);
        sink.PublishTraditionalVisionDebug(detector_debug_data.diff, detector_debug_data.binary,
                                           roi_mgr.GetRoiRect(), frame.size(), frame,
                                           light_vis_params, TIMESTAMP_NS);
        {
          const cv::Rect2i ROI_RECT = roi_mgr.GetRoiRect();
          nlohmann::json roi_status_json;
          roi_status_json["active"] = roi_mgr.IsActive();
          roi_status_json["lost_count"] = roi_mgr.GetLostCount();
          roi_status_json["x"] = ROI_RECT.x;
          roi_status_json["y"] = ROI_RECT.y;
          roi_status_json["width"] = ROI_RECT.width;
          roi_status_json["height"] = ROI_RECT.height;
          roi_status_json["area"] = ROI_RECT.area();
          sink.PublishJson("vision/debug/roi_status", roi_status_json, TIMESTAMP_NS);
        }
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
        track_json["tracker_lost_reason"] = TRACK_TARGET.tracker_lost_reason;
        track_json["yaw_predicted"] = TRACK_TARGET.yaw_predicted;
        track_json["pitch_predicted"] = TRACK_TARGET.pitch_predicted;
        track_json["velocity"] = {TRACK_TARGET.velocity.x(), TRACK_TARGET.velocity.y(),
                                  TRACK_TARGET.velocity.z()};
        sink.PublishJson("tracking/target_state", track_json, TIMESTAMP_NS);

        nlohmann::json lost_stats_json;
        lost_stats_json["tracker_state"] = TRACK_TARGET.tracker_state;
        lost_stats_json["last_lost_reason"] = TRACK_TARGET.tracker_lost_reason;
        lost_stats_json["lost_total_events"] = lost_total_events;
        lost_stats_json["no_detection_frames"] = no_detection_frames;
        lost_stats_json["unsolved_only_frames"] = unsolved_only_frames;
        lost_stats_json["rejected_by_reproj_this_frame"] = rejected_by_reproj_this_frame;
        lost_stats_json["rejected_by_reproj_total"] = rejected_by_reproj_total;
        lost_stats_json["max_reproj_error_px"] = MAX_REPROJ_ERROR_PX;
        nlohmann::json reason_counts = nlohmann::json::object();
        for (const auto& [reason, count] : lost_reason_counters) {
          reason_counts[reason] = count;
        }
        lost_stats_json["reason_counts"] = reason_counts;
        sink.PublishJson("tracking/lost_stats", lost_stats_json, TIMESTAMP_NS);

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

      // ── Step 11: Foxglove HUD 状态同步（按节流间隔推送）─────────────────────
      if (frame_idx % fox_interval == 0) {
        std::string enemy_color_display =
            (rt_state.enemy_color == mv::ArmorColor::RED)    ? "red"
            : (rt_state.enemy_color == mv::ArmorColor::BLUE) ? "blue"
                                                             : "none";

        sink.PublishHudStatus(metrics.CurrentFps(),           // fps
                              static_cast<int>(dets.size()),  // detection_count
                              ctrl.tracking,                  // tracking
                              rt_state.serial_alive,          // serial_alive
                              enemy_color_display,            // enemy_color
                              ctrl.yaw * RAD_TO_DEG,          // target_yaw_deg
                              ctrl.pitch * RAD_TO_DEG,        // target_pitch_deg
                              ctrl.distance,                  // target_distance_m
                              TIMESTAMP_NS                    // ts_ns
        );
      }

    }  // while (!QuitFlag)

    metrics.PrintStats();

    YAML::Node save_root = LoadMergedConfigRoot(cli_args);
    param_manager.InjectParamsToYaml(save_root);
    armor_param_mgr.InjectParamsToYaml(save_root);
    pnp_param_mgr.InjectParamsToYaml(save_root);
    shooter_param_mgr.InjectParamsToYaml(save_root);
    if (SaveMergedConfigRoot(save_root, cli_args.save_target_path)) {
      MV_LOG_INFO("real-debug", "已将热调参数写回配置文件: {}", cli_args.save_target_path);
    } else {
      MV_LOG_WARN("real-debug", "回写配置文件失败: {}", cli_args.save_target_path);
    }

    MV_LOG_INFO("real-debug", "正常退出");

  } catch (const std::exception& e) {
    std::cerr << "[real-debug-vision] 异常退出: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
// NOLINTEND(readability-function-cognitive-complexity,readability-function-size,readability-identifier-naming)
