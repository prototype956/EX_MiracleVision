/**
 * @file pnp_solver.cpp
 * @brief PnP 位姿解算器实现
 */
#include "pnp_solver.hpp"

#include "core/logger.hpp"
#include "factory/factory.hpp"

#include <cmath>
#include <vector>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace mv::modules {

namespace {
const bool PNP_SOLVER_REGISTERED = [] {
  ::mv::Factory<::mv::ISolver>::Register("pnp", [] { return std::make_unique<PnpSolver>(); });
  return true;
}();
}  // namespace

// ── 世界坐标模板（静态，仅构建一次）──────────────────────────────────────────

/** 构建世界坐标点（BL, BR, TR, TL，Z=0）*/
static std::vector<cv::Point3f> MakeWorldPoints(float half_w, float half_h) {
  return {
      {-half_w, -half_h, 0.0F},  // BL
      {half_w, -half_h, 0.0F},   // BR
      {half_w, half_h, 0.0F},    // TR
      {-half_w, half_h, 0.0F},   // TL
  };
}

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

PnpSolver::PnpSolver() = default;
PnpSolver::~PnpSolver() = default;

// ── Init ──────────────────────────────────────────────────────────────────

bool PnpSolver::Init(const YAML::Node& config) {
  if (!config || !config["calibration"]) {
    MV_LOG_ERROR("PnpSolver", "Init failed: missing 'calibration' node in config");
    return false;
  }

  const auto& calib = config["calibration"];

  // 加载相机内参矩阵（9 个浮点数，行优先）
  if (!calib["camera_matrix"]) {
    MV_LOG_ERROR("PnpSolver", "Init failed: missing calibration.camera_matrix");
    return false;
  }
  auto cm_vals = calib["camera_matrix"].as<std::vector<double>>();
  if (cm_vals.size() != 9) {
    MV_LOG_ERROR("PnpSolver", "Init failed: camera_matrix must have 9 values, got {}",
                 cm_vals.size());
    return false;
  }
  camera_matrix_ = cv::Mat(3, 3, CV_64F, cm_vals.data()).clone();

  // 加载畸变系数（5 个浮点数）
  if (!calib["distort_coeffs"]) {
    MV_LOG_WARN("PnpSolver", "calibration.distort_coeffs not found, using zero distortion");
    dist_coeffs_ = cv::Mat::zeros(1, 5, CV_64F);
  } else {
    auto dc_vals = calib["distort_coeffs"].as<std::vector<double>>();
    dist_coeffs_ = cv::Mat(1, static_cast<int>(dc_vals.size()), CV_64F, dc_vals.data()).clone();
  }

  initialized_ = true;
  MV_LOG_INFO("PnpSolver", "Init OK — fx={:.1f} fy={:.1f} cx={:.1f} cy={:.1f}",
              camera_matrix_.at<double>(0, 0), camera_matrix_.at<double>(1, 1),
              camera_matrix_.at<double>(0, 2), camera_matrix_.at<double>(1, 2));
  return true;
}

// ── Solve ─────────────────────────────────────────────────────────────────

bool PnpSolver::Solve(Detection& detection) {
  if (!initialized_) {
    MV_LOG_ERROR("PnpSolver", "Solve() called before Init()");
    return false;
  }

  // 选取世界坐标模板
  const float HALF_W = (detection.type == ArmorType::BIG) ? BIG_HALF_W : SMALL_HALF_W;
  const std::vector<cv::Point3f> WORLD_PTS = MakeWorldPoints(HALF_W, HALF_H);

  // 组织图像点（从 detection.points 读取）
  std::vector<cv::Point2f> img_pts(detection.points.begin(), detection.points.end());

  cv::Mat rvec;
  cv::Mat tvec;

  const bool SOLVE_OK = cv::solvePnP(WORLD_PTS, img_pts, camera_matrix_, dist_coeffs_, rvec, tvec,
                                     false, cv::SOLVEPNP_ITERATIVE);
  if (!SOLVE_OK) {
    MV_LOG_DEBUG("PnpSolver", "solvePnP failed for this detection");
    return false;
  }

  // tvec 为相机坐标系（右+X，下+Y，前+Z），单位 m
  // 转换到云台坐标系（右+X，上+Y，前+Z）：只需翻转 Y
  const double CAM_X = tvec.at<double>(0);
  const double CAM_Y = tvec.at<double>(1);
  const double CAM_Z = tvec.at<double>(2);

  detection.xyz_in_gimbal = Eigen::Vector3d(CAM_X, -CAM_Y, CAM_Z);

  // 计算 yaw / pitch（从相机到目标的方向角）
  detection.yaw_angle = std::atan2(CAM_X, CAM_Z);     // 水平偏角
  detection.pitch_angle = std::atan2(-CAM_Y, CAM_Z);  // 垂直偏角

  detection.is_solved = true;
  return true;
}

}  // namespace mv::modules
