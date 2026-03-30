/**
 * @file ekf_tracker_state_machine_test.cpp
 * @brief Card 0-9: EkfTracker::Track / StateMachine 行为测试
 */

#include <sstream>

#define private public
#include "modules/ekf_predictor/detail/ekf_tracker.hpp"
#undef private

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

mv::Detection MakeDetection(mv::ArmorNumber number, mv::ArmorType type, double center_x) {
  mv::Detection det;
  det.is_solved = true;
  det.color = mv::ArmorColor::RED;
  det.number = number;
  det.type = type;
  det.xyz_in_gimbal = Eigen::Vector3d(center_x, 0.0, 2.0);
  det.yaw_angle = 0.0;
  det.distance_to_center = 1.0;
  return det;
}

mv::modules::detail::EkfTrackTarget MakeTarget(const mv::Detection& det,
                                               std::chrono::steady_clock::time_point time_point_0) {
  Eigen::VectorXd p0_diag(11);
  p0_diag << 1, 64, 1, 64, 1, 64, 0.4, 100, 1, 1, 1;
  return mv::modules::detail::EkfTrackTarget(det, time_point_0, 0.27, 4, p0_diag,
                                             mv::modules::detail::ProcessNoiseParams{});
}

}  // namespace

int main() {
  using mv::modules::detail::EkfTracker;
  using mv::modules::detail::EkfTrackerParams;

  auto now = std::chrono::steady_clock::now();

  // Case 1: 大 dt 应直接 reset 到 LOST。
  {
    EkfTrackerParams params;
    params.max_dt_sec = 0.1;
    EkfTracker tracker(params);

    mv::Detection normal_det = MakeDetection(mv::ArmorNumber::THREE, mv::ArmorType::SMALL, 1.0);
    tracker.target_ = MakeTarget(normal_det, now);
    tracker.state_ = EkfTracker::State::TRACKING;
    tracker.last_t_ = now - std::chrono::milliseconds(400);

    auto maybe_target = tracker.Track({}, now, mv::ArmorColor::RED);
    if (!AssertOrFail(!maybe_target.has_value(), "large dt should return nullopt")) {
      return 1;
    }
    if (!AssertOrFail(tracker.state_ == EkfTracker::State::LOST,
                      "large dt should reset tracker state to LOST")) {
      return 1;
    }
  }

  // Case 2: outpost 的 temp_lost 容忍应高于普通目标。
  {
    EkfTrackerParams params;
    params.max_temp_lost_count = 2;
    params.outpost_max_temp_lost_count = 4;
    EkfTracker tracker(params);

    mv::Detection outpost_det = MakeDetection(mv::ArmorNumber::OUTPOST, mv::ArmorType::SMALL, 1.2);
    tracker.target_ = MakeTarget(outpost_det, now);
    tracker.state_ = EkfTracker::State::TRACKING;

    tracker.StateMachine(false);  // TRACKING -> TEMP_LOST
    if (!AssertOrFail(tracker.state_ == EkfTracker::State::TEMP_LOST,
                      "tracking miss should enter TEMP_LOST")) {
      return 1;
    }
    if (!AssertOrFail(tracker.max_temp_lost_ == params.outpost_max_temp_lost_count,
                      "outpost should use outpost_max_temp_lost_count")) {
      return 1;
    }

    tracker.StateMachine(false);  // temp_lost_count=2
    tracker.StateMachine(false);  // temp_lost_count=3
    tracker.StateMachine(false);  // temp_lost_count=4
    if (!AssertOrFail(tracker.state_ == EkfTracker::State::TEMP_LOST,
                      "outpost should still remain TEMP_LOST before exceeding limit")) {
      return 1;
    }

    tracker.StateMachine(false);  // temp_lost_count=5 -> LOST
    if (!AssertOrFail(tracker.state_ == EkfTracker::State::LOST,
                      "outpost should fall to LOST only after exceeding outpost limit")) {
      return 1;
    }
  }

  // Case 3: UpdateTarget 需同时匹配 number 和 type（与 SP 口径一致）。
  {
    EkfTrackerParams params;
    EkfTracker tracker(params);

    mv::Detection small_det = MakeDetection(mv::ArmorNumber::THREE, mv::ArmorType::SMALL, 1.0);
    tracker.target_ = MakeTarget(small_det, now);

    std::vector<mv::Detection> wrong_type_only{
        MakeDetection(mv::ArmorNumber::THREE, mv::ArmorType::BIG, 1.1)};

    bool updated = tracker.UpdateTarget(wrong_type_only, now + std::chrono::milliseconds(10));
    if (!AssertOrFail(!updated,
                      "UpdateTarget should reject same-number but different-type detections")) {
      return 1;
    }
  }

  std::cout << "[PASS] ekf_tracker_state_machine_test\n";
  return 0;
}
