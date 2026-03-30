/**
 * @file armor_detector_contract_test.cpp
 * @brief ArmorDetector 与 RoiManager 契约测试（角点顺序 / 坐标恢复 / 回退策略）
 */

#include "modules/armor_detector/basic_armor_detector.hpp"
#include "modules/armor_detector/roi_manager.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

cv::Mat MakeSyntheticBlueArmorFrame(int width, int height) {
  cv::Mat frame(height, width, CV_8UC3, cv::Scalar(0, 0, 0));

  // 两个细长蓝色灯条（椭圆，确保 contour 点数 > 5）
  const cv::Point LEFT_CENTER(width / 2 - 40, height / 2);
  const cv::Point RIGHT_CENTER(width / 2 + 40, height / 2);
  const cv::Size LIGHT_AXES(5, 30);
  cv::ellipse(frame, LEFT_CENTER, LIGHT_AXES, 0.0, 0.0, 360.0, cv::Scalar(255, 0, 0), -1);
  cv::ellipse(frame, RIGHT_CENTER, LIGHT_AXES, 0.0, 0.0, 360.0, cv::Scalar(255, 0, 0), -1);

  return frame;
}

bool TestDetectEmptyFrame() {
  mv::modules::BasicArmorDetector detector;
  if (!AssertOrFail(detector.Init(YAML::Node{}), "Init should succeed with default config")) {
    return false;
  }

  const std::vector<mv::Detection> DETECTIONS = detector.Detect(cv::Mat{}, mv::ArmorColor::BLUE);
  return AssertOrFail(DETECTIONS.empty(), "Detect(empty frame) should return empty vector");
}

bool TestDetectCornerOrderAndColor() {
  mv::modules::BasicArmorDetector detector;
  if (!AssertOrFail(detector.Init(YAML::Node{}), "Init should succeed with default config")) {
    return false;
  }

  auto params = detector.GetParams();
  params.light_thresh = 5;
  params.green_thresh = 10;
  params.min_area = 1.0F;
  params.min_light_ratio = 0.01F;
  params.max_light_ratio = 1.0F;
  params.max_light_angle = 90.0F;
  params.min_armor_ratio = 0.1F;
  params.max_armor_ratio = 10.0F;
  params.max_angle_diff = 90.0F;
  detector.SetParams(params);

  const cv::Mat FRAME = MakeSyntheticBlueArmorFrame(1280, 1024);
  const std::vector<mv::Detection> DETECTIONS = detector.Detect(FRAME, mv::ArmorColor::BLUE);
  if (!AssertOrFail(!DETECTIONS.empty(), "Synthetic frame should produce at least one armor")) {
    return false;
  }

  const mv::Detection& detection_first = DETECTIONS.front();
  if (!AssertOrFail(detection_first.color == mv::ArmorColor::BLUE,
                    "Detection color should match enemy_color filter")) {
    return false;
  }

  // 角点顺序契约：BL, BR, TR, TL。
  const cv::Point2f& point_bottom_left = detection_first.points[0];
  const cv::Point2f& point_bottom_right = detection_first.points[1];
  const cv::Point2f& point_top_right = detection_first.points[2];
  const cv::Point2f& point_top_left = detection_first.points[3];

  if (!AssertOrFail(point_bottom_left.y >= point_top_left.y,
                    "BL should be below TL in image coordinates")) {
    return false;
  }
  if (!AssertOrFail(point_bottom_right.y >= point_top_right.y,
                    "BR should be below TR in image coordinates")) {
    return false;
  }
  if (!AssertOrFail(
          point_bottom_left.x <= point_bottom_right.x && point_top_left.x <= point_top_right.x,
          "Left corners should be left of right corners")) {
    return false;
  }

  return true;
}

bool TestRoiRestoreAndDistanceCenter() {
  mv::modules::RoiManager roi;

  mv::Detection det;
  det.points = {cv::Point2f{10.0F, 20.0F}, cv::Point2f{30.0F, 20.0F}, cv::Point2f{30.0F, 10.0F},
                cv::Point2f{10.0F, 10.0F}};
  det.box = cv::Rect2f{10.0F, 10.0F, 20.0F, 10.0F};
  std::vector<mv::Detection> detections{det};

  const cv::Point2i OFFSET{100, 200};
  const cv::Size FRAME_SIZE{1280, 1024};
  roi.RestoreAndUpdate(detections, OFFSET, FRAME_SIZE);

  if (!AssertOrFail(std::abs(detections[0].points[0].x - 110.0F) < 1e-4F,
                    "Point x should be restored by offset.x")) {
    return false;
  }
  if (!AssertOrFail(std::abs(detections[0].points[0].y - 220.0F) < 1e-4F,
                    "Point y should be restored by offset.y")) {
    return false;
  }

  const cv::Point2f IMG_CENTER{static_cast<float>(FRAME_SIZE.width) * 0.5F,
                               static_cast<float>(FRAME_SIZE.height) * 0.5F};
  const double EXPECTED_DIST = cv::norm(detections[0].Center() - IMG_CENTER);
  if (!AssertOrFail(std::abs(detections[0].distance_to_center - EXPECTED_DIST) < 1e-6,
                    "distance_to_center should be computed against full-frame center")) {
    return false;
  }

  return AssertOrFail(roi.IsActive(), "ROI should become active after successful update");
}

bool TestRoiLostFallback() {
  mv::modules::RoiManager roi;

  // 先喂一帧有效检测激活 ROI。
  {
    mv::Detection det;
    det.box = cv::Rect2f{100.0F, 120.0F, 40.0F, 20.0F};
    det.points = {cv::Point2f{100.0F, 140.0F}, cv::Point2f{140.0F, 140.0F},
                  cv::Point2f{140.0F, 120.0F}, cv::Point2f{100.0F, 120.0F}};
    std::vector<mv::Detection> detections{det};
    roi.RestoreAndUpdate(detections, cv::Point2i{0, 0}, cv::Size{1280, 1024});
  }

  if (!AssertOrFail(roi.IsActive(), "ROI should be active after one valid update")) {
    return false;
  }

  std::vector<mv::Detection> empty;
  for (int idx = 0; idx < 5; ++idx) {
    roi.RestoreAndUpdate(empty, cv::Point2i{0, 0}, cv::Size{1280, 1024});
  }

  return AssertOrFail(!roi.IsActive(),
                      "ROI should fallback to full frame after kMaxLost empty frames");
}

}  // namespace

int main() {
  if (!TestDetectEmptyFrame()) {
    return 1;
  }
  if (!TestDetectCornerOrderAndColor()) {
    return 1;
  }
  if (!TestRoiRestoreAndDistanceCenter()) {
    return 1;
  }
  if (!TestRoiLostFallback()) {
    return 1;
  }

  std::cout << "[PASS] armor_detector_contract_test\n";
  return 0;
}
