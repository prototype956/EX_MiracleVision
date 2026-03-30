/**
 * @file ekf_tracker_set_target_test.cpp
 * @brief Card 0-5 RED: 验证 EkfTracker::SetTarget 分支优先级
 */

#include "modules/ekf_predictor/detail/ekf_tracker.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include <Eigen/Dense>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

mv::Detection MakeDetection(mv::ArmorNumber number, mv::ArmorType type = mv::ArmorType::SMALL) {
  mv::Detection detection;
  detection.is_solved = true;
  detection.color = mv::ArmorColor::RED;
  detection.number = number;
  detection.type = type;
  detection.xyz_in_gimbal = Eigen::Vector3d(1.0, 0.0, 2.0);
  detection.yaw_angle = 0.0;
  detection.distance_to_center = 1.0;
  return detection;
}

}  // namespace

int main() {
  using mv::modules::detail::EkfTracker;
  using mv::modules::detail::EkfTrackerParams;

  auto now = std::chrono::steady_clock::now();

  // 用一个明显不同的全局 P0，检验 outpost 分支是否被错误覆盖。
  EkfTrackerParams params;
  params.min_detect_count = 1;
  params.P0_diag = Eigen::VectorXd::Constant(11, 9.0);

  EkfTracker tracker(params);
  std::vector<mv::Detection> outpost_dets{MakeDetection(mv::ArmorNumber::OUTPOST)};

  auto maybe_target = tracker.Track(outpost_dets, now, mv::ArmorColor::RED);
  if (!AssertOrFail(maybe_target.has_value(), "tracker should create target for outpost detection")) {
    return 1;
  }

  double p_radius = maybe_target->ekf().P(8, 8);
  if (!AssertOrFail(std::abs(p_radius - 1.0e-4) < 1.0e-8,
                    "outpost branch P0 should take priority over global P0_diag")) {
    return 1;
  }

  std::cout << "[PASS] ekf_tracker_set_target_test\n";
  return 0;
}
