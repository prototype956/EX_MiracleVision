/**
 * @file main.cpp
 * @brief MiracleVision 主入口 —— Stage 6
 *
 * 【启动流程】
 *   1. 加载 configs/vision.yaml（ConfigManager）
 *   2. 初始化 Logger（spdlog，写文件 + 控制台）
 *   3. 注册信号处理器（SIGINT/SIGTERM → 优雅退出，SIGUSR1 → 热重载）
 *   4. 创建 HAL 对象（相机 + 串口），按配置选择后端，打开硬件
 *   5. 创建并 Init 算法模块（Detector / Solver / Predictor / Voter / Shooter）
 *   6. 用 VisionPipeline::Builder 组装流水线（所有权移交 Pipeline）
 *   7. 用 VisionFSM 包装 Pipeline，调用 Start()
 *   8. 主循环：fsm.Update() + sleep，直到收到停止信号或 FSM 进入非运行态
 *   9. fsm.Stop() + 优雅退出
 *
 * 【相机后端选择（vision.yaml camera.backend）】
 *   "mindvision" → MindVisionCamera（工业相机，需要 SDK）
 *   "opencv"     → OpenCvCamera（USB 摄像头 / 视频文件 / RTSP）
 *   其他值       → 回退到 OpenCvCamera，发出警告
 *
 * 【信号说明】
 *   SIGINT / SIGTERM → g_stop = true → 主循环退出
 *   SIGUSR1          → g_reload = true → 主循环内调用 ConfigManager::Reload()
 *
 * 【已知限制（Stage 6 技术债）】
 *   - SerialNode::TryRecv() 仍为 5 字节占位协议，需与下位机确认后替换
 *   - SimplePredictor 无 EKF，yaw/pitch 为直通值（无预判延迟补偿）
 *   - Factory<>注册已完成，但 main.cpp 直接实例化以避免链接器 dead-strip 问题
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "fsm/vision_fsm.hpp"
#include "hal/camera/mindvision_camera.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "hal/serial/uart_serial.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "modules/rm_shooter/rm_shooter.hpp"
#include "modules/simple_predictor/simple_predictor.hpp"
#include "modules/simple_voter/simple_voter.hpp"
#include "pipeline/pipeline.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

// ── 全局信号标志（仅由信号处理器写，主线程读）─────────────────────────────
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<bool> g_stop{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<bool> g_reload{false};

extern "C" {
static void HandleStop(int /*sig*/) noexcept {
  g_stop.store(true, std::memory_order_release);
}
static void HandleReload(int /*sig*/) noexcept {
  g_reload.store(true, std::memory_order_release);
}
}  // extern "C"

// ── 辅助：字符串 → spdlog 日志等级 ────────────────────────────────────────
static spdlog::level::level_enum ParseLogLevel(const std::string& str) {
  if (str == "trace")
    return spdlog::level::trace;
  if (str == "debug")
    return spdlog::level::debug;
  if (str == "warn")
    return spdlog::level::warn;
  if (str == "error")
    return spdlog::level::err;
  if (str == "critical")
    return spdlog::level::critical;
  return spdlog::level::info;
}

// ── 辅助：解析 "WxH" 分辨率字符串 ─────────────────────────────────────────
static void ParseResolution(const std::string& res, int& width, int& height) {
  if (std::sscanf(res.c_str(), "%dx%d", &width, &height) != 2) {
    width = 1280;
    height = 1024;
  }
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
  // ── 1. 配置文件路径（可通过第一个命令行参数覆盖）───────────────────────
  const std::string CONFIG_PATH = (argc > 1) ? argv[1] : (CONFIG_FILE_PATH "/vision.yaml");

  // ── 2. 加载配置 ────────────────────────────────────────────────────────
  auto& cfg = mv::ConfigManager::Instance();
  try {
    cfg.Load(CONFIG_PATH);
  } catch (const std::exception& exc) {
    std::fprintf(stderr, "[main] FATAL: failed to load config '%s': %s\n", CONFIG_PATH.c_str(),
                 exc.what());
    return EXIT_FAILURE;
  }

  // ── 3. 初始化 Logger ────────────────────────────────────────────────────
  const std::string LOG_DIR = cfg.Get<std::string>("system.log_dir", "logs");
  const std::string LOG_LEVEL = cfg.Get<std::string>("system.log_level", "info");
  const bool LOG_CONSOLE = cfg.Get<bool>("system.log_console", true);

  mv::Logger::Instance().Init(LOG_DIR, ParseLogLevel(LOG_LEVEL), LOG_CONSOLE);
  MV_LOG_INFO("main", "══════════ MiracleVision Starting ══════════");
  MV_LOG_INFO("main", "Config : {}", CONFIG_PATH);
  MV_LOG_INFO("main", "LogLevel: {}", LOG_LEVEL);

  // ── 4. 注册信号处理器 ────────────────────────────────────────────────────
  std::signal(SIGINT, HandleStop);
  std::signal(SIGTERM, HandleStop);
  std::signal(SIGUSR1, HandleReload);
  MV_LOG_INFO("main", "Signal handlers registered (SIGINT/SIGTERM=stop, SIGUSR1=reload)");

  // ── 5. 创建并打开相机 ────────────────────────────────────────────────────
  const std::string BACKEND = cfg.Get<std::string>("camera.backend", "opencv");
  std::unique_ptr<mv::hal::ICamera> camera;

  if (BACKEND == "mindvision") {
    MV_LOG_INFO("main", "Camera backend: MindVision");
    camera = std::make_unique<mv::hal::MindVisionCamera>();

    int cam_w = 1280;
    int cam_h = 1024;
    ParseResolution(cfg.Get<std::string>("camera.resolution", "1280x1024"), cam_w, cam_h);

    YAML::Node cam_cfg;
    cam_cfg["exposure_us"] = cfg.Get<int>("camera.exposure", 2500);
    cam_cfg["resolution"]["width"] = cam_w;
    cam_cfg["resolution"]["height"] = cam_h;

    if (!camera->Open(cam_cfg)) {
      MV_LOG_WARN("main", "MindVisionCamera::Open() failed — falling back to OpenCvCamera");
      camera = std::make_unique<mv::hal::OpenCvCamera>();
      YAML::Node fallback;
      fallback["source"] = cfg.Get<int>("camera.device_index", 0);
      if (!camera->Open(fallback)) {
        MV_LOG_ERROR("main", "OpenCvCamera fallback also failed — cannot continue");
        return EXIT_FAILURE;
      }
    }
  } else {
    if (BACKEND != "opencv") {
      MV_LOG_WARN("main", "Unknown camera backend '{}', using opencv", BACKEND);
    }
    MV_LOG_INFO("main", "Camera backend: OpenCV");
    camera = std::make_unique<mv::hal::OpenCvCamera>();

    YAML::Node cam_cfg;
    // 支持视频文件路径或设备索引
    const std::string VIDEO_PATH = cfg.Get<std::string>("camera.video_path", "");
    if (!VIDEO_PATH.empty()) {
      cam_cfg["source"] = VIDEO_PATH;
    } else {
      cam_cfg["source"] = cfg.Get<int>("camera.device_index", 0);
    }

    if (!camera->Open(cam_cfg)) {
      MV_LOG_ERROR(
          "main", "OpenCvCamera::Open() failed for index/path '{}'",
          VIDEO_PATH.empty() ? std::to_string(cfg.Get<int>("camera.device_index", 0)) : VIDEO_PATH);
      return EXIT_FAILURE;
    }
  }
  MV_LOG_INFO("main", "Camera opened successfully");

  // ── 6. 创建并打开串口 ────────────────────────────────────────────────────
  auto serial = std::make_unique<mv::hal::UartSerial>();
  {
    YAML::Node serial_cfg;
    serial_cfg["device"] = cfg.Get<std::string>("serial.device", "/dev/ttyUSB0");
    serial_cfg["baudrate"] = cfg.Get<int>("serial.baudrate", 115200);

    if (!serial->Open(serial_cfg)) {
      MV_LOG_WARN("main", "UartSerial::Open() failed on '{}' — running without serial (debug mode)",
                  serial_cfg["device"].as<std::string>());
      // 不退出：无串口时 RmShooter::Send() 会静默失败，SerialNode 会累积 fail_count
    } else {
      MV_LOG_INFO("main", "Serial opened on {}", serial_cfg["device"].as<std::string>());
    }
  }

  // ── 7. 创建并 Init 算法模块 ────────────────────────────────────────────
  //   将整棵配置树传给各 Init()，模块自行查找它所需的子节点。
  const YAML::Node ROOT_CFG = cfg.Subtree();

  auto detector = std::make_unique<mv::modules::BasicArmorDetector>();
  auto solver = std::make_unique<mv::modules::PnpSolver>();
  auto predictor = std::make_unique<mv::modules::SimplePredictor>();
  auto voter = std::make_unique<mv::modules::SimpleVoter>();
  auto shooter = std::make_unique<mv::modules::RmShooter>();

  if (!detector->Init(ROOT_CFG)) {
    MV_LOG_ERROR("main", "BasicArmorDetector::Init() failed");
    return EXIT_FAILURE;
  }
  if (!solver->Init(ROOT_CFG)) {
    MV_LOG_ERROR("main", "PnpSolver::Init() failed");
    return EXIT_FAILURE;
  }
  if (!predictor->Init(ROOT_CFG)) {
    MV_LOG_ERROR("main", "SimplePredictor::Init() failed");
    return EXIT_FAILURE;
  }
  if (!voter->Init(ROOT_CFG)) {
    MV_LOG_ERROR("main", "SimpleVoter::Init() failed");
    return EXIT_FAILURE;
  }
  if (!shooter->Init(ROOT_CFG)) {
    MV_LOG_ERROR("main", "RmShooter::Init() failed");
    return EXIT_FAILURE;
  }
  MV_LOG_INFO("main", "All modules initialized");

  // ── 8. 构建 Pipeline ────────────────────────────────────────────────────
  std::unique_ptr<mv::pipeline::VisionPipeline> pipeline;
  try {
    pipeline = mv::pipeline::VisionPipeline::Builder{}
                   .Camera(std::move(camera))
                   .Detector(std::move(detector))
                   .Solver(std::move(solver))
                   .Predictor(std::move(predictor))
                   .Voter(std::move(voter))
                   .Serial(std::move(serial))
                   .Shooter(std::move(shooter))
                   .Build();
  } catch (const std::exception& exc) {
    MV_LOG_ERROR("main", "VisionPipeline::Builder::Build() failed: {}", exc.what());
    return EXIT_FAILURE;
  }
  MV_LOG_INFO("main", "VisionPipeline built");

  // ── 9. 初始化 FSM 并启动 ────────────────────────────────────────────────
  mv::fsm::VisionFSM fsm(std::move(pipeline));
  fsm.Start();
  MV_LOG_INFO("main", "VisionFSM started — entering main loop");

  // ── 10. 主循环 ────────────────────────────────────────────────────────────
  using namespace std::chrono_literals;
  static constexpr auto LOOP_INTERVAL = 10ms;

  while (!g_stop.load(std::memory_order_acquire)) {
    // SIGUSR1 热重载
    if (g_reload.exchange(false, std::memory_order_acq_rel)) {
      MV_LOG_INFO("main", "SIGUSR1 received — reloading config");
      try {
        cfg.Reload();
        MV_LOG_INFO("main", "Config reloaded successfully");
      } catch (const std::exception& exc) {
        MV_LOG_WARN("main", "Config reload failed: {} (keeping previous config)", exc.what());
      }
    }

    fsm.Update();

    // FSM 进入 IDLE（Stop() 被调用）或发生不可恢复错误时退出循环
    if (!fsm.IsRunning() && fsm.CurrentState() == mv::fsm::SystemState::IDLE) {
      MV_LOG_INFO("main", "FSM transitioned to IDLE — exiting loop");
      break;
    }

    std::this_thread::sleep_for(LOOP_INTERVAL);
  }

  // ── 11. 优雅退出 ──────────────────────────────────────────────────────────
  MV_LOG_INFO("main", "Stopping VisionFSM...");
  fsm.Stop();

  MV_LOG_INFO("main", "══════════ MiracleVision Stopped ══════════");
  return EXIT_SUCCESS;
}
