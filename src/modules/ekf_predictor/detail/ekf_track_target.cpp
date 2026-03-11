/**
 * @file ekf_track_target.cpp
 * @brief 单目标 EKF 跟踪状态实现（Stage 8-B）
 *
 * 【坐标系约定】
 *   所有传入的 Detection.xyz_in_gimbal 在 EkfPredictor::Predict()（Stage 8-D）
 *   中经 R_gimbal2world 旋转后赋回 xyz_in_gimbal 字段再送入此处，
 *   因此本文件内所有 xyz_in_gimbal 均视为世界坐标系坐标。
 *
 * 【状态向量（11 维）】
 *   x = [cx, dcx, cy, dcy, cz, dcz, α, dα, r, Δl, Δh]
 *        0    1    2    3   4    5   6   7  8   9  10
 *
 * 【观测空间】
 *   与 sp 不同，本实现使用装甲板的直角坐标 [x, y, z] 作为观测量，
 *   避免引入 xyz2ypd 的额外 Jacobian 链，简化数学推导。
 *
 * 【参考】sp_vision_25/tasks/auto_aim/target.cpp
 */
#include "ekf_track_target.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mv::modules::detail {

// ── 内部工具 ─────────────────────────────────────────────────────────────────

namespace {
/// 将角度限制到 (-π, π]
inline double LimitRad(double angle) {
  while (angle > M_PI)
    angle -= 2.0 * M_PI;
  while (angle <= -M_PI)
    angle += 2.0 * M_PI;
  return angle;
}

/// 发散检测：半径范围 [0.05 m, 0.5 m] 之内认为合理
constexpr double kRadiusMin = 0.05;
constexpr double kRadiusMax = 0.60;

/// 收敛所需最少 Update 次数
constexpr int kConvergeCountNormal = 3;
constexpr int kConvergeCountOutpost = 10;
}  // namespace

// ── 构造函数 ─────────────────────────────────────────────────────────────────

EkfTrackTarget::EkfTrackTarget(const Detection& detection, std::chrono::steady_clock::time_point t,
                               double radius, int armor_num, const Eigen::VectorXd& P0_diag)
    : name(detection.number),
      armor_type(detection.type),
      is_init(true),
      armor_num_(armor_num),
      t_(t) {
  // 1. 从检测结果取装甲板 3D 位置（世界系）和 yaw
  const double ax = detection.xyz_in_gimbal[0];
  const double ay = detection.xyz_in_gimbal[1];
  const double az = detection.xyz_in_gimbal[2];
  const double yaw = detection.yaw_angle;  // 装甲板法向 yaw（世界系）

  // 2. 从装甲板位置推算旋转中心（sp 约定：center = armor + r * [cos,sin,0]）
  const double cx = ax + radius * std::cos(yaw);
  const double cy = ay + radius * std::sin(yaw);

  // 3. 初始状态向量（零速，零角速度，无大小板差）
  Eigen::VectorXd x0(11);
  x0 << cx, 0.0, cy, 0.0, az, 0.0, yaw, 0.0, radius, 0.0, 0.0;

  // 4. 初始协方差
  Eigen::MatrixXd P0 = P0_diag.asDiagonal();

  // 5. 角度安全加法（防止 α 越过 ±π 折叠）
  auto x_add = [](const Eigen::VectorXd& a, const Eigen::VectorXd& b) -> Eigen::VectorXd {
    Eigen::VectorXd c = a + b;
    c[6] = LimitRad(c[6]);
    return c;
  };

  ekf_ = tool::ExtendedKalmanFilter(x0, P0, x_add);
}

// ── 时间预测步 ───────────────────────────────────────────────────────────────

void EkfTrackTarget::Predict(std::chrono::steady_clock::time_point t) {
  const double dt = std::chrono::duration<double>(t - t_).count();
  t_ = t;
  Predict(dt);
}

void EkfTrackTarget::Predict(double dt) {
  // ── 状态转移矩阵 F（11×11，CV 模型）────────────────────────────────────
  // clang-format off
  Eigen::MatrixXd F = Eigen::MatrixXd::Identity(11, 11);
  F(0, 1) = dt;   // cx += dcx * dt
  F(2, 3) = dt;   // cy += dcy * dt
  F(4, 5) = dt;   // cz += dcz * dt
  F(6, 7) = dt;   // α  += dα  * dt
  // clang-format on

  // ── 分段白噪声模型 Q（PWNM）──────────────────────────────────────────────
  // v1: 线性加速度方差（m²/s⁴），v2: 角加速度方差（rad²/s⁴）
  double v1, v2;
  if (name == ArmorNumber::OUTPOST) {
    v1 = 10.0;
    v2 = 0.1;
  } else {
    v1 = 100.0;
    v2 = 400.0;
  }

  const double dt2 = dt * dt;
  const double dt3 = dt2 * dt;
  const double dt4 = dt3 * dt;

  Eigen::MatrixXd Q = Eigen::MatrixXd::Zero(11, 11);
  // 位置-速度对（cx/dcx, cy/dcy, cz/dcz）
  for (int i : {0, 2, 4}) {
    Q(i, i) = dt4 / 4.0 * v1;
    Q(i, i + 1) = dt3 / 2.0 * v1;
    Q(i + 1, i) = dt3 / 2.0 * v1;
    Q(i + 1, i + 1) = dt2 * v1;
  }
  // 角度-角速度对（α/dα）
  Q(6, 6) = dt4 / 4.0 * v2;
  Q(6, 7) = dt3 / 2.0 * v2;
  Q(7, 6) = dt3 / 2.0 * v2;
  Q(7, 7) = dt2 * v2;
  // r, Δl, Δh 设为 0（认为半径和高度差基本不变）

  // 非线性预测函数：对 CV 而言 f(x) = F*x，但需要 wrap 角度
  auto f = [&](const Eigen::VectorXd& x) -> Eigen::VectorXd {
    Eigen::VectorXd xp = F * x;
    xp[6] = LimitRad(xp[6]);
    return xp;
  };

  // 前哨站角速度饱和（防止 EKF 发散后角速度估计无界增长）
  if (name == ArmorNumber::OUTPOST && std::abs(ekf_.x[7]) > 2.51) {
    ekf_.x[7] = ekf_.x[7] > 0.0 ? 2.51 : -2.51;
  }

  ekf_.Predict(F, Q, f);
}

// ── 观测更新步 ───────────────────────────────────────────────────────────────

void EkfTrackTarget::Update(const Detection& detection) {
  update_count_++;

  // 1. 观测量 z = [armor_x, armor_y, armor_z]（世界系）
  const Eigen::Vector3d z = detection.xyz_in_gimbal;

  // 2. 从 z 推算观测角（世界系：从旋转中心指向装甲板的方向 = center→armor → 取反 = armor→center）
  //    sp 约定：armor = center - r*[cos,sin] ，所以观测角 = atan2(cy-ay, cx-ax)
  const double obs_yaw = std::atan2(ekf_.x[2] - z[1], ekf_.x[0] - z[0]);

  // 3. 匹配最近装甲板 ID
  const int id = SelectNearestArmor(ekf_.x, obs_yaw);

  // 4. 跳变检测
  if (id != last_id) {
    is_switch_ = true;
    switch_count_++;
    jumped = (switch_count_ > 0);
  } else {
    is_switch_ = false;
  }
  last_id = id;

  // 5. 观测函数 h: R^11 → R^3
  auto h = [&](const Eigen::VectorXd& x) -> Eigen::VectorXd { return ArmorXyz(x, id); };

  // 6. 观测噪声矩阵 R（自适应：距离越远噪声越大）
  const double dist = z.norm();
  const double sigma = 0.01 * dist + 0.005;  // 简单线性模型 ~cm/m
  const double var = sigma * sigma;
  const Eigen::Vector3d R_diag{var, var, var * 4.0};  // z 轴精度略低
  const Eigen::MatrixXd R = R_diag.asDiagonal();

  // 7. 调用 EKF Update（xyz 空间无角度循环，直接相减）
  ekf_.Update(
      z, HJacobian(ekf_.x, id), R, h,
      [](const Eigen::VectorXd& a, const Eigen::VectorXd& b) -> Eigen::VectorXd { return a - b; });
}

// ── 状态查询 ─────────────────────────────────────────────────────────────────

Eigen::VectorXd EkfTrackTarget::ekf_x() const {
  return ekf_.x;
}

const tool::ExtendedKalmanFilter& EkfTrackTarget::ekf() const {
  return ekf_;
}

std::vector<Eigen::Vector4d> EkfTrackTarget::ArmorXyzaList() const {
  std::vector<Eigen::Vector4d> list;
  list.reserve(static_cast<size_t>(armor_num_));
  for (int i = 0; i < armor_num_; ++i) {
    const Eigen::Vector3d xyz = ArmorXyz(ekf_.x, i);
    const double angle = LimitRad(ekf_.x[6] + i * 2.0 * M_PI / armor_num_);
    list.push_back({xyz[0], xyz[1], xyz[2], angle});
  }
  return list;
}

// ── 发散 / 收敛检测 ──────────────────────────────────────────────────────────

bool EkfTrackTarget::Diverged() const {
  // 方案 1：半径物理约束（最直观）
  const double r = ekf_.x[8];
  const double r2 = armor_num_ == 4 ? r + ekf_.x[9] : r;
  const bool r_ok = r >= kRadiusMin && r <= kRadiusMax;
  const bool r2_ok = r2 >= kRadiusMin && r2 <= kRadiusMax;
  return !(r_ok && r2_ok);
}

bool EkfTrackTarget::Converged() const {
  if (is_converged_)
    return true;  // 缓存，避免重复检查

  const int threshold =
      (name == ArmorNumber::OUTPOST) ? kConvergeCountOutpost : kConvergeCountNormal;
  if (update_count_ >= threshold && !Diverged()) {
    is_converged_ = true;
    return true;
  }
  return false;
}

// ── 内部辅助 ─────────────────────────────────────────────────────────────────

Eigen::Vector3d EkfTrackTarget::ArmorXyz(const Eigen::VectorXd& x, int id) const {
  // armor_angle = α + id * 2π/N（sp 约定：center → armor 方向的负值）
  const double angle = LimitRad(x[6] + id * 2.0 * M_PI / armor_num_);

  // 4 板车：奇数 id（1, 3）使用"侧板"参数（半径 r+Δl，高度 cz+Δh）
  const bool use_side = (armor_num_ == 4) && (id == 1 || id == 3);
  const double r = use_side ? x[8] + x[9] : x[8];

  // sp 约定：armor = center - r * [cos, sin, 0]
  return {x[0] - r * std::cos(angle), x[2] - r * std::sin(angle), use_side ? x[4] + x[10] : x[4]};
}

Eigen::MatrixXd EkfTrackTarget::HJacobian(const Eigen::VectorXd& x, int id) const {
  // h(x) = [cx - r*cos(α'), cy_state - r*sin(α'), cz_state ± Δh]
  // 其中 α' = α + id*2π/N，cy_state = x[2]，cz_state = x[4]
  // ∂h/∂x（3×11）：
  const double angle = LimitRad(x[6] + id * 2.0 * M_PI / armor_num_);
  const bool use_side = (armor_num_ == 4) && (id == 1 || id == 3);
  const double r = use_side ? x[8] + x[9] : x[8];

  const double cos_a = std::cos(angle);
  const double sin_a = std::sin(angle);

  // ∂armor_x/∂α = r*sin(α)，  ∂armor_x/∂r = -cos(α)，  ∂armor_x/∂Δl = -cos(α) if use_side
  // ∂armor_y/∂α = -r*cos(α)，  ∂armor_y/∂r = -sin(α)，  ∂armor_y/∂Δl = -sin(α) if use_side
  // ∂armor_z/∂Δh = 1 if use_side else 0
  // clang-format off
  Eigen::MatrixXd H = Eigen::MatrixXd::Zero(3, 11);
  //              cx   dcx  cy   dcy  cz   dcz     α            dα     r           Δl                    Δh
  H(0, 0)  = 1.0;                                        // ∂ax/∂cx
  H(0, 6)  =  r * sin_a;                                 // ∂ax/∂α
  H(0, 8)  = -cos_a;                                     // ∂ax/∂r
  H(0, 9)  = use_side ? -cos_a : 0.0;                    // ∂ax/∂Δl

  H(1, 2)  = 1.0;                                        // ∂ay/∂cy
  H(1, 6)  = -r * cos_a;                                 // ∂ay/∂α
  H(1, 8)  = -sin_a;                                     // ∂ay/∂r
  H(1, 9)  = use_side ? -sin_a : 0.0;                    // ∂ay/∂Δl

  H(2, 4)  = 1.0;                                        // ∂az/∂cz
  H(2, 10) = use_side ? 1.0 : 0.0;                       // ∂az/∂Δh
  // clang-format on

  return H;
}

int EkfTrackTarget::SelectNearestArmor(const Eigen::VectorXd& x, double observed_yaw) const {
  int best_id = 0;
  double min_err = 1e9;

  for (int i = 0; i < armor_num_; ++i) {
    const double pred_angle = LimitRad(x[6] + i * 2.0 * M_PI / armor_num_);
    const double err = std::abs(LimitRad(observed_yaw - pred_angle));
    if (err < min_err) {
      min_err = err;
      best_id = i;
    }
  }
  return best_id;
}

}  // namespace mv::modules::detail
