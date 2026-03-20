/**
 * @file real_debug_vision.cpp
 * @brief 实机联调程序第一步：驱动相机并验证稳定取帧
 *
 * 【职责边界】
 *   仅负责相机链路联通验证：加载配置、打开相机、循环取帧与本地预览。
 *   不负责检测、解算、预测、决策与串口下发。
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/camera/i_camera.hpp"
#include "hal/camera/mindvision_camera.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "tool/debug/debug_session.hpp"
#include "tool/debug/metrics_tracker.hpp"
#include "tool/debug/terminal_hud.hpp"

#include <memory>
#include <string>

#include <opencv2/highgui.hpp>

int main() {
  //--------------------------- 初始化日志系统 ---------------------------
  mv::Logger::Instance().Init("logs");
  MV_LOG_INFO("real-debug-vision", "stage-1 启动：相机驱动验证");

  //--------------------------- 初始化配置文件系统 ---------------------------
  // yaml实例化配置管理器并加载配置文件
  auto& cfg = mv::ConfigManager::Instance();
  try {
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
  } catch (const std::exception& e) {
    MV_LOG_ERROR("real-debug-vision", "加载配置失败: {}", e.what());
    return EXIT_FAILURE;
  }

  //加载camera节点配置
  const YAML::Node ROOT_CFG = cfg.Subtree();  // 或直接用 cfg.Get<> 读字段
  const YAML::Node CAMERA_CFG = ROOT_CFG["camera"];

  const auto BACKEND = cfg.Get<std::string>("camera.backend", "mindvision");

  std::unique_ptr<mv::hal::ICamera> camera;
  camera = std::make_unique<mv::hal::MindVisionCamera>();

  if (!camera->Open(CAMERA_CFG)) {
    MV_LOG_ERROR("real-debug-vision", "相机打开失败，backend={}", BACKEND);
    return EXIT_FAILURE;
  }

  MV_LOG_INFO("real-debug-vision", "相机打开成功，按 q 退出预览");

  //--------------------------- 初始化debug工具 ---------------------------
  //构造帧级性能指标统计器
  mv::tool::MetricsTracker metrics;
  //构造终端HUD显示工具（默认 200ms 刷新一次）
  mv::tool::TerminalHUD hud;

  //--------------------------- 构造装甲检测器 -----------------------------
  mv::modules::BasicArmorDetector detector;
  if (!detector.Init(ROOT_CFG)) {
    MV_LOG_ERROR("real-debug-vision", "装甲检测器初始化失败");
    return EXIT_FAILURE;
  }
  auto params = detector.GetParams();

  cv::Mat frame;
  while (true) {
    if (!camera->Grab(frame) || frame.empty()) {
      MV_LOG_WARN("real-debug-vision", "Grab 失败或空帧，结束本次测试");
      break;
    }

    detector.SetParams(params);

    // 更新性能指标并刷新终端 HUD（暂时无检测结果和云台控制输出）
    metrics.Tick(true, 1);  // 本测试暂时未接入检测模块，默认每帧都有检测结果
    std::vector<mv::Detection> dets;  // 暂时空
    hud.Update(metrics.CurrentFps(), dets, nullptr, nullptr);

    cv::imshow("mv-real-debug-camera", frame);
    const int KEY = cv::waitKey(1);
    if (KEY == 'q' || KEY == 'Q' || KEY == 27) {
      break;
    }
  }

  //打印性能统计摘要
  metrics.PrintStats();

  camera->Close();
  cv::destroyAllWindows();
  MV_LOG_INFO("real-debug-vision", "stage-1 结束：相机链路正常退出");
  return EXIT_SUCCESS;
}
