/**
 * @file ekf_predictor_init_config_test.cpp
 * @brief Card 0-6 RED: 验证 EkfPredictor::Init 对 ROOT_CFG 路径读取
 */

#include "modules/ekf_predictor/ekf_predictor.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

mv::Detection MakeOutpostDetection() {
  mv::Detection detection;
  detection.is_solved = true;
  detection.color = mv::ArmorColor::RED;
  detection.number = mv::ArmorNumber::OUTPOST;
  detection.type = mv::ArmorType::SMALL;
  detection.xyz_in_gimbal = Eigen::Vector3d(1.0, 0.0, 2.0);
  detection.yaw_angle = 0.0;
  detection.distance_to_center = 1.0;
  return detection;
}

double EstimateRadiusFromTrackTarget(const mv::TrackTarget& target) {
  if (target.armor_positions.empty()) {
    return -1.0;
  }
  const Eigen::Vector4d& armor0 = target.armor_positions.front();
  double delta_x = armor0[0] - target.position[0];
  double delta_y = armor0[1] - target.position[1];
  return std::sqrt(delta_x * delta_x + delta_y * delta_y);
}

}  // namespace

int main() {
  YAML::Node root;
  root["auto_aim"]["ekf_predictor"]["min_detect_count"] = 1;
  root["auto_aim"]["ekf_predictor"]["init_radius_outpost"] = 0.58;

  if (!AssertOrFail(root["auto_aim"]["ekf_predictor"]["init_radius_outpost"].IsDefined(),
                    "test YAML should contain auto_aim.ekf_predictor.init_radius_outpost")) {
    return 1;
  }

  mv::modules::EkfPredictor predictor;
  if (!AssertOrFail(predictor.Init(root), "EkfPredictor::Init should succeed with root config")) {
    return 1;
  }

  std::vector<mv::Detection> detections{MakeOutpostDetection()};
  auto now = std::chrono::steady_clock::now();
  (void)predictor.Predict(detections, now, mv::ArmorColor::RED);

  mv::TrackTarget track_target = predictor.GetTrackTarget();
  if (!AssertOrFail(!track_target.armor_positions.empty(),
                    "track target should contain armor list")) {
    return 1;
  }

  double estimated_radius = EstimateRadiusFromTrackTarget(track_target);
  if (!AssertOrFail(std::abs(estimated_radius - 0.58) < 1e-3,
                    "init_radius_outpost from auto_aim.ekf_predictor should take effect")) {
    std::cerr << "[INFO] estimated_radius=" << estimated_radius << " expected=0.58\n";
    return 1;
  }

  std::cout << "[PASS] ekf_predictor_init_config_test\n";
  return 0;
}
