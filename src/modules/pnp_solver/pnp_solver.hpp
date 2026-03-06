/**
 * @file pnp_solver.hpp
 * @brief 基于 OpenCV solvePnP 的装甲板位姿解算器
 *
 * 【算法原理】
 *   使用 cv::solvePnP（ITERATIVE 方法），以装甲板四角点的已知世界坐标
 *   与图像像素坐标求解 R / t 向量，转换到云台坐标系后输出 xyz + 角度。
 *
 * 【世界坐标约定（armor 以中心为原点，Z=0，单位：m）】
 *   小装甲板（SMALL）：宽 0.135m，高 0.055m
 *   大装甲板（BIG  ）：宽 0.230m，高 0.055m
 *   角点顺序与 Detection.points 一致：BL / BR / TR / TL
 *
 * 【坐标系转换】
 *   solvePnP 输出相机坐标系（右+X、下+Y、前+Z），
 *   本类内部转换为云台坐标系（右+X、上+Y、前+Z）。
 *
 * 【YAML 配置字段】（来自 vision.yaml 的 calibration 节点）
 * @code
 *   calibration:
 *     camera_matrix: [fx,0,cx, 0,fy,cy, 0,0,1]  # 9 个浮点数（行优先）
 *     distort_coeffs: [k1,k2,p1,p2,k3]           # 5 个畸变系数
 * @endcode
 *
 * 工厂键：`"pnp"`
 */
#pragma once

#include "interfaces/i_solver.hpp"

#include <opencv2/core.hpp>

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
  // 相机内参（cv::Mat，3×3）
  cv::Mat camera_matrix_;
  // 畸变系数（cv::Mat，1×5）
  cv::Mat dist_coeffs_;

  // 世界坐标模板：[BL, BR, TR, TL] 各 (x, y, z)，单位 m
  // 小装甲：half_w=0.0675, half_h=0.0275
  // 大装甲：half_w=0.115,  half_h=0.0275
  static constexpr float SMALL_HALF_W = 0.0675F;
  static constexpr float BIG_HALF_W = 0.115F;
  static constexpr float HALF_H = 0.0275F;

  bool initialized_{false};
};

}  // namespace mv::modules
