/**
 * @file ekf_track_target_update_test.cpp
 * @brief Card 0-1 RED: 验证 EkfTrackTarget::Update 需要消费 yaw 观测
 */

#include "modules/ekf_predictor/detail/ekf_track_target.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

#include <Eigen/Dense>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

mv::Detection MakeDetection(double x, double y, double z, double yaw) {
  mv::Detection det;
  det.is_solved = true;
  det.number = mv::ArmorNumber::THREE;
  det.type = mv::ArmorType::BIG;
  det.xyz_in_gimbal = Eigen::Vector3d(x, y, z);
  det.yaw_angle = yaw;
  det.pitch_angle = 0.0;
  det.distance_to_center = 0.0;
  return det;
}

}  // namespace

int main() {
  using mv::modules::detail::EkfTrackTarget;

  const auto T0 = std::chrono::steady_clock::now();

  // 初始化时 yaw=0，构造出的状态角 alpha 也应接近 0。
  const mv::Detection INIT_DET = MakeDetection(1.0, 0.0, 2.0, 0.0);
  Eigen::VectorXd p0_diag(11);
  p0_diag << 1, 64, 1, 64, 1, 64, 0.4, 100, 1, 1, 1;
  EkfTrackTarget target(INIT_DET, T0, 0.27, 2, p0_diag);

  const double alpha_before = target.ekf_x()[6];

  // 观测 xyz 不变，只改变观测 yaw。
  // 目标：若 Update 使用 ypd+yaw 观测，alpha 应发生可见更新。
  mv::Detection yaw_changed = INIT_DET;
  yaw_changed.yaw_angle = 0.8;
  yaw_changed.pitch_angle = 0.2;

  target.Update(yaw_changed);

  const double alpha_after = target.ekf_x()[6];
  const double delta = std::abs(alpha_after - alpha_before);

  if (!AssertOrFail(delta > 1e-3, "alpha should change when only observed yaw/pitch changes")) {
    return 1;
  }

  // Card 0-3 RED: 当两个候选装甲板角误差相等时，应优先保持 last_id，避免抖动切换。
  // 这里构造 armor_num=2，alpha 约为 0 时，观测 yaw=pi/2 会使 id=0 与 id=1 误差相等。
  EkfTrackTarget tie_target(INIT_DET, T0, 0.27, 2, p0_diag);
  tie_target.last_id = 1;

  mv::Detection tie_det = INIT_DET;
  tie_det.yaw_angle = M_PI / 2.0;
  tie_target.Update(tie_det);

  if (!AssertOrFail(tie_target.last_id == 1,
                    "tie-break should prefer last_id when errors are equal")) {
    return 1;
  }

  // Card 0-4 RED: 过程噪声应由注入参数决定，而不是硬编码常量。
  // 构造两个 outpost 目标，仅 outpost 角噪声不同，预测后协方差应出现可见差异。
  mv::Detection outpost_det = MakeDetection(1.0, 0.0, 2.0, 0.0);
  outpost_det.number = mv::ArmorNumber::OUTPOST;

    const mv::modules::detail::ProcessNoiseParams low_noise_cfg{
      100.0, 400.0, 10.0, 0.1};
    const mv::modules::detail::ProcessNoiseParams high_noise_cfg{
      100.0, 400.0, 10.0, 1000.0};

    EkfTrackTarget low_noise_target(outpost_det, T0, 0.27, 3, p0_diag, low_noise_cfg);
    EkfTrackTarget high_noise_target(outpost_det, T0, 0.27, 3, p0_diag, high_noise_cfg);

  low_noise_target.Predict(0.05);
  high_noise_target.Predict(0.05);

  const double low_ang_var = low_noise_target.ekf().P(7, 7);
  const double high_ang_var = high_noise_target.ekf().P(7, 7);
  if (!AssertOrFail(high_ang_var > low_ang_var + 1e-6,
                    "injected outpost angular noise should change covariance growth")) {
    return 1;
  }

  std::cout << "[PASS] ekf_track_target_update_test\n";
  return 0;
}
