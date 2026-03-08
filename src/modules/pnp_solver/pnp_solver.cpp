/**
 * @file pnp_solver.cpp
 * @brief PnP 位姿解算器实现（IPPE + 外参变换）
 *
 * 【算法升级说明（相较 ITERATIVE 版本）】
 *   1. SOLVEPNP_IPPE：装甲板为平面目标，IPPE 给出解析解，比 LM 迭代
 *      快 3~5×，且无初始值依赖，精度相当。
 *   2. 外参变换：可从 calibration.R_camera2gimbal / t_camera2gimbal 加载
 *      标定外参，实现精确相机→云台坐标系转换。不提供外参时退化为 Y 轴
 *      翻转（兼容旧行为）。
 *   3. yaw/pitch 从云台坐标系（非原始相机坐标）计算，误差更小。
 */
#include "pnp_solver.hpp"

#include "core/logger.hpp"
#include "factory/factory.hpp"

#include <cmath>
#include <vector>

#include <Eigen/Core>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/imgproc.hpp>

namespace mv::modules {

namespace {
const bool PNP_SOLVER_REGISTERED = [] {
  ::mv::Factory<::mv::ISolver>::Register("pnp",
                                         [] { return std::make_unique<PnpSolver>(); });
  return true;
}();
}  // namespace

// ── 世界坐标模板（静态，仅构建一次）──────────────────────────────────────────

/** 构建世界坐标点（BL, BR, TR, TL，Z=0，装甲板平面朝相机）*/
static std::vector<cv::Point3f> MakeWorldPoints(float half_w, float half_h) {
  return {
      {-half_w, -half_h, 0.0F},  // BL
      { half_w, -half_h, 0.0F},  // BR
      { half_w,  half_h, 0.0F},  // TR
      {-half_w,  half_h, 0.0F},  // TL
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

  // ── 内参：camera_matrix（必须）──────────────────────────────────────────
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

  // ── 内参：distort_coeffs（可选）─────────────────────────────────────────
  if (!calib["distort_coeffs"]) {
    MV_LOG_WARN("PnpSolver", "calibration.distort_coeffs not found, using zero distortion");
    dist_coeffs_ = cv::Mat::zeros(1, 5, CV_64F);
  } else {
    auto dc_vals = calib["distort_coeffs"].as<std::vector<double>>();
    dist_coeffs_ = cv::Mat(1, static_cast<int>(dc_vals.size()), CV_64F, dc_vals.data()).clone();
  }

  // ── 外参：R_camera2gimbal（可选，3×3 行优先）────────────────────────────
  if (calib["R_camera_to_gimbal"]) {
    auto r_vals = calib["R_camera_to_gimbal"].as<std::vector<double>>();
    if (r_vals.size() == 9) {
      R_camera2gimbal_ = Eigen::Matrix<double, 3, 3, Eigen::RowMajor>(r_vals.data());
    } else {
      MV_LOG_WARN("PnpSolver", "R_camera_to_gimbal must have 9 values, got {} — ignored",
                  r_vals.size());
    }
  }

  // ── 外参：t_camera2gimbal（可选，3×1）───────────────────────────────────
  if (calib["t_camera_to_gimbal"]) {
    auto t_vals = calib["t_camera_to_gimbal"].as<std::vector<double>>();
    if (t_vals.size() == 3) {
      t_camera2gimbal_ = Eigen::Vector3d(t_vals[0], t_vals[1], t_vals[2]);
    } else {
      MV_LOG_WARN("PnpSolver", "t_camera_to_gimbal must have 3 values, got {} — ignored",
                  t_vals.size());
    }
  }

  initialized_ = true;
  MV_LOG_INFO("PnpSolver",
              "Init OK — fx={:.1f} fy={:.1f} cx={:.1f} cy={:.1f}",
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
  const float HALF_W =
      (detection.type == ArmorType::BIG) ? BIG_HALF_W : SMALL_HALF_W;
  const std::vector<cv::Point3f> WORLD_PTS = MakeWorldPoints(HALF_W, HALF_H);

  // 组织图像点
  std::vector<cv::Point2f> img_pts(detection.points.begin(), detection.points.end());

  cv::Mat rvec;
  cv::Mat tvec;

  // IPPE：平面目标解析解，比 ITERATIVE 快且无需初始值
  const bool SOLVE_OK = cv::solvePnP(WORLD_PTS, img_pts, camera_matrix_, dist_coeffs_,
                                     rvec, tvec, false, cv::SOLVEPNP_IPPE);
  if (!SOLVE_OK) {
    MV_LOG_DEBUG("PnpSolver", "solvePnP (IPPE) failed for this detection");
    return false;
  }

  // ── 坐标系变换：相机 → 云台（R_c2g * xyz_c + t_c2g）──────────────────────
  Eigen::Vector3d xyz_in_camera;
  cv::cv2eigen(tvec, xyz_in_camera);

  const Eigen::Vector3d xyz_in_gimbal =
      R_camera2gimbal_ * xyz_in_camera + t_camera2gimbal_;

  detection.xyz_in_gimbal = xyz_in_gimbal;

  // yaw / pitch 从云台坐标系计算
  detection.yaw_angle   = std::atan2(xyz_in_gimbal.x(), xyz_in_gimbal.z());
  detection.pitch_angle = std::atan2(-xyz_in_gimbal.y(), xyz_in_gimbal.z());

  detection.is_solved = true;
  return true;
}

}  // namespace mv::modules
