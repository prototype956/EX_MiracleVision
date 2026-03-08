/**
 * @file foxglove_vision_test.cpp
 * @brief Foxglove 模块端到端测试（视频/相机 → 装甲板识别 → Foxglove 可视化）
 *
 * 【功能】
 *   验证 FoxgloveSink 所有 Publish* 接口与 TerminalHUD 在真实视觉数据下的行为：
 *   - camera/raw          — 原始图像帧（仅有客户端连接时编码）
 *   - detections/annotations + detections/3d — 装甲板 2D/3D 检测框
 *   - pnp/debug_image + pnp/axes_3d + pnp/residuals — PnP 三层调试
 *   - control/gimbal      — 云台控制指令 JSON
 *   - /tf                 — world → gimbal 坐标系变换
 *   - pipeline/nodes      — 单节点线程健康 JSON
 *
 * 【用法】
 * @code
 *   mv-foxglove-vision-test [video_or_camera] [red|blue] [port]
 *   mv-foxglove-vision-test                         # 摄像头 0，红方，端口 8765
 *   mv-foxglove-vision-test armor.mp4               # 视频文件
 *   mv-foxglove-vision-test armor.mp4 blue          # 视频文件，识别蓝方
 *   mv-foxglove-vision-test 0 red 9090              # 摄像头，自定义端口
 * @endcode
 *
 * 【Foxglove Studio 接入】
 *   打开 Foxglove Studio → 新建连接 → WebSocket → ws://NUC_IP:8765
 *
 * 【赛场无网条件】
 *   即使不连接 Foxglove Studio，程序仍正常运行；
 *   TerminalHUD 以 200 ms 间隔在终端刷新状态行，提供零开销本地调试。
 *
 * 【按键（终端聚焦时）】
 *   q / Ctrl+C → 退出
 *   c          → 切换识别颜色（红/蓝）
 *   l          → 切换视频循环（仅视频文件有效）
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "interfaces/types.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "modules/simple_predictor/simple_predictor.hpp"
#include "tool/debug/terminal_hud.hpp"
#include "tool/foxglove/foxglove_sink.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

// ── 全局退出标志（Ctrl+C 处理）──────────────────────────────────────────────

namespace {

std::atomic<bool> g_quit{false};

extern "C" void SigIntHandler(int /*sig*/) {
  g_quit.store(true, std::memory_order_relaxed);
}

// ── 解析命令行 ─────────────────────────────────────────────────────────────

struct Args {
  YAML::Node cam_cfg;
  bool is_file_source{false};
  mv::ArmorColor enemy_color{mv::ArmorColor::RED};
  uint16_t foxglove_port{8765};
};

Args ParseArgs(int argc, char** argv) {
  Args args;

  // 参数 1：视频路径 or 摄像头索引
  if (argc > 1) {
    const std::string src(argv[1]);
    const bool is_index = !src.empty() && std::all_of(src.begin(), src.end(), [](unsigned char c) {
      return std::isdigit(c) != 0;
    });
    if (is_index) {
      args.cam_cfg["source"] = std::stoi(src);
    } else {
      args.cam_cfg["source"] = src;
      args.is_file_source    = true;
    }
  } else {
    args.cam_cfg["source"] = 0;
  }

  // 参数 2：颜色
  if (argc > 2) {
    const std::string color(argv[2]);
    args.enemy_color =
        (color == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
  }

  // 参数 3：端口
  if (argc > 3) {
    args.foxglove_port = static_cast<uint16_t>(std::stoi(argv[3]));
  }

  return args;
}

// ── 构造 world→gimbal 变换矩阵 ──────────────────────────────────────────────

/**
 * @brief 根据云台指令中的 yaw/pitch 角构造 4×4 齐次变换矩阵。
 *
 * 模型：gimbal 相对 world 偏转。
 *   R = Rz(yaw) * Ry(-pitch)  （右手系，Z 向上）
 * 注意：这里只是用于可视化调试，不代表真实物理安装关系。
 */
Eigen::Matrix4d GimbalTransform(double yaw_rad, double pitch_rad) {
  Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
  T.block<3, 3>(0, 0) =
      (Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
       Eigen::AngleAxisd(-pitch_rad, Eigen::Vector3d::UnitY()))
          .toRotationMatrix();
  return T;
}

// ── FPS 滑动窗口 ──────────────────────────────────────────────────────────────

class FpsTracker {
 public:
  explicit FpsTracker(int window = 30) : window_{window} {}

  void Tick() noexcept {
    ++count_;
    if (count_ >= window_) {
      const auto now   = std::chrono::steady_clock::now();
      const double dt  = std::chrono::duration<double>(now - last_).count();
      fps_             = (dt > 0.0) ? static_cast<double>(count_) / dt : 0.0;
      last_            = now;
      count_           = 0;
    }
  }

  [[nodiscard]] double Fps() const noexcept { return fps_; }

 private:
  int window_;
  int count_{0};
  double fps_{0.0};
  std::chrono::steady_clock::time_point last_{std::chrono::steady_clock::now()};
};

}  // namespace

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  std::signal(SIGINT, SigIntHandler);

  try {
    // ── 1. 解析参数 ───────────────────────────────────────────────────────────
    const Args ARGS = ParseArgs(argc, argv);

    // ── 2. 加载配置 + Logger ──────────────────────────────────────────────────
    auto& cfg = mv::ConfigManager::Instance();
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
    mv::Logger::Instance().Init("logs", spdlog::level::info, true);
    MV_LOG_INFO("foxglove-test", "══════════ mv-foxglove-vision-test 启动 ══════════");
    MV_LOG_INFO("foxglove-test", "Foxglove WebSocket 端口: {}", ARGS.foxglove_port);

    // ── 3. 初始化 Foxglove + TerminalHUD ─────────────────────────────────────
    mv::tool::FoxgloveSinkConfig fox_cfg;
    fox_cfg.port = ARGS.foxglove_port;
    mv::tool::FoxgloveSink sink{fox_cfg};
    sink.Start();
    MV_LOG_INFO("foxglove-test", "Foxglove 服务已启动 (ws://0.0.0.0:{})", ARGS.foxglove_port);

    mv::tool::TerminalHUD hud;

    // ── 4. 初始化算法模块 ─────────────────────────────────────────────────────
    const YAML::Node ROOT_CFG = cfg.Subtree();

    mv::modules::BasicArmorDetector detector;
    mv::modules::PnpSolver solver;
    mv::modules::SimplePredictor predictor;

    if (!detector.Init(ROOT_CFG) || !solver.Init(ROOT_CFG) || !predictor.Init(ROOT_CFG)) {
      MV_LOG_ERROR("foxglove-test", "模块初始化失败，退出");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("foxglove-test", "所有算法模块初始化成功");

    // ── 5. 打开图像源 ─────────────────────────────────────────────────────────
    mv::hal::OpenCvCamera camera;
    if (!camera.Open(ARGS.cam_cfg)) {
      MV_LOG_ERROR("foxglove-test", "无法打开图像源");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("foxglove-test", "图像源打开成功");

    // ── 6. 运行时状态 ─────────────────────────────────────────────────────────
    mv::ArmorColor enemy_color = ARGS.enemy_color;
    bool loop_video             = ARGS.is_file_source;
    FpsTracker fps_tracker{30};

    MV_LOG_INFO("foxglove-test",
                "识别颜色: {}，视频循环: {}",
                (enemy_color == mv::ArmorColor::RED) ? "RED" : "BLUE",
                loop_video ? "ON" : "OFF");

    std::cout << "\n[foxglove-test] 提示：\n"
              << "  Foxglove Studio 连接 → ws://localhost:" << ARGS.foxglove_port << "\n"
              << "  按 Ctrl+C 退出\n\n";

    // ── 7. 主循环 ─────────────────────────────────────────────────────────────
    cv::Mat frame;
    uint64_t frame_idx = 0;

    while (!g_quit.load(std::memory_order_relaxed)) {
      // 采帧
      if (!camera.Grab(frame) || frame.empty()) {
        if (loop_video && ARGS.is_file_source) {
          camera.Close();
          if (!camera.Open(ARGS.cam_cfg)) {
            MV_LOG_ERROR("foxglove-test", "视频重新打开失败");
            break;
          }
          predictor.Init(ROOT_CFG);
          continue;
        }
        MV_LOG_INFO("foxglove-test", "视频结束或相机断开");
        break;
      }

      ++frame_idx;
      fps_tracker.Tick();

      // 计时戳（纳秒）
      const int64_t ts_ns = static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count());

      // ── 检测 + 解算 + 预测 ────────────────────────────────────────────────
      const auto T_FRAME = std::chrono::steady_clock::now();
      auto detections    = detector.Detect(frame, enemy_color);
      for (auto& det : detections) {
        solver.Solve(det);
      }
      const mv::GimbalControl CTRL = predictor.Predict(detections, T_FRAME, enemy_color);

      // ── Foxglove 发布 ─────────────────────────────────────────────────────

      // 1. 原始图像（零客户端时内部自动跳过编码）
      sink.PublishImage(frame, "camera/raw", "camera", ts_ns);

      // 2. 外部门控示例：只有有人连接时才做昂贵的调试绘图
      if (sink.HasClients()) {
        cv::Mat dbg_frame = frame.clone();
        // 叠加轻量标注（不调用 DebugSession，直接在帧上标注检测框数量）
        cv::putText(dbg_frame,
                    "DET:" + std::to_string(detections.size()) + "  FPS:" +
                        std::to_string(static_cast<int>(fps_tracker.Fps())),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.9,
                    cv::Scalar(0, 255, 0), 2);
        sink.PublishImage(dbg_frame, "camera/annotated", "camera", ts_ns);
      }

      // 3. 装甲板 2D/3D 检测结果
      sink.PublishDetections(detections, ts_ns);

      // 4. PnP 三层调试（零客户端时内部自动跳过图像编码）
      sink.PublishPnpResult(detections, frame, ts_ns);

      // 5. 云台控制指令
      sink.PublishGimbalControl(CTRL, ts_ns);

      // 6. world → gimbal TF（以云台 yaw/pitch 角驱动）
      sink.PublishTransform("world", "gimbal",
                            GimbalTransform(CTRL.yaw, CTRL.pitch), ts_ns);

      // 7. 线程健康（单线程模式：用实测 FPS 伪造单节点上报）
      mv::tool::TerminalHUD::NodeMetrics node_metric;
      node_metric.node_name  = "VideoDetect";
      node_metric.fps        = fps_tracker.Fps();
      node_metric.latency_ms = (frame_idx > 0) ? (1000.0 / std::max(fps_tracker.Fps(), 1.0)) : 0.0;
      node_metric.is_alive   = true;

      // FoxgloveSink::ThreadMetrics 与 TerminalHUD::NodeMetrics 字段一一对应
      mv::tool::FoxgloveSink::ThreadMetrics thread_metric;
      thread_metric.node_name  = node_metric.node_name;
      thread_metric.fps        = node_metric.fps;
      thread_metric.latency_ms = node_metric.latency_ms;
      thread_metric.is_alive   = true;
      sink.PublishThreadMetrics({thread_metric}, ts_ns);

      // ── TerminalHUD 刷新（200 ms 速率限制，零连接时的主要调试手段）──────
      std::vector<mv::tool::TerminalHUD::NodeMetrics> nm{node_metric};
      hud.Update(fps_tracker.Fps(), detections, &CTRL, &nm);
    }

    // ── 8. 清理 ───────────────────────────────────────────────────────────────
    hud.Flush();
    camera.Close();
    sink.Stop();
    MV_LOG_INFO("foxglove-test", "共处理 {} 帧", frame_idx);
    MV_LOG_INFO("foxglove-test", "══════════ mv-foxglove-vision-test 结束 ══════════");

  } catch (const std::exception& exc) {
    std::cerr << "[foxglove-test] FATAL: " << exc.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
