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
#include <Eigen/Geometry>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/imgproc.hpp>

namespace mv::modules {

namespace {
const bool PNP_SOLVER_REGISTERED = [] {
  ::mv::Factory<::mv::ISolver>::Register("pnp", [] { return std::make_unique<PnpSolver>(); });
  return true;
}();
}  // namespace

// ── 世界坐标模板（静态，仅构建一次）──────────────────────────────────────────

/** 构建世界坐标点（BL, BR, TR, TL，Z=0，装甲板平面朝相机）*/
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

// ── 云台→世界坐标变换接口实现 ─────────────────────────────────────────────

void PnpSolver::SetGimbalToWorldRotation(const Eigen::Quaterniond& quaternion) {
  Eigen::Matrix3d R_imubody2imuabsolute_ = quaternion.toRotationMatrix();
  R_gimbal2world_ = R_gimbal2imubody_.transpose() * R_imubody2imuabsolute_ * R_gimbal2imubody_;
}

Eigen::Matrix3d PnpSolver::GetGimbalToWorldRotation() const {
  return R_gimbal2world_;
}

void PnpSolver::SetGimbalToWorldTranslation(const Eigen::Quaterniond& quaternion) {
  Eigen::Matrix3d R_imubody2imuabsolute_ = quaternion.toRotationMatrix();
  t_gimbal2world_ = R_imubody2imuabsolute_ * t_gimbal2imubody_;
}

Eigen::Vector3d PnpSolver::GetGimbalToWorldTranslation() const {
  return t_gimbal2world_;
}

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

  if (calib["R_gimbal_to_imubody"]) {
    auto r_vals = calib["R_gimbal_to_imubody"].as<std::vector<double>>();
    if (r_vals.size() == 9) {
      R_gimbal2imubody_ = Eigen::Matrix<double, 3, 3, Eigen::RowMajor>(r_vals.data());
    } else {
      MV_LOG_WARN("PnpSolver", "R_gimbal_to_imubody must have 9 values, got {} — ignored",
                  r_vals.size());
    }
  }

  // ── 装甲板物理尺寸（可选，armor 节点优先）─────────────────────────────
  if (config["armor"]) {
    const auto& armor = config["armor"];
    small_half_w_ = armor["small_half_w"].as<float>(0.0675F);
    big_half_w_ = armor["big_half_w"].as<float>(0.115F);
    half_h_ = armor["half_h"].as<float>(0.0275F);
  }

  initialized_ = true;
  MV_LOG_INFO("PnpSolver",
              "Init OK — fx={:.1f} fy={:.1f} cx={:.1f} cy={:.1f} "
              "armor small_hw={:.4f} big_hw={:.4f} hh={:.4f}",
              camera_matrix_.at<double>(0, 0), camera_matrix_.at<double>(1, 1),
              camera_matrix_.at<double>(0, 2), camera_matrix_.at<double>(1, 2), small_half_w_,
              big_half_w_, half_h_);
  return true;
}

// ── Solve ─────────────────────────────────────────────────────────────────

bool PnpSolver::Solve(Detection& detection) {
  if (!initialized_) {
    MV_LOG_ERROR("PnpSolver", "Solve() called before Init()");
    return false;
  }

  // 选取世界坐标模板
  const float HALF_W = (detection.type == ArmorType::BIG) ? big_half_w_ : small_half_w_;
  const std::vector<cv::Point3f> WORLD_PTS = MakeWorldPoints(HALF_W, half_h_);

  // 组织图像点
  std::vector<cv::Point2f> img_pts(detection.points.begin(), detection.points.end());

  cv::Mat rvec;
  cv::Mat tvec;

  // ── IPPE 双解选优 ─────────────────────────────────────────────────────────
  // SOLVEPNP_IPPE 对平面目标必然产生两个数学等价解（平面"正面"vs"背面翻转"）。
  // 直接用 solvePnP() 时，内部随机选一个解，在特定距离/角度时会发生"跳解"，
  // 导致深度和 yaw 突变。solvePnPGeneric 可同时拿到两个解并自行评估选最优。
  std::vector<cv::Mat> rvecs;
  std::vector<cv::Mat> tvecs;
  std::vector<double> reproj_errs_raw;
  const int num_sols =
      cv::solvePnPGeneric(WORLD_PTS, img_pts, camera_matrix_, dist_coeffs_, rvecs, tvecs, false,
                          cv::SOLVEPNP_IPPE, cv::noArray(), cv::noArray(), reproj_errs_raw);
  if (num_sols == 0) {
    MV_LOG_DEBUG("PnpSolver", "solvePnPGeneric (IPPE) returned 0 solutions");
    return false;
  }

  // ── 选解策略（三级） ──────────────────────────────────────────────────────
  //
  //  Level 1：Z > 0 过滤（目标必须在相机正前方，否则物理无效）。
  //  Level 2：若两解都满足（或都不满足）Z>0：比较重投影误差。
  //  Level 3（时序锁定 Temporal Continuity）：
  //    若两解的重投影误差差距 < kReprErrHysteresis px（近距离时常见），
  //    单用误差判据不可靠，改为选距上一帧 tvec 更近的解，防止逐帧"翻解"。
  //    若上一帧距当前帧 tvec 跳变 > kMaxFrameJump m，则认为是新目标，
  //    清除历史并重新用误差判据。

  int best = 0;
  int alt = -1;
  if (num_sols > 1) {
    alt = 1;
    const bool z0_pos = (tvecs[0].at<double>(2) > 0);
    const bool z1_pos = (tvecs[1].at<double>(2) > 0);

    if (z0_pos && !z1_pos) {
      best = 0;
      alt = 1;
    } else if (!z0_pos && z1_pos) {
      best = 1;
      alt = 0;
    } else {
      // 两解同为 Z>0 或同为 Z<=0：先按误差选 best
      best = (reproj_errs_raw[0] <= reproj_errs_raw[1]) ? 0 : 1;
      alt = 1 - best;

      // Level 3：误差差距很小时用时序锁定覆盖
      const double err_diff = std::abs(reproj_errs_raw[0] - reproj_errs_raw[1]);
      if (err_diff < REPR_ERR_HYSTERESIS && !last_tvec_.empty()) {
        Eigen::Vector3d p_best;
        Eigen::Vector3d p_alt;
        Eigen::Vector3d p_last;
        cv::cv2eigen(tvecs[best], p_best);
        cv::cv2eigen(tvecs[alt], p_alt);
        cv::cv2eigen(last_tvec_, p_last);

        const double d_best = (p_best - p_last).norm();
        const double d_alt = (p_alt - p_last).norm();

        // 若上一帧距离超阈值，认为是新目标，清除历史
        if (d_best > MAX_FRAME_JUMP && d_alt > MAX_FRAME_JUMP) {
          last_tvec_ = cv::Mat();  // reset
        } else if (d_alt < d_best) {
          // alt 更接近上一帧位置，翻转：alt 才是真正的连续解
          std::swap(best, alt);
        }
        // 否则 best 本来就更接近上一帧，保持不变
      }
    }
  }

  // 更新时序历史
  last_tvec_ = tvecs[best].clone();

  rvec = rvecs[best];
  tvec = tvecs[best];

  // ── 坐标系变换：相机 → 云台（R_c2g * xyz_c + t_c2g）──────────────────────
  Eigen::Vector3d xyz_in_camera;
  cv::cv2eigen(tvec, xyz_in_camera);

  const Eigen::Vector3d xyz_in_gimbal = R_camera2gimbal_ * xyz_in_camera + t_camera2gimbal_;

  detection.xyz_in_gimbal = xyz_in_gimbal;

  // yaw / pitch 从云台坐标系计算
  detection.yaw_angle = std::atan2(xyz_in_gimbal.x(), xyz_in_gimbal.z());
  detection.pitch_angle = std::atan2(-xyz_in_gimbal.y(), xyz_in_gimbal.z());

  // ── 重投影误差（主解）──────────────────────────────────────────────────────
  {
    std::vector<cv::Point2f> proj_pts;
    cv::projectPoints(WORLD_PTS, rvec, tvec, camera_matrix_, dist_coeffs_, proj_pts);
    if (proj_pts.size() == 4) {
      double err_sq_sum = 0.0;
      for (int k = 0; k < 4; ++k) {
        detection.reprojected_points.at(static_cast<size_t>(k)) = proj_pts[k];
        const double delta_x = img_pts[k].x - proj_pts[k].x;
        const double delta_y = img_pts[k].y - proj_pts[k].y;
        err_sq_sum += delta_x * delta_x + delta_y * delta_y;
      }
      detection.reproj_error = std::sqrt(err_sq_sum / 4.0);
    }
  }

  // ── 第二候选解（IPPE alt solution，供可视化歧义对比）──────────────────────
  if (alt >= 0 && alt < num_sols) {
    Eigen::Vector3d xyz_c_alt;
    cv::cv2eigen(tvecs[alt], xyz_c_alt);
    detection.xyz_in_gimbal_alt = R_camera2gimbal_ * xyz_c_alt + t_camera2gimbal_;

    std::vector<cv::Point2f> proj_alt;
    cv::projectPoints(WORLD_PTS, rvecs[alt], tvecs[alt], camera_matrix_, dist_coeffs_, proj_alt);
    if (proj_alt.size() == 4) {
      double err_sq_sum = 0.0;
      for (int k = 0; k < 4; ++k) {
        detection.reprojected_points_alt.at(static_cast<size_t>(k)) = proj_alt[k];
        const double delta_x = img_pts[k].x - proj_alt[k].x;
        const double delta_y = img_pts[k].y - proj_alt[k].y;
        err_sq_sum += delta_x * delta_x + delta_y * delta_y;
      }
      detection.reproj_error_alt = std::sqrt(err_sq_sum / 4.0);
    }
    detection.has_alt_solution = true;
  }

  detection.is_solved = true;
  return true;
}

// ── Yaw 优化（遍历重投影误差）────────────────────────────────────────────
void PnpSolver::OptimizeYaw(Detection& detection, double yaw_min, double yaw_max, double step) {
  if (!initialized_) {
    MV_LOG_ERROR("PnpSolver", "OptimizeYaw() called before Init()");
    return;
  }
  // 需要已有的 3D 位置（云台系），通常由 Solve() 填充
  const Eigen::Vector3d xyz_in_gimbal = detection.xyz_in_gimbal;
  const Eigen::Vector3d xyz_in_world = R_gimbal2world_ * xyz_in_gimbal + t_gimbal2world_;

  // 图像点
  const std::vector<cv::Point2f> img_pts(detection.points.begin(), detection.points.end());

  double best_err = std::numeric_limits<double>::infinity();
  double best_yaw = detection.yaw_angle;
  std::array<cv::Point2f, 4> best_proj{};

  for (double yaw = yaw_min; yaw <= yaw_max; yaw += step) {
    std::array<cv::Point2f, 4> proj_tmp{};
    const double err = ArmorReprojectionRms(xyz_in_world, yaw, detection.type, img_pts, &proj_tmp);
    if (err < best_err) {
      best_err = err;
      best_yaw = yaw;
      best_proj = proj_tmp;
    }
  }

  detection.yaw_angle = best_yaw;
  detection.reproj_error = best_err;
  detection.reprojected_points = best_proj;
}

// 私有辅助函数：给定世界坐标与 yaw，重投影装甲四角点（使用固定 pitch=+15°）
std::vector<cv::Point2f> PnpSolver::ReprojectArmor(const Eigen::Vector3d& xyz_in_world, double yaw,
                           ArmorType type) const {
  const double pitch_rad = 15.0 * CV_PI / 180.0;
  const double sin_yaw = std::sin(yaw);
  const double cos_yaw = std::cos(yaw);
  const double sin_pitch = std::sin(pitch_rad);
  const double cos_pitch = std::cos(pitch_rad);

  const Eigen::Matrix3d r_armor2world{
    {cos_yaw * cos_pitch, -sin_yaw, cos_yaw * sin_pitch},
    {sin_yaw * cos_pitch, cos_yaw, sin_yaw * sin_pitch},
    {-sin_pitch, 0.0, cos_pitch}};
  Eigen::Matrix3d r_armor2camera = R_camera2gimbal_.transpose() * R_gimbal2world_.transpose() * r_armor2world;
  Eigen::Vector3d t_armor2camera = R_camera2gimbal_.transpose() *
                   (R_gimbal2world_.transpose() * xyz_in_world - t_camera2gimbal_);

  cv::Mat r_cv;
  cv::eigen2cv(r_armor2camera, r_cv);
  cv::Vec3d rvec;
  cv::Rodrigues(r_cv, rvec);
  cv::Vec3d tvec(t_armor2camera[0], t_armor2camera[1], t_armor2camera[2]);

  std::vector<cv::Point2f> image_points;
  const float half_w = (type == ArmorType::BIG) ? big_half_w_ : small_half_w_;
  const auto object_pts = MakeWorldPoints(half_w, half_h_);
  cv::projectPoints(object_pts, rvec, tvec, camera_matrix_, dist_coeffs_, image_points);
  return image_points;
}

// 私有辅助函数：计算重投影 RMS（输入 world xyz、yaw、图像点），并可输出投影点
double PnpSolver::ArmorReprojectionRms(const Eigen::Vector3d& xyz_in_world, double yaw,
                                       ArmorType type, const std::vector<cv::Point2f>& img_pts,
                                       std::array<cv::Point2f, 4>* out_proj) const {
  auto proj_points = ReprojectArmor(xyz_in_world, yaw, type);
  if (proj_points.size() != 4) {
    return std::numeric_limits<double>::infinity();
  }
  double err_sq = 0.0;
  for (int k = 0; k < 4; ++k) {
    const double delta_x = img_pts[k].x - proj_points[k].x;
    const double delta_y = img_pts[k].y - proj_points[k].y;
    err_sq += delta_x * delta_x + delta_y * delta_y;
    if (out_proj) {
      (*out_proj).at(static_cast<size_t>(k)) = proj_points[k];
    }
  }
  return std::sqrt(err_sq / 4.0);
}

// 计算指定 yaw/pitch 下的重投影误差（兼容 sp 实现）
double PnpSolver::ArmorReprojectionError(const Detection& detection, float yaw, float pitch,
                                         float inclined) const {
  if (!initialized_) {
    return std::numeric_limits<double>::infinity();
  }

  // 由 detection 提供云台系位置，转换到世界坐标系
  const Eigen::Vector3d xyz_in_gimbal = detection.xyz_in_gimbal;
  const Eigen::Vector3d xyz_in_world = R_gimbal2world_ * xyz_in_gimbal + t_gimbal2world_;

  // 与 ReprojectArmor 相同的投影流程，但使用外部传入的 pitch
  const double sin_yaw = std::sin(yaw);
  const double cos_yaw = std::cos(yaw);
  const double sin_pitch = std::sin(pitch);
  const double cos_pitch = std::cos(pitch);

  const Eigen::Matrix3d r_armor2world{
      {cos_yaw * cos_pitch, -sin_yaw, cos_yaw * sin_pitch},
      {sin_yaw * cos_pitch, cos_yaw, sin_yaw * sin_pitch},
      {-sin_pitch, 0.0, cos_pitch}};

  Eigen::Matrix3d r_armor2camera =
      R_camera2gimbal_.transpose() * R_gimbal2world_.transpose() * r_armor2world;
  Eigen::Vector3d t_armor2camera = R_camera2gimbal_.transpose() *
                                  (R_gimbal2world_.transpose() * xyz_in_world - t_camera2gimbal_);

  cv::Mat r_cv;
  cv::eigen2cv(r_armor2camera, r_cv);
  cv::Vec3d rvec;
  cv::Rodrigues(r_cv, rvec);
  cv::Vec3d tvec(t_armor2camera[0], t_armor2camera[1], t_armor2camera[2]);

  const float half_w = (detection.type == ArmorType::BIG) ? big_half_w_ : small_half_w_;
  const auto object_pts = MakeWorldPoints(half_w, half_h_);
  std::vector<cv::Point2f> proj_pts;
  cv::projectPoints(object_pts, rvec, tvec, camera_matrix_, dist_coeffs_, proj_pts);

  if (proj_pts.size() != 4) {
    return std::numeric_limits<double>::infinity();
  }

  // 若提供 inclined (>0)，使用 SJTUCost（像素距离 + 角度差）；否则返回 RMS（与 ArmorReprojectionRms 一致）
  if (inclined > 0.0F) {
    // SJTUCost 接受 (refs, pts, inclined)——refs 为参考（投影），pts 为检测点
    return SJTUCost(proj_pts, std::vector<cv::Point2f>(detection.points.begin(),
                                                      detection.points.end()),
                    static_cast<double>(inclined));
  }

  double err_sq = 0.0;
  for (int k = 0; k < 4; ++k) {
    const double dx = detection.points[k].x - proj_pts[k].x;
    const double dy = detection.points[k].y - proj_pts[k].y;
    err_sq += dx * dx + dy * dy;
  }
  return std::sqrt(err_sq / 4.0);
}

// SJTU 代价函数（像素距离 + 角度差）
double PnpSolver::SJTUCost(const std::vector<cv::Point2f>& cv_refs,
                           const std::vector<cv::Point2f>& cv_pts,
                           const double& inclined) const {
  std::size_t size = cv_refs.size();
  std::vector<Eigen::Vector2d> refs;
  std::vector<Eigen::Vector2d> pts;
  for (std::size_t i = 0u; i < size; ++i) {
    refs.emplace_back(cv_refs[i].x, cv_refs[i].y);
    pts.emplace_back(cv_pts[i].x, cv_pts[i].y);
  }
  double cost = 0.;
  for (std::size_t i = 0u; i < size; ++i) {
    std::size_t p = (i + 1u) % size;
    Eigen::Vector2d ref_d = refs[p] - refs[i];
    Eigen::Vector2d pt_d = pts[p] - pts[i];

    const double ref_len = ref_d.norm();
    if (ref_len <= 1e-12) {
      continue;  // degenerate edge, skip
    }

    double pixel_dis = (0.5 * ((refs[i] - pts[i]).norm() + (refs[p] - pts[p]).norm()) +
                        std::fabs(ref_len - pt_d.norm())) /
                       ref_len;

    // inline absolute angle between ref_d and pt_d (clamped acos)
    double angular_dis = 0.0;
    const double a_norm = ref_d.norm();
    const double b_norm = pt_d.norm();
    if (a_norm > 1e-12 && b_norm > 1e-12) {
      double v = (ref_d.dot(pt_d)) / (a_norm * b_norm);
      if (v > 1.0) v = 1.0;
      if (v < -1.0) v = -1.0;
      angular_dis = std::acos(v);
    }

    double term1 = pixel_dis * std::sin(inclined);
    double term2 = angular_dis * std::cos(inclined);
    double cost_i = term1 * term1 + 2.0 * term2 * term2;
    cost += std::sqrt(cost_i);
  }
  return cost;
}

}  // namespace mv::modules

// 将一组世界坐标投影到像素平面（使用外部提供的 世界->相机 旋转和平移）
std::vector<cv::Point2f> mv::modules::PnpSolver::WorldToPixel(
  const std::vector<cv::Point3f>& world_pts, const Eigen::Matrix3d& R_world2camera,
  const Eigen::Vector3d& t_world2camera) const {
  // 转为 OpenCV rvec/tvec（Rodrigues 需要 3x3 矩阵）
  cv::Mat rvec;
  cv::Mat tvec;
  cv::eigen2cv(R_world2camera, rvec);
  cv::eigen2cv(t_world2camera, tvec);

  std::vector<cv::Point3f> valid_world_points;
  valid_world_points.reserve(world_pts.size());
  for (const auto& wp : world_pts) {
    Eigen::Vector3d wp_e(wp.x, wp.y, wp.z);
    Eigen::Vector3d cam = R_world2camera * wp_e + t_world2camera;
    if (cam.z() > 0.0) valid_world_points.push_back(wp);
  }

  if (valid_world_points.empty()) return {};

  std::vector<cv::Point2f> image_points;
  cv::projectPoints(valid_world_points, rvec, tvec, camera_matrix_, dist_coeffs_, image_points);
  return image_points;
}
