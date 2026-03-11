/**
 * @file trajectory_solver.cpp
 * @brief 飞行时间迭代求解器实现（Stage 8-B）
 *
 * 【弹道模型（不考虑空气阻力）】
 *   二次方程：a·tan²θ - d·tanθ + (a+h) = 0
 *   其中 a = g·d²/(2·v₀²)，取飞行时间最短的解。
 *
 * 【参考】sp_vision_25/tasks/auto_aim/aimer.cpp
 */
#include "trajectory_solver.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>

namespace mv::modules::detail {

// ── 内部弹道模型 ──────────────────────────────────────────────────────────────

namespace {

/// 重力加速度（武汉赛场实测，sp 取值）
constexpr double kG = 9.7833;

/// 弹道求解结果
struct Trajectory {
  bool unsolvable{true};
  double fly_time{0.0};
  double pitch{0.0};  ///< 抬头为正（rad）
};

/**
 * @brief 单点弹道求解（不考虑空气阻力）
 *
 * @param v0  初速度（m/s）
 * @param d   目标水平距离（m）
 * @param h   目标竖直高度（m，向上为正）
 */
Trajectory SolveTrajectory(double v0, double d, double h) {
  if (v0 < 1.0 || d < 1e-3)
    return {};

  const double a = kG * d * d / (2.0 * v0 * v0);
  const double b = -d;
  const double c = a + h;
  const double delta = b * b - 4.0 * a * c;

  if (delta < 0.0)
    return {};  // 不可解

  const double tan1 = (-b + std::sqrt(delta)) / (2.0 * a);
  const double tan2 = (-b - std::sqrt(delta)) / (2.0 * a);
  const double pitch1 = std::atan(tan1);
  const double pitch2 = std::atan(tan2);
  const double t1 = d / (v0 * std::cos(pitch1));
  const double t2 = d / (v0 * std::cos(pitch2));

  // 取飞行时间较短的解
  const bool use1 = t1 < t2;
  return {false, use1 ? t1 : t2, use1 ? pitch1 : pitch2};
}

}  // namespace

// ── 构造函数 ─────────────────────────────────────────────────────────────────

TrajectorySolver::TrajectorySolver(const TrajectorySolverParams& params) : params_(params) {}

// ── Solve（主入口）──────────────────────────────────────────────────────────

GimbalControl TrajectorySolver::Solve(const EkfTrackTarget& target,
                                      std::chrono::steady_clock::time_point timestamp,
                                      double bullet_speed) {
  GimbalControl ctrl;
  ctrl.timestamp = timestamp;

  // 弹速保护（过低时退化为固定值，防止弹道求解发散）
  if (bullet_speed < 14.0)
    bullet_speed = 23.0;

  // 1. 根据弹速选择发弹延迟补偿时间
  const double delay_s = (bullet_speed < params_.decision_speed)
                             ? params_.low_speed_delay_ms / 1000.0
                             : params_.high_speed_delay_ms / 1000.0;

  // 2. 计算"当前时刻到发弹"的总延迟：检测器→预测器耗时(≈5ms) + 发弹延迟
  //    以 timestamp 为基准向未来外推
  const double init_dt_s = 0.005 + delay_s;
  auto future = timestamp + std::chrono::microseconds(static_cast<int64_t>(init_dt_s * 1e6));

  // 3. 预测目标状态（操作副本，不修改调用方内的 target）
  EkfTrackTarget predicted_target = target;
  predicted_target.Predict(future);

  // 4. 初次弹道求解（提供初始飞行时间估计）
  const AimPoint aim0 = ChooseAimPoint(predicted_target, 0.0);
  if (!aim0.valid) {
    MV_LOG_DEBUG("TrajectorySolver", "No valid aim point (initial)");
    return ctrl;
  }

  const Eigen::Vector3d xyz0 = aim0.xyza.head<3>();
  const double d0 = std::sqrt(xyz0[0] * xyz0[0] + xyz0[1] * xyz0[1]);
  Trajectory traj = SolveTrajectory(bullet_speed, d0, xyz0[2]);

  if (traj.unsolvable) {
    MV_LOG_DEBUG("TrajectorySolver", "Unsolvable initial trajectory d={:.2f} z={:.2f}", d0,
                 xyz0[2]);
    return ctrl;
  }

  // 5. 迭代收敛飞行时间（最多 max_iter 次）
  const double converge_s = params_.iter_converge_ms / 1000.0;
  AimPoint final_aim = aim0;

  for (int iter = 0; iter < params_.max_iter; ++iter) {
    // 以"当前未来时刻 + 上一次飞行时间"预测目标位置
    const auto predict_t =
        future + std::chrono::microseconds(static_cast<int64_t>(traj.fly_time * 1e6));

    EkfTrackTarget iter_target = target;
    iter_target.Predict(predict_t);

    const AimPoint aim_i = ChooseAimPoint(iter_target, 0.0);
    if (!aim_i.valid) {
      MV_LOG_DEBUG("TrajectorySolver", "No valid aim point at iter {}", iter);
      return ctrl;
    }

    const Eigen::Vector3d xyz_i = aim_i.xyza.head<3>();
    const double d_i = std::sqrt(xyz_i[0] * xyz_i[0] + xyz_i[1] * xyz_i[1]);
    const Trajectory traj_new = SolveTrajectory(bullet_speed, d_i, xyz_i[2]);

    if (traj_new.unsolvable) {
      MV_LOG_DEBUG("TrajectorySolver", "Unsolvable at iter {} d={:.2f}", iter, d_i);
      return ctrl;
    }

    final_aim = aim_i;

    if (std::abs(traj_new.fly_time - traj.fly_time) < converge_s)
      break;  // 收敛

    traj = traj_new;
  }

  // 6. 计算最终 yaw / pitch 并加偏置
  const Eigen::Vector3d final_xyz = final_aim.xyza.head<3>();
  const double yaw = std::atan2(final_xyz[1], final_xyz[0]) + params_.yaw_offset_rad;
  const double pitch = -(traj.pitch + params_.pitch_offset_rad);  // 世界系向上为负

  ctrl.tracking = true;
  ctrl.yaw = yaw;
  ctrl.pitch = pitch;
  ctrl.distance = final_xyz.norm();
  ctrl.fire = false;  // fire 由 Voter 决定

  debug_aim_point_ = final_aim;
  return ctrl;
}

// ── ChooseAimPoint ───────────────────────────────────────────────────────────

AimPoint TrajectorySolver::ChooseAimPoint(const EkfTrackTarget& target, double current_yaw) {
  (void)current_yaw;  // 保留参数供未来接入云台实际 yaw
  const std::vector<Eigen::Vector4d> xyza_list = target.ArmorXyzaList();
  if (xyza_list.empty())
    return {};

  // 如果目标尚未发生跳变，直接选 id=0 的装甲板
  if (!target.jumped)
    return {true, xyza_list[0]};

  const Eigen::VectorXd& x = target.ekf_x();
  // 整车旋转中心在世界系 xy 平面的投影角（cloud→center 方向）
  const double center_yaw = std::atan2(x[2], x[0]);  // x[2]=cy, x[0]=cx

  const int n = static_cast<int>(xyza_list.size());

  // 计算每块装甲板相对旋转中心的 delta_angle
  std::vector<double> delta_angles(n);
  for (int i = 0; i < n; ++i) {
    // limit_rad
    double da = xyza_list[i][3] - center_yaw;
    while (da > M_PI)
      da -= 2.0 * M_PI;
    while (da <= -M_PI)
      da += 2.0 * M_PI;
    delta_angles[i] = da;
  }

  // 非小陀螺（角速度 |dα| <= 2 且非前哨站）：选在可射击范围内、delta_angle 绝对值最小的装甲板
  if (std::abs(x[7]) <= 2.0 && target.name != ArmorNumber::OUTPOST) {
    const double max_angle = params_.max_approaching_angle;
    int best_id = -1;
    double best_err = 1e9;
    for (int i = 0; i < n; ++i) {
      if (std::abs(delta_angles[i]) > max_angle)
        continue;
      if (std::abs(delta_angles[i]) < best_err) {
        best_err = std::abs(delta_angles[i]);
        best_id = i;
      }
    }
    if (best_id < 0)
      return {false, xyza_list[0]};
    return {true, xyza_list[best_id]};
  }

  // 小陀螺模式：选择"正在靠近"的装甲板
  const double coming_angle =
      (target.name == ArmorNumber::OUTPOST) ? 70.0 / 57.3 : params_.max_approaching_angle;
  const double leaving_angle =
      (target.name == ArmorNumber::OUTPOST) ? 30.0 / 57.3 : params_.max_leaving_angle;

  for (int i = 0; i < n; ++i) {
    if (std::abs(delta_angles[i]) > coming_angle)
      continue;
    // dα > 0 = 逆时针，选 delta_angle > -leaving_angle 的装甲板
    if (x[7] > 0.0 && delta_angles[i] < leaving_angle)
      return {true, xyza_list[i]};
    if (x[7] < 0.0 && delta_angles[i] > -leaving_angle)
      return {true, xyza_list[i]};
  }

  return {false, xyza_list[0]};
}

}  // namespace mv::modules::detail
