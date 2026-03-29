/**
 * @file pnp_solver_test.cpp
 * @brief PnpSolver 最小测试：覆盖 Init/Solve 的成功与失败路径
 */

#include "modules/pnp_solver/pnp_solver.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <yaml-cpp/yaml.h>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

std::vector<cv::Point2f> ProjectArmorCorners(const cv::Mat& camera_matrix, const cv::Mat& dist) {
  constexpr float HALF_W = 0.0675F;
  constexpr float HALF_H = 0.0275F;
  const std::vector<cv::Point3f> OBJECT_PTS{{-HALF_W, -HALF_H, 0.0F},
                                            {HALF_W, -HALF_H, 0.0F},
                                            {HALF_W, HALF_H, 0.0F},
                                            {-HALF_W, HALF_H, 0.0F}};

  const cv::Vec3d RVEC(0.0, 0.0, 0.0);
  const cv::Vec3d TVEC(0.0, 0.0, 2.0);

  std::vector<cv::Point2f> image_points;
  cv::projectPoints(OBJECT_PTS, RVEC, TVEC, camera_matrix, dist, image_points);
  return image_points;
}

}  // namespace

int main() {
  try {
    mv::modules::PnpSolver solver;

    // 失败路径：未初始化时 Solve 必须返回 false
    mv::Detection uninitialized_detection;
    if (!AssertOrFail(!solver.Solve(uninitialized_detection),
                      "Solve should fail before Init is called")) {
      return 1;
    }

    // 成功路径：加载配置并执行一次稳定可解的 IPPE
    YAML::Node config = YAML::LoadFile(std::string(CONFIG_FILE_PATH) + "/vision.yaml");
    if (!AssertOrFail(solver.Init(config), "Init should succeed with vision.yaml config")) {
      return 1;
    }

    const auto CAMERA_VALUES = config["calibration"]["camera_matrix"].as<std::vector<double>>();
    const auto DIST_VALUES = config["calibration"]["distort_coeffs"].as<std::vector<double>>();

    cv::Mat camera_matrix(3, 3, CV_64F);
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const auto ROW_IDX = static_cast<size_t>(row);
        const auto COL_IDX = static_cast<size_t>(col);
        camera_matrix.at<double>(row, col) = CAMERA_VALUES[ROW_IDX * 3U + COL_IDX];
      }
    }

    cv::Mat dist_coeffs(1, static_cast<int>(DIST_VALUES.size()), CV_64F);
    for (size_t idx = 0; idx < DIST_VALUES.size(); ++idx) {
      dist_coeffs.at<double>(0, static_cast<int>(idx)) = DIST_VALUES[idx];
    }

    mv::Detection detection;
    detection.type = mv::ArmorType::SMALL;

    const auto POINTS = ProjectArmorCorners(camera_matrix, dist_coeffs);
    if (!AssertOrFail(POINTS.size() == 4U, "Projected points should contain exactly 4 corners")) {
      return 1;
    }

    detection.points = std::array<cv::Point2f, 4>{POINTS[0], POINTS[1], POINTS[2], POINTS[3]};
    if (!AssertOrFail(solver.Solve(detection),
                      "Solve should succeed on synthetic projected corners")) {
      return 1;
    }
    if (!AssertOrFail(detection.is_solved, "Detection should be marked as solved")) {
      return 1;
    }
    if (!AssertOrFail(std::isfinite(detection.reproj_error),
                      "Reprojection error should be finite")) {
      return 1;
    }

    std::cout << "[PASS] pnp_solver_test\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[FAIL] exception: " << exception.what() << "\n";
    return 1;
  }
}
