/**
 * @file pnp_solver.hpp
 * @brief 基于 OpenCV solvePnP 的装甲板位姿解算器（IPPE + 外参变换）
 *
 * 【算法原理】
 *   使用 cv::solvePnP（IPPE 方法，平面目标解析解），以装甲板四角点的已知
 *   世界坐标与图像像素坐标求解相机坐标系下的 R / t，再经外参矩阵（R_c2g /
 *   t_c2g）变换到云台坐标系后输出 xyz + 角度。
 *
 *   IPPE（Infinitesimal Plane-based Pose Estimation）对装甲板等平面目标
 *   比 ITERATIVE（Levenberg-Marquardt 迭代）更快、更不依赖初始值。
 *
 * 【世界坐标约定（armor 以中心为原点，Z=0，单位：m）】
 *   小装甲板（SMALL）：宽 0.135m，高 0.055m
 *   大装甲板（BIG  ）：宽 0.230m，高 0.055m
 *   角点顺序与 Detection.points 一致：BL / BR / TR / TL
 *
 * 【坐标系变换链】
 *   solvePnP → 相机坐标系（right+X、down+Y、fwd+Z）
 *            → 云台坐标系（R_c2g、t_c2g 外参）
 *
 * 【YAML 配置字段】（来自 vision.yaml 的 calibration 节点）
 * @code
 *   calibration:
 *     camera_matrix: [fx,0,cx, 0,fy,cy, 0,0,1]  # 9 个浮点数（行优先）
 *     distort_coeffs: [k1,k2,p1,p2,k3]           # 5 个畸变系数
 *     # 可选外参（不提供时使用 2-axis Y 翻转简化）
 *     R_camera_to_gimbal: [r00,r01,r02, r10,r11,r12, r20,r21,r22]  # 3×3 行优先
 *     t_camera_to_gimbal: [tx, ty, tz]                               # 3×1，单位 m
 * @endcode
 *
 * 工厂键：`"pnp"`
 */
#pragma once

#include "interfaces/i_solver.hpp"

#include <opencv2/core.hpp>
#include <Eigen/Core>

namespace mv::modules {

class PnpSolver final : public ISolver {
 public:
  PnpSolver();
  ~PnpSolver() override;

  PnpSolver(const PnpSolver&) = delete;
  PnpSolver& operator=(const PnpSolver&) = delete;
  PnpSolver(PnpSolver&&) = delete;
  PnpSolver& operator=(PnpSolver&&) = delete;

  bool Init(const YAML::Node& config) override;

  bool Solve(Detection& detection) override;

  [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

 private:
  // ── 内参 ──────────────────────────────────────────────────────────────────
  cv::Mat camera_matrix_;   ///< 3×3，CV_64F
  cv::Mat dist_coeffs_;     ///< 1×5，CV_64F

  // ── 外参（相机 → 云台坐标系）────────────────────────────────────────────
  // 默认：Y 轴翻转（相机 down+Y → 云台 up+Y），等效于旧版简化实现
  // 提供 calibration.R_camera_to_gimbal / t_camera_to_gimbal 后可使用精确外参
  Eigen::Matrix3d R_camera2gimbal_{
      (Eigen::Matrix3d() << 1.0, 0.0, 0.0,
                            0.0,-1.0, 0.0,
                            0.0, 0.0, 1.0).finished()};
  Eigen::Vector3d t_camera2gimbal_{Eigen::Vector3d::Zero()};

  // ── 世界坐标模板 ──────────────────────────────────────────────────────────
  // 小装甲：half_w=0.0675, half_h=0.0275
  // 大装甲：half_w=0.115,  half_h=0.0275
  static constexpr float SMALL_HALF_W = 0.0675F;
  static constexpr float BIG_HALF_W   = 0.115F;
  static constexpr float HALF_H       = 0.0275F;

  bool initialized_{false};
};

}  // namespace mv::modules
