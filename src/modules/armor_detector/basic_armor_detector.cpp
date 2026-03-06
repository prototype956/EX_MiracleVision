/**
 * @file basic_armor_detector.cpp
 * @brief 传统视觉装甲板检测器实现
 */
#include "basic_armor_detector.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace mv::modules {

// 注册到工厂（匿名 namespace + 静态 bool，main() 之前触发）
namespace {
const bool BASIC_ARMOR_DETECTOR_REGISTERED = [] {
  ::mv::Factory<::mv::IDetector>::Register("basic",
                                           [] { return std::make_unique<BasicArmorDetector>(); });
  return true;
}();
}  // namespace

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

BasicArmorDetector::BasicArmorDetector() = default;
BasicArmorDetector::~BasicArmorDetector() = default;

// ── Init ──────────────────────────────────────────────────────────────────

bool BasicArmorDetector::Init(const YAML::Node& config) {
  if (config && config["detector"]) {
    const auto& det = config["detector"];
    if (det["light_thresh"]) {
      params_.light_thresh = det["light_thresh"].as<int>();
    }
    if (det["min_light_ratio"]) {
      params_.min_light_ratio = det["min_light_ratio"].as<float>();
    }
    if (det["max_light_ratio"]) {
      params_.max_light_ratio = det["max_light_ratio"].as<float>();
    }
    if (det["max_light_angle"]) {
      params_.max_light_angle = det["max_light_angle"].as<float>();
    }
    if (det["min_armor_ratio"]) {
      params_.min_armor_ratio = det["min_armor_ratio"].as<float>();
    }
    if (det["max_armor_ratio"]) {
      params_.max_armor_ratio = det["max_armor_ratio"].as<float>();
    }
    if (det["max_angle_diff"]) {
      params_.max_angle_diff = det["max_angle_diff"].as<float>();
    }
    if (det["min_area"]) {
      params_.min_area = det["min_area"].as<float>();
    }
  }
  initialized_ = true;
  MV_LOG_INFO("BasicArmorDetector",
              "Init OK — thresh={} angle_lim={:.1f}° armor_ratio=[{:.1f},{:.1f}]",
              params_.light_thresh, params_.max_light_angle, params_.min_armor_ratio,
              params_.max_armor_ratio);
  return true;
}

// ── Detect ────────────────────────────────────────────────────────────────

std::vector<Detection> BasicArmorDetector::Detect(const cv::Mat& frame, ArmorColor enemy_color) {
  if (frame.empty()) {
    MV_LOG_WARN("BasicArmorDetector", "Detect() called with empty frame");
    return {};
  }

  cv::Mat diff = BuildChannelDiff(frame, enemy_color);

  cv::Mat binary;
  cv::threshold(diff, binary, params_.light_thresh, 255, cv::THRESH_BINARY);

  // 膨胀一下：让轮廓更连续，减少断裂
  const cv::Mat KERNEL = cv::getStructuringElement(cv::MORPH_ELLIPSE, {3, 3});
  cv::dilate(binary, binary, KERNEL);

  // ── DEBUG：存储差分图和二值图 ───────────────────────────────────────
  if (debug_enabled_) {
    debug_data_.diff = diff.clone();
    debug_data_.binary = binary.clone();
  }

  std::vector<LightBar> lights = FindLightBars(binary);

  // ── DEBUG：在原图上创建灯条可视化图 ──────────────────────────────
  if (debug_enabled_) {
    debug_data_.lights_vis = frame.clone();
    for (const auto& lb : lights) {
      std::array<cv::Point2f, 4> corners{};
      lb.rect.points(corners.data());
      for (int i = 0; i < 4; ++i) {
        cv::line(debug_data_.lights_vis, corners[i], corners[(i + 1) % 4], cv::Scalar(0, 255, 255),
                 1);
      }
      cv::circle(debug_data_.lights_vis, lb.top, 3, cv::Scalar(255, 120, 0), -1);
      cv::circle(debug_data_.lights_vis, lb.bottom, 3, cv::Scalar(0, 120, 255), -1);
    }
  }

  if (lights.size() < 2) {
    return {};
  }

  // 按中心 x 从左到右排序
  std::sort(lights.begin(), lights.end(), [](const LightBar& lhs, const LightBar& rhs) {
    return lhs.rect.center.x < rhs.rect.center.x;
  });

  std::vector<Detection> detections;
  for (std::size_t idx = 0; idx + 1 < lights.size(); ++idx) {
    for (std::size_t jdx = idx + 1; jdx < lights.size(); ++jdx) {
      if (IsValidArmor(lights[idx], lights[jdx])) {
        detections.emplace_back(MakeDetection(lights[idx], lights[jdx]));
      }
    }
  }

  MV_LOG_DEBUG("BasicArmorDetector", "Frame: {} lights, {} armors", lights.size(),
               detections.size());
  return detections;
}

// ── 内部方法实现 ──────────────────────────────────────────────────────────

cv::Mat BasicArmorDetector::BuildChannelDiff(const cv::Mat& bgr, ArmorColor color) {
  std::vector<cv::Mat> channels;
  cv::split(bgr, channels);
  // channels[0]=B, [1]=G, [2]=R

  cv::Mat result;
  if (color == ArmorColor::RED) {
    cv::subtract(channels[2], channels[0], result);  // R - B（自动饱和截断至 0）
  } else if (color == ArmorColor::BLUE) {
    cv::subtract(channels[0], channels[2], result);  // B - R
  } else {
    cv::cvtColor(bgr, result, cv::COLOR_BGR2GRAY);
  }
  return result;
}

std::vector<BasicArmorDetector::LightBar> BasicArmorDetector::FindLightBars(
    const cv::Mat& binary) const {
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  std::vector<LightBar> lights;
  lights.reserve(contours.size());

  for (const auto& contour : contours) {
    const float AREA = static_cast<float>(cv::contourArea(contour));
    if (AREA < params_.min_area) {
      continue;
    }
    if (contour.size() < 5) {
      // fitEllipse / minAreaRect requires at least 5 points
      continue;
    }

    const cv::RotatedRect RAW = cv::minAreaRect(contour);

    // 确保 height >= width（长轴归一化）
    cv::RotatedRect rect = RAW;
    if (rect.size.width > rect.size.height) {
      std::swap(rect.size.width, rect.size.height);
      rect.angle += 90.0F;
    }

    const float LEN = rect.size.height;
    const float WID = rect.size.width;
    const float RATIO = WID / LEN;

    if (RATIO < params_.min_light_ratio || RATIO > params_.max_light_ratio) {
      continue;
    }

    // 计算相对垂直方向的倾斜角：长轴方向 = angle+90° from horizontal
    // tilt from vertical = |angle from horizontal - 90°| = |rect.angle|
    // 经过上面的归一化，rect.angle ∈ [-90, 0) 且 height >= width
    // 长轴方向与水平夹角 ≈ 90° + rect.angle（因为长轴=高=宽轴转90°）
    // 倾斜角 = |90° - long_axis_angle_from_vertical| 即 |rect.angle|（after normalization）
    const float TILT = std::abs(rect.angle);  // 0 = 垂直, 90 = 水平

    if (TILT > params_.max_light_angle) {
      continue;
    }

    // 计算顶端、底端中点
    std::array<cv::Point2f, 4> corners{};
    rect.points(corners.data());
    // 按 y 排序，y 小的是图像中更靠上的点
    std::sort(corners.begin(), corners.end(),
              [](const cv::Point2f& lpt, const cv::Point2f& rpt) { return lpt.y < rpt.y; });
    const cv::Point2f TOP = (corners[0] + corners[1]) * 0.5F;
    const cv::Point2f BOTTOM = (corners[2] + corners[3]) * 0.5F;

    lights.push_back({rect, TOP, BOTTOM, LEN, WID, TILT});
  }

  return lights;
}

bool BasicArmorDetector::IsValidArmor(const LightBar& left, const LightBar& right) const {
  // 1. 角度差
  const float ANGLE_DIFF = std::abs(left.tilt - right.tilt);
  if (ANGLE_DIFF > params_.max_angle_diff) {
    return false;
  }

  // 2. 高度比（大小相差不能超过 40%）
  const float AVG_LEN = (left.length + right.length) * 0.5F;
  const float MAX_LEN = std::max(left.length, right.length);
  const float MIN_LEN = std::min(left.length, right.length);
  if (MIN_LEN / MAX_LEN < 0.6F) {
    return false;
  }

  // 3. 垂直位移过大（两灯条中心 Y 差 > 0.5 × avg_len）
  const float DELTA_Y = std::abs(left.rect.center.y - right.rect.center.y);
  if (DELTA_Y > AVG_LEN * 0.5F) {
    return false;
  }

  // 4. 装甲宽高比：水平间距 / avg_len
  const float DELTA_X = right.rect.center.x - left.rect.center.x;
  if (DELTA_X <= 0.0F) {
    return false;  // 重叠或顺序错误
  }
  const float RATIO = DELTA_X / AVG_LEN;
  return RATIO >= params_.min_armor_ratio && RATIO <= params_.max_armor_ratio;
}

Detection BasicArmorDetector::MakeDetection(const LightBar& left, const LightBar& right) {
  Detection det;

  // 四角点顺序：BL, BR, TR, TL
  det.points[0] = left.bottom;   // BL
  det.points[1] = right.bottom;  // BR
  det.points[2] = right.top;     // TR
  det.points[3] = left.top;      // TL

  // 包围框
  const float MIN_X =
      std::min({det.points[0].x, det.points[1].x, det.points[2].x, det.points[3].x});
  const float MAX_X =
      std::max({det.points[0].x, det.points[1].x, det.points[2].x, det.points[3].x});
  const float MIN_Y =
      std::min({det.points[0].y, det.points[1].y, det.points[2].y, det.points[3].y});
  const float MAX_Y =
      std::max({det.points[0].y, det.points[1].y, det.points[2].y, det.points[3].y});
  det.box = {MIN_X, MIN_Y, MAX_X - MIN_X, MAX_Y - MIN_Y};

  // 置信度固定 1.0，颜色/编号留由外部填写（此处无分类器）
  det.confidence = 1.0F;
  det.color = ArmorColor::UNKNOWN;
  det.number = ArmorNumber::UNKNOWN;

  // 装甲类型：水平跨度 / 平均高度 > 3.5 判为大装甲
  const float HSPAN = det.box.width;
  const float AVG_H = (left.length + right.length) * 0.5F;
  det.type = (HSPAN / AVG_H > 3.5F) ? ArmorType::BIG : ArmorType::SMALL;

  // 到图像中心的距离（假设 1280×1024，中心为 (640,512)）
  const cv::Point2f CENTER = det.Center();
  const cv::Point2f IMG_CTR = {640.0F, 512.0F};
  det.distance_to_center = static_cast<double>(cv::norm(CENTER - IMG_CTR));

  // 3D 信息待 ISolver 填充
  det.is_solved = false;

  return det;
}

}  // namespace mv::modules
