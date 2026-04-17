/**
 * @file foxglove_vision_test.cpp
 * @brief Foxglove 模块端到端测试（视频/相机/仿真流 → 装甲板识别 → Foxglove 可视化）
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
 *   mv-foxglove-vision-test [video_or_camera_or_sim] [red|blue] [port]
 *   mv-foxglove-vision-test                         # 摄像头 0，红方，端口 8765
 *   mv-foxglove-vision-test armor.mp4               # 视频文件
 *   mv-foxglove-vision-test armor.mp4 blue          # 视频文件，识别蓝方
 *   mv-foxglove-vision-test 0 red 9090              # 摄像头，自定义端口
 *   mv-foxglove-vision-test sim blue 8765           # SimCamera（vision.yaml 中 sim_endpoint）
 *   mv-foxglove-vision-test sim:127.0.0.1:19090     # SimCamera（命令行覆盖 endpoint）
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
#include "hal/camera/i_camera.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "hal/camera/sim_camera.hpp"
#include "interfaces/types.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/armor_detector/roi_manager.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "modules/simple_predictor/simple_predictor.hpp"
#include "tool/debug/armor_param_manager.hpp"
#include "tool/debug/terminal_hud.hpp"
#include "tool/foxglove/foxglove_sink.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <Eigen/Dense>
#include <filesystem>
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
  bool use_sim_camera{false};
  std::string sim_endpoint_override;
  mv::ArmorColor enemy_color{mv::ArmorColor::RED};
  uint16_t foxglove_port{8765};
};

Args ParseArgs(int argc, char** argv) {
  Args args;

  // 参数 1：视频路径 / 摄像头索引 / sim[:host:port]
  if (argc > 1) {
    const std::string src(argv[1]);
    if (src == "sim") {
      args.use_sim_camera = true;
    } else if (src.rfind("sim:", 0) == 0) {
      args.use_sim_camera = true;
      args.sim_endpoint_override = src.substr(4);
    } else {
      const bool is_index =
          !src.empty() && std::all_of(src.begin(), src.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
          });
      if (is_index) {
        args.cam_cfg["source"] = std::stoi(src);
      } else {
        args.cam_cfg["source"] = src;
        args.is_file_source = true;
      }
    }
  } else {
    args.cam_cfg["source"] = 0;
  }

  // 参数 2：颜色
  if (argc > 2) {
    const std::string color(argv[2]);
    args.enemy_color = (color == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
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
 * 坐标系约定：
 *   world  帧：Z-up 右手系（Foxglove 3D 面板默认）
 *   gimbal 帧：Z-forward（OpenCV/PnP 惯例，Z=深度）
 *
 * 因此需要一个固定基底旋转 R_FIX（绕 X 轴 -90°）将 world Z-up
 * 对齐到 gimbal Z-forward，使 xyz_in_gimbal.z=5m 在 3D 面板中
 * 显示为"正前方 5 米"而非"正上方 5 米"。
 *
 *   R_total = Rz(yaw) * Ry(-pitch) * R_fix
 *
 * 注意：这里只是用于可视化调试，不代表真实物理安装关系。
 */
Eigen::Matrix4d GimbalTransform(double yaw_rad, double pitch_rad) {
  // 基底旋转：将 world Z-up 对齐到 gimbal Z-forward
  // 绕 X 轴 -90°：world(X=前,Y=左,Z=上) → gimbal(X=右,Y=上,Z=前)
  static const Eigen::Matrix3d R_FIX =
      Eigen::AngleAxisd(-M_PI / 2.0, Eigen::Vector3d::UnitX()).toRotationMatrix();

  Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
  T.block<3, 3>(0, 0) = (Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
                         Eigen::AngleAxisd(-pitch_rad, Eigen::Vector3d::UnitY()))
                            .toRotationMatrix() *
                        R_FIX;
  return T;
}

// ── 装甲板标签 ───────────────────────────────────────────────────────────────

inline std::string ArmorNumberStr(mv::ArmorNumber n) {
  switch (n) {
    case mv::ArmorNumber::ONE:
      return "1";
    case mv::ArmorNumber::TWO:
      return "2";
    case mv::ArmorNumber::THREE:
      return "3";
    case mv::ArmorNumber::FOUR:
      return "4";
    case mv::ArmorNumber::FIVE:
      return "5";
    case mv::ArmorNumber::SENTRY:
      return "S";
    case mv::ArmorNumber::OUTPOST:
      return "OP";
    case mv::ArmorNumber::BASE:
      return "BS";
    default:
      return "?";
  }
}

inline std::string MakeDetLabel(const mv::Detection& det) {
  const std::string prefix =
      ((det.color == mv::ArmorColor::RED) ? "R-" : "B-") + ArmorNumberStr(det.number);
  if (det.is_solved) {
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%s %.2fm err:%.1fpx", prefix.c_str(), det.xyz_in_gimbal.norm(),
                  det.reproj_error);
    return buf;
  }
  return prefix + " noPnP";
}

/**
 * @brief 绘制富调试图像（检测框 + PnP 重投影 + 状态栏）
 *
 * - 绿色多边形：检测到的角点
 * - 青色多边形：PnP 重投影角点（is_solved=true 时绘制，用于验证精度）
 * - 红色/蓝色中心点：目标颜色
 * - 标签文字：编号 + 距离 + 重投影误差
 * - 左上角状态栏：DET/SLV/FPS/CTRL 一行概览
 */
cv::Mat DrawAnnotated(const cv::Mat& frame, const std::vector<mv::Detection>& dets,
                      const mv::GimbalControl& ctrl, double fps) {
  cv::Mat img;
  if (frame.channels() == 1) {
    cv::cvtColor(frame, img, cv::COLOR_GRAY2BGR);
  } else {
    img = frame.clone();
  }

  int solved_count = 0;
  for (const auto& det : dets) {
    if (det.is_solved)
      ++solved_count;
  }

  for (const auto& det : dets) {
    const cv::Scalar BOX_CLR =
        (det.color == mv::ArmorColor::RED) ? cv::Scalar(0, 0, 220) : cv::Scalar(220, 80, 0);

    // ── 检测角点（绿色框）──────────────────────────────────────────────────
    std::vector<cv::Point> poly;
    poly.reserve(4);
    for (const auto& pt : det.points) {
      poly.emplace_back(static_cast<int>(pt.x), static_cast<int>(pt.y));
    }
    cv::polylines(img, poly, true, cv::Scalar(0, 215, 0), 2, cv::LINE_AA);

    // ── 重投影角点（青色框）—— 主 PnP 解（Z>0, 误差更小）────────────────────
    if (det.is_solved) {
      std::vector<cv::Point> rp;
      rp.reserve(4);
      for (const auto& pt : det.reprojected_points) {
        rp.emplace_back(static_cast<int>(pt.x), static_cast<int>(pt.y));
      }
      cv::polylines(img, rp, true, cv::Scalar(255, 220, 0), 1, cv::LINE_AA);
    }

    // ── 重投影角点（橙色框）—— IPPE 第二候选解，展示歧义性 ────────────────────
    if (det.has_alt_solution) {
      std::vector<cv::Point> ap;
      ap.reserve(4);
      for (const auto& pt : det.reprojected_points_alt) {
        ap.emplace_back(static_cast<int>(pt.x), static_cast<int>(pt.y));
      }
      cv::polylines(img, ap, true, cv::Scalar(0, 130, 255), 1, cv::LINE_AA);  // BGR 橙色
    }

    // ── 中心点 ────────────────────────────────────────────────────────────
    const cv::Point2f CTR = det.Center();
    cv::circle(img, cv::Point(static_cast<int>(CTR.x), static_cast<int>(CTR.y)), 5, BOX_CLR, -1);

    // ── 标签 ──────────────────────────────────────────────────────────────
    cv::putText(img, MakeDetLabel(det),
                cv::Point(static_cast<int>(CTR.x) + 6, static_cast<int>(CTR.y) - 12),
                cv::FONT_HERSHEY_SIMPLEX, 0.42, BOX_CLR, 1, cv::LINE_AA);
  }

  // ── 状态栏（左上角）──────────────────────────────────────────────────────
  {
    char buf[128];
    constexpr double RAD2DEG = 180.0 / 3.14159265;
    std::snprintf(buf, sizeof(buf),
                  "DET:%zu SLV:%d FPS:%.0f | ctrl yaw:%.1f\xC2\xB0 pit:%.1f\xC2\xB0 trk:%s",
                  dets.size(), solved_count, fps, ctrl.yaw * RAD2DEG, ctrl.pitch * RAD2DEG,
                  ctrl.tracking ? "Y" : "N");
    // 半透明背景条
    cv::rectangle(img, cv::Point(0, 0), cv::Point(img.cols, 32), cv::Scalar(0, 0, 0), cv::FILLED);
    cv::putText(img, buf, cv::Point(8, 22), cv::FONT_HERSHEY_SIMPLEX, 0.52, cv::Scalar(0, 240, 0),
                1, cv::LINE_AA);
  }

  return img;
}

// ── FPS 滑动窗口 ──────────────────────────────────────────────────────────────

class FpsTracker {
 public:
  explicit FpsTracker(int window = 30) : window_{window} {}

  void Tick() noexcept {
    ++count_;
    if (count_ >= window_) {
      const auto now = std::chrono::steady_clock::now();
      const double dt = std::chrono::duration<double>(now - last_).count();
      fps_ = (dt > 0.0) ? static_cast<double>(count_) / dt : 0.0;
      last_ = now;
      count_ = 0;
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

    // 应用 debug_override.yaml（由 mv-video-test 的 s 键保存）
    const std::string OVERRIDE_YAML = std::string(CONFIG_FILE_PATH) + "/debug/debug_override.yaml";
    if (std::filesystem::exists(OVERRIDE_YAML)) {
      try {
        const YAML::Node OV = YAML::LoadFile(OVERRIDE_YAML);
        if (OV && OV["detector"]) {
          detector.Init(OV);
          MV_LOG_INFO("foxglove-test", "已应用调试参数覆盖: {}", OVERRIDE_YAML);
        }
      } catch (const std::exception& e) {
        MV_LOG_WARN("foxglove-test", "读取调试覆盖文件失败（忽略）: {}", e.what());
      }
    }

    // ── 5. 打开图像源（OpenCV / SimCamera）──────────────────────────────────
    std::unique_ptr<mv::hal::ICamera> camera;
    if (ARGS.use_sim_camera) {
      camera = std::make_unique<mv::hal::SimCamera>();
      YAML::Node sim_cfg;
      sim_cfg["endpoint"] = ARGS.sim_endpoint_override.empty()
                                ? cfg.Get<std::string>("camera.sim_endpoint", "127.0.0.1:19090")
                                : ARGS.sim_endpoint_override;
      sim_cfg["connect_timeout_ms"] = cfg.Get<int>("camera.sim_connect_timeout_ms", 2000);
      sim_cfg["recv_timeout_ms"] = cfg.Get<int>("camera.sim_recv_timeout_ms", 500);
      sim_cfg["reconnect_interval_ms"] = cfg.Get<int>("camera.sim_reconnect_interval_ms", 200);
      sim_cfg["max_payload_bytes"] = cfg.Get<int>("camera.sim_max_payload_bytes", 8 * 1024 * 1024);

      if (!camera->Open(sim_cfg)) {
        MV_LOG_ERROR("foxglove-test", "无法打开 SimCamera，endpoint={}",
                     sim_cfg["endpoint"].as<std::string>());
        return EXIT_FAILURE;
      }
      MV_LOG_INFO("foxglove-test", "SimCamera 打开成功，endpoint={}",
                  sim_cfg["endpoint"].as<std::string>());
    } else {
      camera = std::make_unique<mv::hal::OpenCvCamera>();
      if (!camera->Open(ARGS.cam_cfg)) {
        MV_LOG_ERROR("foxglove-test", "无法打开 OpenCv 图像源");
        return EXIT_FAILURE;
      }
      MV_LOG_INFO("foxglove-test", "OpenCv 图像源打开成功");
    }

    // ── 静态 TF：gimbal → camera（相机安装位置固定，无论是否有外参配置都必须发布）──
    // 默认外参：Y 轴翻转（相机 down+Y → 云台 up+Y），与 PnpSolver 默认值一致
    {
      Eigen::Matrix3d R_c2g =
          (Eigen::Matrix3d() << 1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0).finished();

      if (ROOT_CFG["calibration"] && ROOT_CFG["calibration"]["R_camera_to_gimbal"]) {
        try {
          auto r_vals = ROOT_CFG["calibration"]["R_camera_to_gimbal"].as<std::vector<double>>();
          if (r_vals.size() == 9) {
            R_c2g = Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(r_vals.data());
            MV_LOG_INFO("foxglove-test", "TF gimbal→camera: 使用配置文件外参");
          }
        } catch (const std::exception& e) {
          MV_LOG_WARN("foxglove-test", "解析 R_camera_to_gimbal 失败，回退为 Y 翻转默认值: {}",
                      e.what());
        }
      } else {
        MV_LOG_INFO("foxglove-test", "TF gimbal→camera: 未配置外参，使用默认 Y 轴翻转");
      }

      // gimbal→camera = R_c2g^T（逆变换）
      Eigen::Matrix4d T_g2c = Eigen::Matrix4d::Identity();
      T_g2c.block<3, 3>(0, 0) = R_c2g.transpose();

      const int64_t now_ns =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
      sink.PublishTransform("gimbal", "camera", T_g2c, now_ns);
      MV_LOG_INFO("foxglove-test", "已发布静态 TF: gimbal → camera");
    }

    // ── 6. 运行时状态 ─────────────────────────────────────────────────────────

    // 颜色优先级：命令行 > vision.yaml > 默认 RED
    mv::ArmorColor enemy_color = ARGS.enemy_color;
    if (enemy_color == mv::ArmorColor::RED) {
      // 没有命令行覆盖，从配置文件读取
      const auto yaml_color = cfg.Get<std::string>("auto_aim.enemy_color", "red");
      if (yaml_color == "blue") {
        enemy_color = mv::ArmorColor::BLUE;
      }
    }

    bool loop_video = ARGS.is_file_source;
    mv::modules::RoiManager roi_mgr;
    FpsTracker fps_tracker{30};
    std::mutex color_mtx;  ///< 保护 enemy_color 在Foxglove回调与主循环间的并发访问
    mv::tool::ArmorDetectorParamManager armor_pm(&detector);

    // ── Foxglove 参数：enemy_color（支持面板实时切换）───────────────────────
    {
      const std::string init_color = (enemy_color == mv::ArmorColor::BLUE) ? "blue" : "red";
      sink.UpdateParameters({{"enemy_color", init_color}});
    }
    armor_pm.PushToFoxglove(sink);

    sink.SetParameterCallback([&](const std::string& name, const nlohmann::json& /*raw*/) {
      if (armor_pm.HandleParameter(sink, name)) {
        return;
      }
      if (name == "enemy_color") {
        const auto val = sink.GetParameter("enemy_color");
        if (!val.is_null() && val.is_string()) {
          const std::string s = val.get<std::string>();
          std::lock_guard<std::mutex> lk(color_mtx);
          enemy_color = (s == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
          roi_mgr.Reset();  // 切换颜色时清空 ROI
          MV_LOG_INFO("foxglove-test", "[参数] enemy_color 切换为: {}", s);
        }
      }
    });

    MV_LOG_INFO("foxglove-test", "识别颜色: {}（来源: {}），视频循环: {}",
                (enemy_color == mv::ArmorColor::RED) ? "RED" : "BLUE",
                (enemy_color == ARGS.enemy_color) ? "命令行" : "vision.yaml",
                loop_video ? "ON" : "OFF");
    MV_LOG_INFO("foxglove-test", "输入后端: {}",
          ARGS.use_sim_camera ? "SimCamera" : "OpenCvCamera");

    std::cout << "\n[foxglove-test] 提示：\n"
              << "  Foxglove Studio 连接 → ws://localhost:" << ARGS.foxglove_port << "\n"
              << "  按 Ctrl+C 退出\n\n"
              << "  已发布话题：\n"
              << "    camera/raw          — 原始图像帧\n"
              << "    camera/annotated    — 检测框 + PnP 重投影 + 状态栏\n"
              << "    detections/annotations — 装甲板 2D 角点 (ImageAnnotations)\n"
              << "    detections/3d       — 装甲板 3D 立方体 (SceneUpdate)\n"
              << "    pnp/debug_image     — PnP 调试图（绿=检测点, 青=重投影点）\n"
              << "    pnp/axes_3d         — 每块装甲板的 RGB XYZ 坐标轴 (SceneUpdate)\n"
              << "    pnp/residuals       — 位姿/距离/重投影误差 JSON\n"
              << "    control/gimbal      — 云台控制指令 JSON\n"
              << "    /tf                 — world→gimbal 动态 TF + gimbal→camera 静态 TF\n"
              << "    pipeline/nodes      — 线程健康 JSON\n\n";

    // ── 7. 主循环 ─────────────────────────────────────────────────────────────
    cv::Mat frame;
    uint64_t frame_idx = 0;

    while (!g_quit.load(std::memory_order_relaxed)) {
      // 采帧
      if (!camera->Grab(frame) || frame.empty()) {
        if (!ARGS.use_sim_camera && loop_video && ARGS.is_file_source) {
          camera->Close();
          if (!camera->Open(ARGS.cam_cfg)) {
            MV_LOG_ERROR("foxglove-test", "视频重新打开失败");
            break;
          }
          predictor.Init(ROOT_CFG);
          roi_mgr.Reset();
          continue;
        }

        if (ARGS.use_sim_camera) {
          // SimCamera 内部支持断线重连；此处短暂退避，避免空转占满 CPU。
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
          continue;
        }

        MV_LOG_INFO("foxglove-test", "视频结束或相机断开");
        break;
      }

      ++frame_idx;
      fps_tracker.Tick();

      // 计时戳（纳秒）
      const int64_t ts_ns =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());

      // ── 检测 + 解算 + 预测 ────────────────────────────────────────────────
      const auto T_FRAME = std::chrono::steady_clock::now();

      // ROI 裁剪 → 检测（局部坐标）→ 坐标恢复 + 更新 ROI 状态
      mv::ArmorColor cur_color;
      {
        std::lock_guard<std::mutex> lk(color_mtx);
        cur_color = enemy_color;
      }
      auto [cropped, roi_offset] = roi_mgr.Crop(frame);
      auto detections = detector.Detect(cropped, cur_color);
      roi_mgr.RestoreAndUpdate(detections, roi_offset, frame.size());

      for (auto& det : detections) {
        solver.Solve(det);
      }
      const mv::GimbalControl CTRL = predictor.Predict(detections, T_FRAME, cur_color);

      // ── Foxglove 发布 ─────────────────────────────────────────────────────

      // 1. 原始图像（零客户端时内部自动跳过编码）
      sink.PublishImage(frame, "camera/raw", "camera", ts_ns);

      // 2. 富调试图（检测框 + PnP 重投影 + 状态栏），仅有客户端时绘制
      if (sink.HasClients()) {
        sink.PublishImage(DrawAnnotated(frame, detections, CTRL, fps_tracker.Fps()),
                          "camera/annotated", "camera", ts_ns);
      }

      // 3. 装甲板 2D/3D 检测结果
      sink.PublishDetections(detections, ts_ns);

      // 4. PnP 三层调试（零客户端时内部自动跳过图像编码）
      sink.PublishPnpResult(detections, frame, ts_ns);

      // 5. 云台控制指令
      sink.PublishGimbalControl(CTRL, ts_ns);

      // 6. world → gimbal TF（以云台 yaw/pitch 角驱动）
      sink.PublishTransform("world", "gimbal", GimbalTransform(CTRL.yaw, CTRL.pitch), ts_ns);

      // 7. 线程健康（单线程模式：用实测 FPS 伪造单节点上报）
      mv::tool::TerminalHUD::NodeMetrics node_metric;
      node_metric.node_name = "VideoDetect";
      node_metric.fps = fps_tracker.Fps();
      node_metric.latency_ms = (frame_idx > 0) ? (1000.0 / std::max(fps_tracker.Fps(), 1.0)) : 0.0;
      node_metric.is_alive = true;

      // FoxgloveSink::ThreadMetrics 与 TerminalHUD::NodeMetrics 字段一一对应
      mv::tool::FoxgloveSink::ThreadMetrics thread_metric;
      thread_metric.node_name = node_metric.node_name;
      thread_metric.fps = node_metric.fps;
      thread_metric.latency_ms = node_metric.latency_ms;
      thread_metric.is_alive = true;
      sink.PublishThreadMetrics({thread_metric}, ts_ns);

      // ── TerminalHUD 刷新（200 ms 速率限制，零连接时的主要调试手段）──────
      std::vector<mv::tool::TerminalHUD::NodeMetrics> nm{node_metric};
      hud.Update(fps_tracker.Fps(), detections, &CTRL, &nm);

      // ── Foxglove 参数同步（每秒一次，避免频繁推送）────────────────────────
      if (frame_idx % 60 == 0) {
        std::string cur_str;
        {
          std::lock_guard<std::mutex> lk(color_mtx);
          cur_str = (enemy_color == mv::ArmorColor::BLUE) ? "blue" : "red";
        }
        sink.UpdateParameters({{"enemy_color", cur_str}});
      }
    }

    // ── 8. 清理 ───────────────────────────────────────────────────────────────
    hud.Flush();
    if (!armor_pm.SaveToFile(OVERRIDE_YAML, std::string(CONFIG_FILE_PATH) + "/vision.yaml")) {
      MV_LOG_WARN("foxglove-test", "保存 detector 调参覆盖文件失败: {}", OVERRIDE_YAML);
    }
    camera->Close();
    sink.Stop();
    MV_LOG_INFO("foxglove-test", "共处理 {} 帧", frame_idx);
    MV_LOG_INFO("foxglove-test", "══════════ mv-foxglove-vision-test 结束 ══════════");

  } catch (const std::exception& exc) {
    std::cerr << "[foxglove-test] FATAL: " << exc.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
