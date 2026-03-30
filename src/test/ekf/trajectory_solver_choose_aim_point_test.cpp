/**
 * @file trajectory_solver_choose_aim_point_test.cpp
 * @brief Card 0-7 RED: 验证 ChooseAimPoint 双候选防抖锁定语义
 */

#include <sstream>

#define private public
#include "modules/ekf_predictor/detail/trajectory_solver.hpp"
#include "modules/ekf_predictor/detail/ekf_track_target.hpp"
#undef private

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

mv::Detection MakeDetection() {
  mv::Detection det;
  det.is_solved = true;
  det.color = mv::ArmorColor::RED;
  det.number = mv::ArmorNumber::THREE;
  det.type = mv::ArmorType::SMALL;
  det.xyz_in_gimbal = Eigen::Vector3d(4.0, 0.0, 0.2);
  det.yaw_angle = 0.0;
  det.distance_to_center = 1.0;
  return det;
}

int FindArmorIdByYaw(const std::vector<Eigen::Vector4d>& armors, const Eigen::Vector4d& chosen) {
  int best_id = -1;
  double best_diff = 1e9;
  for (int index = 0; index < static_cast<int>(armors.size()); ++index) {
    double diff = std::abs(armors[index][3] - chosen[3]);
    if (diff < best_diff) {
      best_diff = diff;
      best_id = index;
    }
  }
  return best_id;
}

mv::modules::detail::EkfTrackTarget MakeTarget(double yaw_angle,
                                               std::chrono::steady_clock::time_point time_point_0) {
  mv::Detection det = MakeDetection();
  det.yaw_angle = yaw_angle;

  Eigen::VectorXd p0_diag(11);
  p0_diag << 1, 64, 1, 64, 1, 64, 0.4, 100, 1, 1, 1;

  mv::modules::detail::EkfTrackTarget target(
      det, time_point_0, 0.27, 4, p0_diag, mv::modules::detail::ProcessNoiseParams{});
  target.jumped = true;
  return target;
}

}  // namespace

int main() {
  using mv::modules::detail::EkfTrackTarget;
  using mv::modules::detail::TrajectorySolverParams;
  using mv::modules::detail::TrajectorySolver;

  TrajectorySolverParams solver_params;
  solver_params.max_approaching_angle = 2.0;
  TrajectorySolver solver(solver_params);

  auto time_point_0 = std::chrono::steady_clock::now();
  EkfTrackTarget target = MakeTarget(0.0, time_point_0);
  target.jumped = true;

  // 第一次选择：alpha=0，记录当前选择的 id。
  target.ekf_.x[6] = 0.0;
  auto aim_first = solver.ChooseAimPoint(target, 0.0);
  if (!AssertOrFail(aim_first.valid, "first choose should be valid")) {
    return 1;
  }
  auto armors_first = target.ArmorXyzaList();
  int first_id = FindArmorIdByYaw(armors_first, aim_first.xyza);
  if (!AssertOrFail(first_id >= 0, "first aim id should be found")) {
    return 1;
  }

  // 第二次选择：调整 alpha 到 -0.8，此时另一个候选会更优。
  // 未实现 lock_id 防抖时会切换 id（RED）。
  target.ekf_.x[6] = -0.8;
  auto aim_second = solver.ChooseAimPoint(target, 0.0);
  if (!AssertOrFail(aim_second.valid, "second choose should be valid")) {
    return 1;
  }
  auto armors_second = target.ArmorXyzaList();
  int second_id = FindArmorIdByYaw(armors_second, aim_second.xyza);
  if (!AssertOrFail(second_id >= 0, "second aim id should be found")) {
    return 1;
  }

  if (!AssertOrFail(second_id == first_id,
                    "lock behavior expected: id should stay stable in dual-candidate window")) {
    return 1;
  }

  std::cout << "[PASS] trajectory_solver_choose_aim_point_test\n";
  return 0;
}
