/**
 * @file ekf_predictor.cpp
 * @brief EkfPredictor Pimpl 实现与工厂注册（Stage 8-C/D）
 *
 * Stage 8-C: Impl 结构体 + YAML 解析 + EkfTracker/TrajectorySolver 初始化
 * Stage 8-D: Predict() 核心流程（坐标系变换 + 追踪 + 弹道求解）
 */

#include "ekf_predictor.hpp"

#include "core/logger.hpp"
#include "detail/ekf_track_target.hpp"
#include "detail/ekf_tracker.hpp"
#include "detail/trajectory_solver.hpp"
#include "factory/factory.hpp"

#include <chrono>

#include <Eigen/Geometry>
#include <optional>
#include <yaml-cpp/yaml.h>

namespace mv::modules {

// ── Pimpl 私有数据 ───────────────────────────────────────────────────────────

struct EkfPredictor::Impl {
  // ── 核心算法组件 ───────────────────────────────────────────────────────
  detail::EkfTracker tracker;
  detail::TrajectorySolver solver;

  // ── 坐标系变换 ─────────────────────────────────────────────────────────
  /// 云台系 → 世界系旋转矩阵（由 SetGimbalOrientation() 每帧更新）
  Eigen::Matrix3d R_gimbal2world{Eigen::Matrix3d::Identity()};

  // ── 弹速 ───────────────────────────────────────────────────────────────
  /// 从 YAML 读取的默认弹速（m/s）；Stage 8-F 接入串口后可改为原子量
  double bullet_speed{23.0};

  // ── 杂项 ───────────────────────────────────────────────────────────────
  TrackTarget last_track_target{};
  YAML::Node config{};
  bool initialized{false};

  // ── 构造（需要 EkfTracker/TrajectorySolver 的默认参数先就绪）─────────
  Impl() : tracker(detail::EkfTrackerParams{}), solver(detail::TrajectorySolverParams{}) {}
};

// ── 构造 / 析构 ──────────────────────────────────────────────────────────────

EkfPredictor::EkfPredictor() : impl_(std::make_unique<Impl>()) {}

EkfPredictor::~EkfPredictor() = default;

// ── Init ─────────────────────────────────────────────────────────────────────

bool EkfPredictor::Init(const YAML::Node& config) {
  impl_->config = config;

  // 兼容两种 YAML 布局：
  //   1. 传入整个 auto_aim.yaml 根节点（字段直接在根）
  //   2. 传入 auto_aim.ekf_predictor 子节点（字段直接在该节点）
  const YAML::Node& n = config["ekf_predictor"].IsDefined() ? config["ekf_predictor"] : config;

  // ── 从 YAML 填充 EkfTrackerParams ──────────────────────────────────────
  detail::EkfTrackerParams tp;
  tp.min_detect_count = n["min_detect_count"].as<int>(5);
  tp.max_detecting_lost_count = n["max_detecting_lost_count"].as<int>(2);
  tp.max_temp_lost_count = n["max_temp_lost_count"].as<int>(15);
  tp.outpost_max_temp_lost_count = n["outpost_max_temp_lost_count"].as<int>(30);
  tp.max_dt_sec = n["max_dt_sec"].as<double>(0.1);

  tp.init_radius_small = n["init_radius_small"].as<double>(0.27);
  tp.init_radius_big = n["init_radius_big"].as<double>(0.27);
  tp.init_radius_outpost = n["init_radius_outpost"].as<double>(0.26);

  tp.process_noise_pos = n["process_noise_pos"].as<double>(100.0);
  tp.process_noise_ang = n["process_noise_ang"].as<double>(400.0);
  tp.process_noise_outpost_pos = n["process_noise_outpost_pos"].as<double>(10.0);
  tp.process_noise_outpost_ang = n["process_noise_outpost_ang"].as<double>(0.1);

  tp.divergence_threshold = n["divergence_threshold"].as<double>(1e6);

  // 初始协方差对角（11 维）
  if (n["P0_diag"].IsDefined()) {
    auto vec = n["P0_diag"].as<std::vector<double>>();
    if (vec.size() == 11) {
      tp.P0_diag = Eigen::Map<Eigen::VectorXd>(vec.data(), 11);
    }
  }

  // ── 从 YAML 填充 TrajectorySolverParams ────────────────────────────────
  detail::TrajectorySolverParams sp;
  sp.yaw_offset_rad = n["yaw_offset_deg"].as<double>(0.0) / 57.295779513;
  sp.pitch_offset_rad = n["pitch_offset_deg"].as<double>(0.0) / 57.295779513;
  sp.low_speed_delay_ms = n["low_speed_delay_ms"].as<double>(100.0);
  sp.high_speed_delay_ms = n["high_speed_delay_ms"].as<double>(70.0);
  sp.decision_speed = n["decision_speed"].as<double>(25.0);
  sp.max_iter = n["max_iter"].as<int>(10);
  sp.iter_converge_ms = n["iter_converge_ms"].as<double>(1.0);
  sp.max_approaching_angle = n["max_approaching_angle"].as<double>(1.047);  // 60 deg
  sp.max_leaving_angle = n["max_leaving_angle"].as<double>(0.349);          // 20 deg

  // ── 弹速默认值 ─────────────────────────────────────────────────────────
  impl_->bullet_speed = n["bullet_speed"].as<double>(23.0);

  // ── 实例化算法组件 ─────────────────────────────────────────────────────
  impl_->tracker = detail::EkfTracker(tp);
  impl_->solver = detail::TrajectorySolver(sp);

  impl_->initialized = true;
  MV_LOG_INFO("EkfPredictor", "Init OK (min_detect={}, bullet_speed={:.1f} m/s)",
              tp.min_detect_count, impl_->bullet_speed);
  return true;
}

// ── SetGimbalOrientation ─────────────────────────────────────────────────────

void EkfPredictor::SetGimbalOrientation(const Eigen::Quaterniond& q) {
  // q 为"从云台系到 IMU 绝对系（世界系）"的旋转四元数
  // xyz_world = R_gimbal2world · xyz_gimbal
  impl_->R_gimbal2world = q.normalized().toRotationMatrix();
}

// ── Predict ──────────────────────────────────────────────────────────────────

GimbalControl EkfPredictor::Predict(const std::vector<Detection>& detections,
                                    std::chrono::steady_clock::time_point timestamp,
                                    ArmorColor enemy_color) {
  // 1. 将 xyz_in_gimbal 旋转到世界系，存回 xyz_in_gimbal 字段
  //    （EkfTrackTarget / EkfTracker 约定：传入的 xyz_in_gimbal 已是世界系）
  std::vector<Detection> world_dets;
  world_dets.reserve(detections.size());
  for (Detection det : detections) {
    if (!det.is_solved)
      continue;  // 未完成 PnP 解算的板子无 3D 坐标
    det.xyz_in_gimbal = impl_->R_gimbal2world * det.xyz_in_gimbal;
    // yaw_angle 也需要旋转（偏航角加 R 的 yaw 分量）
    // 取旋转矩阵对应的 yaw（绕 Z 轴的分量）
    const double world_yaw = std::atan2(impl_->R_gimbal2world(1, 0), impl_->R_gimbal2world(0, 0));
    det.yaw_angle += world_yaw;
    world_dets.push_back(std::move(det));
  }

  // 2. 调用 EkfTracker 状态机
  const std::optional<detail::EkfTrackTarget> maybe_target =
      impl_->tracker.Track(world_dets, timestamp, enemy_color);

  // 3. 无追踪目标
  if (!maybe_target.has_value()) {
    impl_->last_track_target.is_tracking = false;
    impl_->last_track_target.tracker_state =
        std::string(detail::EkfTracker::StateName(impl_->tracker.GetState()));
    impl_->last_track_target.tracker_lost_reason =
      std::string(impl_->tracker.GetLastLostReasonName());
    impl_->last_track_target.armor_positions.clear();
    GimbalControl ctrl{};
    ctrl.tracking = false;
    ctrl.fire = false;
    ctrl.timestamp = timestamp;
    return ctrl;
  }

  // 4. 弹道求解（操作副本，不修改 tracker 内部状态）
  GimbalControl ctrl = impl_->solver.Solve(*maybe_target, timestamp, impl_->bullet_speed);

  // 5. 更新 last_track_target（供 GetTrackTarget() 使用）
  const Eigen::VectorXd& x = maybe_target->ekf_x();
  impl_->last_track_target.is_tracking = ctrl.tracking;
  impl_->last_track_target.number = maybe_target->name;
  impl_->last_track_target.color = enemy_color;
  impl_->last_track_target.yaw_predicted = ctrl.yaw;
  impl_->last_track_target.pitch_predicted = ctrl.pitch;
  impl_->last_track_target.tracker_state =
      std::string(detail::EkfTracker::StateName(impl_->tracker.GetState()));
  impl_->last_track_target.tracker_lost_reason = "none";
  // position = 旋转中心（世界系，cx/cy/cz 分别在 x[0]/x[2]/x[4]）
  impl_->last_track_target.position = Eigen::Vector3d(x[0], x[2], x[4]);
  // velocity = 旋转中心速度（dcx/dcy/dcz 在 x[1]/x[3]/x[5]）
  impl_->last_track_target.velocity = Eigen::Vector3d(x[1], x[3], x[5]);
  // armor_positions = EKF 估计的所有装甲板世界坐标 [x,y,z,yaw]
  impl_->last_track_target.armor_positions = maybe_target->ArmorXyzaList();

  return ctrl;
}

// ── GetTrackTarget / Reset / IsInitialized ────────────────────────────────────

TrackTarget EkfPredictor::GetTrackTarget() const {
  return impl_->last_track_target;
}

void EkfPredictor::Reset() {
  impl_->tracker.Reset();
  impl_->last_track_target = TrackTarget{};
}

bool EkfPredictor::IsInitialized() const noexcept {
  return impl_->initialized;
}

// ── 工厂注册 ─────────────────────────────────────────────────────────────────

namespace {
const bool EKF_PREDICTOR_REGISTERED = [] {
  ::mv::Factory<::mv::IPredictor>::Register("ekf", [] { return std::make_unique<EkfPredictor>(); });
  return true;
}();
}  // namespace

}  // namespace mv::modules
