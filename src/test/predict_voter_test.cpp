/**
 * @file predict_voter_test.cpp
 * @brief 预测 + 决策模块 Foxglove 端到端调试测试
 *
 * 【功能】
 *   以离线视频或实时摄像头为输入，将识别→PnP→EKF预测→决策完整链路串联，
 *   通过 Foxglove Studio WebSocket 进行全链路可视化调试，并支持通过
 *   Foxglove 参数面板实时调节 EKF / Voter 参数，无需重启程序。
 *
 *   检测链路：
 *     OpenCvCamera → BasicArmorDetector → PnpSolver
 *         → EkfPredictor → CooldownVoter → FoxgloveSink
 *
 * 【Foxglove 发布话题】
 *   ┌──────────────────────────────┬─────────────────────────────┬────────────┐
 *   │ 话题                         │ Schema                      │ 建议面板   │
 *   ├──────────────────────────────┼─────────────────────────────┼────────────┤
 *   │ camera/raw                   │ foxglove.RawImage            │ Image      │
 *   │ camera/annotated             │ foxglove.RawImage            │ Image      │
 *   │ detections/annotations       │ foxglove.ImageAnnotations   │ Image 叠加 │
 *   │ detections/3d                │ foxglove.SceneUpdate        │ 3D         │
 *   │ pnp/debug_image              │ foxglove.RawImage            │ Image      │
 *   │ pnp/axes_3d                  │ foxglove.SceneUpdate        │ 3D         │
 *   │ pnp/residuals                │ PnpResiduals (JSON)         │ Raw Msgs   │
 *   │ tracking/armor_positions     │ foxglove.SceneUpdate        │ 3D         │
 *   │ tracking/rotation_center     │ foxglove.SceneUpdate        │ 3D         │
 *   │ tracking/aim_point           │ foxglove.SceneUpdate        │ 3D         │
 *   │ tracking/target_state        │ JSON                        │ Raw/Plot   │
 *   │ voter/decision               │ JSON                        │ Raw Msgs   │
 *   │ debug/params                 │ JSON                        │ Raw Msgs   │
 *   │ control/gimbal               │ GimbalControl               │ Raw Msgs   │
 *   │ /tf                          │ foxglove.FrameTransforms    │ 3D / TF树  │
 *   │ pipeline/nodes               │ PipelineNodes               │ Raw Msgs   │
 *   └──────────────────────────────┴─────────────────────────────┴────────────┘
 *
 *   注：camera/annotated 仅在有 Foxglove 客户端连接时绘制（HasClients() 门控），
 *       避免无接收端时浪费图像 clone + encode 开销。
 *
 * 【Foxglove Studio 快速接入】
 *   新建连接 → WebSocket → ws://NUC_IP:8765（或指定端口）
 *
 * 【参数面板（实时调节，无需重启）】
 *   Foxglove 参数面板可调节以下参数，修改后对应模块自动重初始化：
 *   ┌────────────────────────────────────┬───────────────────────────────────┐
 *   │ 参数名                              │ 说明                              │
 *   ├────────────────────────────────────┼───────────────────────────────────┤
 *   │ enemy_color                        │ 识别目标颜色 (red/blue)            │
 *   │ loop_video                         │ 视频文件循环播放                    │
 *   │ playback_fps                       │ 视频播放速率节流（0=不限速）         │
 *   │ ekf.min_detect_count               │ 进入跟踪态所需最小检测帧数           │
 *   │ ekf.max_temp_lost_count            │ 暂失跟踪最大容忍帧数                │
 *   │ ekf.process_noise_pos              │ EKF 位置过程噪声                   │
 *   │ ekf.process_noise_ang              │ EKF 角度过程噪声                   │
 *   │ ekf.yaw_offset_deg                 │ 云台 Yaw 补偿（°）                 │
 *   │ ekf.pitch_offset_deg               │ 云台 Pitch 补偿（°）               │
 *   │ ekf.low_speed_delay_ms             │ 低速弹道提前量（ms）                │
 *   │ ekf.high_speed_delay_ms            │ 高速弹道提前量（ms）                │
 *   │ ekf.bullet_speed                   │ 模拟弹速（m/s）                    │
 *   │ voter.auto_fire                    │ 是否允许自动开火                    │
 *   │ voter.min_lock_frames              │ 连续锁定帧数阈值                    │
 *   │ voter.first_tolerance_rad          │ 首次开火角度容差（rad）              │
 *   │ voter.fire_tolerate_frames         │ 开火后容忍帧数                      │
 *   │ sim.yaw_deg / pitch_deg / roll_deg │ 模拟云台姿态（无真实 IMU 时手动拨）  │
 *   └────────────────────────────────────┴───────────────────────────────────┘
 *
 * 【用法】
 * @code
 *   mv-predict-voter-test [video_or_camera] [red|blue] [port]
 *
 *   # 示例：
 *   mv-predict-voter-test                           # 摄像头 0，红方，端口 8765
 *   mv-predict-voter-test armor.mp4                 # 离线视频文件
 *   mv-predict-voter-test armor.mp4 blue            # 识别蓝方
 *   mv-predict-voter-test armor.mp4 red 9090        # 自定义端口
 *   mv-predict-voter-test 1                         # 摄像头索引 1
 * @endcode
 *
 * 【辅助模块（均位于 src/tool/debug/）】
 *   PredictParamManager   — 参数状态管理、Foxglove 双向同步、YAML 注入、JSON 构建
 *   predict_annotator.hpp — DrawAnnotated / SimGimbalQuaternion / ThrottlePlayback
 *   MetricsTracker        — 滑动窗口 FPS 统计 + 结束摘要
 *   TerminalHUD           — 终端原地刷新状态行（不干扰日志时固定显示）
 *
 * 【日志策略】
 *   控制台：info 及以上（避免 debug 消息滚动终端破坏 TerminalHUD 固定显示）
 *   文件  ：trace 及以上（logs/ 目录，以时间戳命名，保留完整调试记录）
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/camera/opencv_camera.hpp"
#include "interfaces/types.hpp"
#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/armor_detector/roi_manager.hpp"
#include "modules/cooldown_voter/cooldown_voter.hpp"
#include "modules/ekf_predictor/ekf_predictor.hpp"
#include "modules/pnp_solver/pnp_solver.hpp"
#include "tool/debug/metrics_tracker.hpp"
#include "tool/debug/predict_annotator.hpp"
#include "tool/debug/predict_param_manager.hpp"
#include "tool/debug/terminal_hud.hpp"
#include "tool/foxglove/foxglove_sink.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

namespace {

std::atomic<bool> g_quit{false};

extern "C" void SigIntHandler(int /*sig*/) noexcept {
  g_quit.store(true, std::memory_order_relaxed);
}

struct CliArgs {
  YAML::Node cam_cfg;
  bool is_file_source{false};
  mv::ArmorColor enemy_color{mv::ArmorColor::RED};
  uint16_t foxglove_port{8765};
};

CliArgs ParseCliArgs(int argc, char** argv) {
  CliArgs args;

  if (argc > 1) {
    const std::string src(argv[1]);
    const bool is_index = !src.empty() && std::all_of(src.begin(), src.end(), [](unsigned char c) {
      return std::isdigit(c) != 0;
    });
    if (is_index) {
      args.cam_cfg["source"] = std::stoi(src);
    } else {
      args.cam_cfg["source"] = src;
      args.is_file_source = true;
    }
  } else {
    args.cam_cfg["source"] = 0;
  }

  if (argc > 2) {
    args.enemy_color =
        (std::string(argv[2]) == "blue") ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
  }

  if (argc > 3) {
    args.foxglove_port = static_cast<uint16_t>(std::stoi(argv[3]));
  }

  return args;
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGINT, SigIntHandler);

  try {
    const CliArgs cli = ParseCliArgs(argc, argv);

    auto& cfg = mv::ConfigManager::Instance();
    cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
    // 控制台仅显示 info 及以上，避免 debug 消息滚动终端导致 TerminalHUD 被刷走；
    // debug 级别日志仍完整写入 logs/ 文件供离线分析。
    mv::Logger::Instance().Init("logs", spdlog::level::info, true);

    MV_LOG_INFO("predict-voter-test", "══════════ mv-predict-voter-test 启动 ══════════");
    MV_LOG_INFO("predict-voter-test", "Foxglove WebSocket 端口: {}", cli.foxglove_port);

    mv::tool::PredictParamManager pm;
    pm.State().enemy_color = cli.enemy_color;
    if (pm.State().enemy_color == mv::ArmorColor::RED &&
        cfg.Get<std::string>("auto_aim.enemy_color", "red") == "blue") {
      pm.State().enemy_color = mv::ArmorColor::BLUE;
    }
    pm.State().loop_video = cli.is_file_source;

    YAML::Node root_cfg = cfg.Subtree();
    pm.InjectParamsToYaml(root_cfg);

    mv::modules::BasicArmorDetector detector;
    mv::modules::PnpSolver solver;
    if (!detector.Init(root_cfg) || !solver.Init(root_cfg)) {
      MV_LOG_ERROR("predict-voter-test", "检测/求解器初始化失败，退出");
      return EXIT_FAILURE;
    }

    const std::string override_yaml = std::string(CONFIG_FILE_PATH) + "/debug/debug_override.yaml";
    if (std::filesystem::exists(override_yaml)) {
      try {
        const YAML::Node ov = YAML::LoadFile(override_yaml);
        if (ov && ov["detector"]) {
          detector.Init(ov);
          MV_LOG_INFO("predict-voter-test", "已应用 debug_override.yaml 检测参数覆盖");
        }
      } catch (const std::exception& e) {
        MV_LOG_WARN("predict-voter-test", "读取 debug_override.yaml 失败（忽略）: {}", e.what());
      }
    }

    mv::modules::EkfPredictor predictor;
    mv::modules::CooldownVoter voter;

    // ── 解析相机内参（供 DrawPredictedArmors 重投影使用）────────────────────
    cv::Mat repro_K = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat repro_D = cv::Mat::zeros(1, 5, CV_64F);
    Eigen::Matrix3d repro_R_c2g =
        (Eigen::Matrix3d() << 1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0).finished();
    Eigen::Vector3d repro_t_c2g = Eigen::Vector3d::Zero();
    if (root_cfg["calibration"]) {
      const auto& calib = root_cfg["calibration"];
      try {
        if (calib["camera_matrix"]) {
          auto cm = calib["camera_matrix"].as<std::vector<double>>();
          if (cm.size() == 9)
            repro_K = cv::Mat(3, 3, CV_64F, cm.data()).clone();
        }
        if (calib["distort_coeffs"]) {
          auto dc = calib["distort_coeffs"].as<std::vector<double>>();
          repro_D = cv::Mat(1, static_cast<int>(dc.size()), CV_64F, dc.data()).clone();
        }
        if (calib["R_camera_to_gimbal"]) {
          auto r = calib["R_camera_to_gimbal"].as<std::vector<double>>();
          if (r.size() == 9)
            repro_R_c2g = Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(r.data());
        }
        if (calib["t_camera_to_gimbal"]) {
          auto t = calib["t_camera_to_gimbal"].as<std::vector<double>>();
          if (t.size() == 3)
            repro_t_c2g = Eigen::Vector3d(t[0], t[1], t[2]);
        }
      } catch (const std::exception& e) {
        MV_LOG_WARN("predict-voter-test", "解析相机内参失败（重投影将退化）: {}", e.what());
      }
    }

    if (!predictor.Init(root_cfg)) {
      MV_LOG_ERROR("predict-voter-test", "EkfPredictor 初始化失败，退出");
      return EXIT_FAILURE;
    }
    if (!voter.Init(root_cfg)) {
      MV_LOG_ERROR("predict-voter-test", "CooldownVoter 初始化失败，退出");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("predict-voter-test", "EkfPredictor + CooldownVoter 初始化成功");

    mv::hal::OpenCvCamera camera;
    if (!camera.Open(cli.cam_cfg)) {
      MV_LOG_ERROR("predict-voter-test", "无法打开图像源");
      return EXIT_FAILURE;
    }
    MV_LOG_INFO("predict-voter-test", "图像源打开成功");

    mv::tool::FoxgloveSinkConfig fox_cfg;
    fox_cfg.port = cli.foxglove_port;
    // ── Foxglove 图像发布优化：JPEG 压缩 + 分辨率缩放 ────────────────────────
    // JPEG 质量 50 + 480×270 可将每帧数据从 ~2.7MB 降至 ~5KB（约 540 倍）。
    // 真正的帧率瓶颈不在 JPEG 编码质量，而在 resize 的输入分辨率：
    //   视频分辨率 2700×2160（~17.5MB/帧），每次 ImagePublisher 内部做
    //   cv::resize(2700×2160 → 480×270, INTER_AREA) 就要 3~5ms，
    //   DrawAnnotated 里的 frame.clone() 也需要 ~2ms。
    // 解决方案：在主循环入口处做 **一次** resize 得到小图 frame_display，
    //   之后 DrawAnnotated / PublishImage 全程在 480×270 上操作，
    //   ImagePublisher 内部检测到尺寸已吻合则跳过第二次 resize。
    fox_cfg.use_jpeg = true;
    fox_cfg.jpeg_quality = 50;
    fox_cfg.publish_width = 480;
    fox_cfg.publish_height = 270;
    constexpr int kDisplayW = 480;
    constexpr int kDisplayH = 270;
    mv::tool::FoxgloveSink sink{fox_cfg};
    sink.Start();
    MV_LOG_INFO("predict-voter-test", "Foxglove 服务已启动 (ws://0.0.0.0:{})", cli.foxglove_port);

    mv::tool::TerminalHUD hud;
    pm.Register(sink);
    pm.PushToFoxglove(sink);

    {
      Eigen::Matrix3d r_c2g =
          (Eigen::Matrix3d() << 1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0).finished();

      if (root_cfg["calibration"] && root_cfg["calibration"]["R_camera_to_gimbal"]) {
        try {
          auto r = root_cfg["calibration"]["R_camera_to_gimbal"].as<std::vector<double>>();
          if (r.size() == 9) {
            r_c2g = Eigen::Map<const Eigen::Matrix<double, 3, 3, Eigen::RowMajor>>(r.data());
          }
        } catch (...) {
          MV_LOG_WARN("predict-voter-test", "解析 R_camera_to_gimbal 失败，使用 Y 翻转默认值");
        }
      }

      Eigen::Matrix4d t_g2c = Eigen::Matrix4d::Identity();
      t_g2c.block<3, 3>(0, 0) = r_c2g.transpose();

      const int64_t now_ns =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
      sink.PublishTransform("gimbal", "camera", t_g2c, now_ns);
      MV_LOG_INFO("predict-voter-test", "已发布静态 TF: gimbal → camera");
    }

    std::cout << "\n[predict-voter-test] 提示：\n"
              << "  Foxglove Studio  → ws://localhost:" << cli.foxglove_port << "\n"
              << "  按 Ctrl+C 退出\n\n";

    mv::modules::RoiManager roi_mgr;
    mv::tool::MetricsTracker metrics{30};
    cv::Mat frame;
    uint64_t frame_idx = 0;

    // ── Foxglove 发布节流控制 ────────────────────────────────────────────────────────
    // 主循环尽量跑满速，图像/JSON 类消息只按 foxglove_target_fps 向 Foxglove 推送。
    // 这样计算（EKF、检测）始终每帧执行，可视化内容降频到 ~30fps，不影响控制精度。
    // foxglove_publish_interval 表示每隔多少帧才发一次（由主循环预期 FPS 推算）。
    constexpr int kFoxgloveTargetFps = 30;  ///< Foxglove 可视化目标帧率
    int foxglove_publish_interval = 4;  ///< 每 N 帧向 Foxglove 发一次（初始假设主循环 ~120fps）
    uint64_t last_fox_frame = 0;  ///< 上次向 Foxglove 发布图像的帧号
    (void)kFoxgloveTargetFps;     // 可用于若干帧后动态调整区间

    while (!g_quit.load(std::memory_order_relaxed)) {
      const auto frame_start = std::chrono::steady_clock::now();

      if (pm.ConsumeReinitEkf()) {
        pm.InjectParamsToYaml(root_cfg);
        predictor.Reset();
        voter.Reset();
        roi_mgr.Reset();

        if (!predictor.Init(root_cfg)) {
          MV_LOG_WARN("predict-voter-test", "EkfPredictor 重初始化失败，跳过本次更新");
        } else {
          mv::tool::ParamState ps_snap;
          {
            std::lock_guard<std::mutex> lk(pm.Mutex());
            ps_snap = pm.State();
          }
          MV_LOG_INFO("predict-voter-test",
                      "[参数] EKF 已重初始化 (pos_noise={:.1f} ang_noise={:.1f} yaw_off={:.2f}° "
                      "pit_off={:.2f}°)",
                      ps_snap.ekf_process_noise_pos, ps_snap.ekf_process_noise_ang,
                      ps_snap.ekf_yaw_offset_deg, ps_snap.ekf_pitch_offset_deg);
        }
      }

      if (pm.ConsumeReinitVoter()) {
        pm.InjectParamsToYaml(root_cfg);
        voter.Reset();
        if (!voter.Init(root_cfg)) {
          MV_LOG_WARN("predict-voter-test", "CooldownVoter 重初始化失败，跳过本次更新");
        } else {
          mv::tool::ParamState ps_snap;
          {
            std::lock_guard<std::mutex> lk(pm.Mutex());
            ps_snap = pm.State();
          }
          MV_LOG_INFO(
              "predict-voter-test",
              "[参数] Voter 已重初始化 (auto_fire={} min_lock={} tol={:.4f}rad tolerate_frames={})",
              ps_snap.voter_auto_fire, ps_snap.voter_min_lock_frames,
              ps_snap.voter_first_tolerance_rad, ps_snap.voter_fire_tolerate_frames);
        }
      }

      mv::tool::ParamState ps_snap;
      {
        std::lock_guard<std::mutex> lk(pm.Mutex());
        ps_snap = pm.State();
      }

      if (!camera.Grab(frame) || frame.empty()) {
        if (ps_snap.loop_video && cli.is_file_source) {
          camera.Close();
          if (!camera.Open(cli.cam_cfg)) {
            MV_LOG_ERROR("predict-voter-test", "视频重新打开失败，退出");
            break;
          }
          predictor.Reset();
          predictor.Init(root_cfg);
          voter.Reset();
          voter.Init(root_cfg);
          roi_mgr.Reset();
          MV_LOG_INFO("predict-voter-test", "视频循环重新播放");
          continue;
        }
        MV_LOG_INFO("predict-voter-test", "视频结束或相机断开");
        break;
      }

      ++frame_idx;

      const int64_t ts_ns =
          static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());
      const auto t_frame = std::chrono::steady_clock::now();

      predictor.SetGimbalOrientation(mv::tool::SimGimbalQuaternion(
          ps_snap.sim_yaw_deg, ps_snap.sim_pitch_deg, ps_snap.sim_roll_deg));

      auto [cropped, roi_offset] = roi_mgr.Crop(frame);
      auto detections = detector.Detect(cropped, ps_snap.enemy_color);
      roi_mgr.RestoreAndUpdate(detections, roi_offset, frame.size());

      for (auto& det : detections) {
        solver.Solve(det);
      }

      metrics.Tick(!detections.empty(), static_cast<int>(detections.size()));

      mv::GimbalControl ctrl = predictor.Predict(detections, t_frame, ps_snap.enemy_color);
      const mv::TrackTarget target = predictor.GetTrackTarget();
      const bool fire_permitted = voter.Vote(target, ctrl);
      ctrl.fire = fire_permitted;

      // ── 动态调整 Foxglove 发布间隔（后 100 帧根据实际 FPS 更新） ────────────────
      if (frame_idx == 100) {
        const double cur_fps = metrics.CurrentFps();
        if (cur_fps > 1.0) {
          foxglove_publish_interval = std::max(1, static_cast<int>(cur_fps / kFoxgloveTargetFps));
        }
      }
      const bool should_publish_fox =
          (frame_idx - last_fox_frame) >= static_cast<uint64_t>(foxglove_publish_interval);
      if (should_publish_fox) {
        last_fox_frame = frame_idx;
      }

      if (should_publish_fox) {
        // ── 一次性预缩放：将 2700×2160 帧缩到显示分辨率 ──────────────────────
        // 之后所有 Foxglove 图像操作均在小图上进行，彻底消除：
        //   ① ImagePublisher 内部的重复 resize（3~5ms/次）
        //   ② DrawAnnotated 中对原始大帧的 frame.clone()（~2ms）
        cv::Mat frame_display;
        const float sx = static_cast<float>(kDisplayW) / static_cast<float>(frame.cols);
        const float sy = static_cast<float>(kDisplayH) / static_cast<float>(frame.rows);
        cv::resize(frame, frame_display, cv::Size(kDisplayW, kDisplayH), 0, 0, cv::INTER_AREA);

        // 缩放检测坐标到显示分辨率
        std::vector<mv::Detection> dets_display = detections;
        for (auto& d : dets_display) {
          for (auto& pt : d.points) {
            pt.x *= sx;
            pt.y *= sy;
          }
          for (auto& pt : d.reprojected_points) {
            pt.x *= sx;
            pt.y *= sy;
          }
          for (auto& pt : d.reprojected_points_alt) {
            pt.x *= sx;
            pt.y *= sy;
          }
        }

        // 缩放相机内参 K（用于 DrawPredictedArmors 的重投影）
        cv::Mat repro_K_disp = repro_K.clone();
        repro_K_disp.at<double>(0, 0) *= sx;  // fx
        repro_K_disp.at<double>(1, 1) *= sy;  // fy
        repro_K_disp.at<double>(0, 2) *= sx;  // cx
        repro_K_disp.at<double>(1, 2) *= sy;  // cy

        sink.PublishImage(frame_display, "camera/raw", "camera", ts_ns);
        if (sink.HasClients()) {
          cv::Mat annotated = mv::tool::DrawAnnotated(frame_display, dets_display, ctrl, target,
                                                      fire_permitted, metrics.CurrentFps());
          mv::tool::DrawPredictedArmors(annotated, target, repro_K_disp, repro_D, repro_R_c2g,
                                        repro_t_c2g);
          sink.PublishImage(annotated, "camera/annotated", "camera", ts_ns);
        }
        sink.PublishDetections(detections, ts_ns);
        sink.PublishPnpResult(detections, frame_display, ts_ns);
      }

      const Eigen::Vector3d aim_xyz =
          target.is_tracking ? target.position : Eigen::Vector3d::Zero();
      if (should_publish_fox) {
        sink.PublishTrackingVisuals(target, aim_xyz, "world", ts_ns);
        sink.PublishJson("tracking/target_state",
                         mv::tool::PredictParamManager::MakeTargetStateJson(target, ctrl), ts_ns);
        sink.PublishJson("voter/decision",
                         mv::tool::PredictParamManager::MakeVoterJson(fire_permitted, target,
                                                                      ps_snap.voter_auto_fire),
                         ts_ns);
      }
      sink.PublishGimbalControl(ctrl, ts_ns);
      sink.PublishTransform("world", "gimbal", mv::tool::WorldToGimbalTF(ctrl.yaw, ctrl.pitch),
                            ts_ns);

      if (frame_idx % 60 == 0) {
        sink.PublishJson("debug/params", pm.MakeParamsSnapshotJson(), ts_ns);
        pm.PushToFoxglove(sink);
      }

      {
        mv::tool::FoxgloveSink::ThreadMetrics tm;
        tm.node_name = "PredictVoter";
        tm.fps = metrics.CurrentFps();
        tm.latency_ms = (metrics.CurrentFps() > 0.0) ? (1000.0 / metrics.CurrentFps()) : 0.0;
        tm.is_alive = true;
        sink.PublishThreadMetrics({tm}, ts_ns);
      }

      {
        mv::tool::TerminalHUD::NodeMetrics nm;
        nm.node_name = "PredictVoter";
        nm.fps = metrics.CurrentFps();
        nm.latency_ms = (metrics.CurrentFps() > 0.0) ? (1000.0 / metrics.CurrentFps()) : 0.0;
        nm.is_alive = true;
        const std::vector<mv::tool::TerminalHUD::NodeMetrics> hud_metrics{nm};
        hud.Update(metrics.CurrentFps(), detections, &ctrl, &hud_metrics);
      }

      mv::tool::ThrottlePlayback(frame_start, ps_snap.playback_fps);
    }

    hud.Flush();
    camera.Close();
    sink.Stop();
    metrics.PrintStats();

    MV_LOG_INFO("predict-voter-test", "共处理 {} 帧", frame_idx);
    MV_LOG_INFO("predict-voter-test", "══════════ mv-predict-voter-test 结束 ══════════");
  } catch (const std::exception& exc) {
    std::cerr << "[predict-voter-test] FATAL: " << exc.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
