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

#include <Eigen/Core>
#include <opencv2/core.hpp>

namespace mv::modules {

/**
 * @brief 基于 IPPE 解析解的装甲板位姿解算器
 *
 * 实现 ISolver 接口，工厂键为 `"pnp"`。
 *
 * 【与旧版 basic_pnp 的区别】
 *   旧版 basic_pnp 使用 SOLVEPNP_ITERATIVE（LM 迭代），有初始值依赖，
 *   在无历史信息时收敛至错误最小值的概率较高。
 *   本实现改用 SOLVEPNP_IPPE（解析解）+ solvePnPGeneric 双解选优：
 *   1. IPPE 速度约为 LM 的 3~5 倍，精度在平面目标上相当；
 *   2. solvePnPGeneric 同时返回两个数学解，选优策略三级：
 *      Level 1 Z>0（目标必须在相机正前方）> Level 2 最小重投影误差 >
 *      Level 3 时序锁定（近距离误差接近时优先选与上一帧距离更近的解）。
 *
 * 【线程安全】
 *   Init() 和 Solve() 应由同一线程顺序调用。
 *   多个 PnpSolver 实例彼此独立，各自持有独立的内参/外参/时序状态。
 */
class PnpSolver final : public ISolver {
 public:
  PnpSolver();
  ~PnpSolver() override;

  PnpSolver(const PnpSolver&) = delete;
  PnpSolver& operator=(const PnpSolver&) = delete;
  PnpSolver(PnpSolver&&) = delete;
  PnpSolver& operator=(PnpSolver&&) = delete;

  /**
   * @return true   内参解析成功，对象进入就绪状态
   * @return false  缺少 calibration 节点或 camera_matrix 维度错误，
   *               错误原因已通过日志输出，对象保持未初始化状态
   *
   * @note 外参（R_camera_to_gimbal / t_camera_to_gimbal）可选：
   *       不提供时自动回退为 Y 轴翻转简化外参，与旧版行为兼容。
   */
  bool Init(const YAML::Node& config) override;

  /**
   * @brief 对单块装甲板执行 PnP 解算，就地填写位姿字段
   *
   * 读取 detection.points（4 个图像角点）和 detection.type（决定世界坐标模板尺寸），
   * 调用 solvePnPGeneric(IPPE) 获取最多两个解，按三级策略选优后写入：
   *   - detection.xyz_in_gimbal    云台坐标系三维位置（m）
   *   - detection.yaw_angle        水平角（rad，顺时针为正）
   *   - detection.pitch_angle      俯仰角（rad，向下为正）
   *   - detection.reprojected_points  主解重投影角点（4 点）
   *   - detection.reproj_error     主解重投影均方根误差（px）
   *   - detection.has_alt_solution  是否存在第二候选解
   *   - detection.xyz_in_gimbal_alt / reprojected_points_alt / reproj_error_alt
   *
   * @param detection  输入/输出，需预先填好 points / type
   * @return true      解算成功，detection.is_solved 被置为 true
   * @return false     解算失败（Init() 未调用，或 solvePnPGeneric 返回 0 个解）
   */
  bool Solve(Detection& detection) override;

  // 设置云台到世界的旋转
  void SetGimbalToWorldRotation(const Eigen::Quaterniond& quaternion);
  // 获取云台到世界的旋转
  [[nodiscard]] Eigen::Matrix3d GetGimbalToWorldRotation() const;

  // 设置云台到世界的平移
  void SetGimbalToWorldTranslation(const Eigen::Quaterniond& quaternion);
  // 获取云台到世界的平移
  [[nodiscard]] Eigen::Vector3d GetGimbalToWorldTranslation() const;

  /**
   * @brief 是否已完成 Init() 初始化
   *
   * 在 Solve() 调用前可用此方法检查对象是否就绪，避免未初始化时调用产生错误日志。
   */
  [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

  /**
   * @brief 在给定区间遍历 yaw，寻找重投影误差最小的 yaw 并更新 detection
   * @param detection 输入/输出，需先由 Solve 填充 xyz_in_gimbal
   * @param yaw_min 最小 yaw（弧度）
   * @param yaw_max 最大 yaw（弧度）
   * @param step 步长（弧度）
   */
  void OptimizeYaw(Detection& detection, double yaw_min, double yaw_max, double step);

  /**
   * @brief 计算指定 yaw/pitch 下的重投影误差
   * @param detection 目标检测结构体（需包含 points 和 type，以及已由 Solve 填写的 xyz_in_gimbal）
   * @param yaw 指定 yaw 角（rad）
   * @param pitch 指定 pitch 角（rad）
   * @param inclined 倾斜角（用于 SJTUCost，加权角度/像素误差），若 <= 0 则返回 RMS
   * @return 重投影误差（px）
   */
  [[nodiscard]] double ArmorReprojectionError(const Detection& detection, float yaw, float pitch,
                                              float inclined) const;

  // 将一组世界坐标投影到像素平面（使用外部提供的 世界->相机 旋转和平移）
  [[nodiscard]] std::vector<cv::Point2f> WorldToPixel(
      const std::vector<cv::Point3f>& world_pts, const Eigen::Matrix3d& R_world2camera,
      const Eigen::Vector3d& t_world2camera) const;

 private:
  // 重投影辅助函数：给定世界坐标与 yaw，返回四角点像素
  [[nodiscard]] std::vector<cv::Point2f> ReprojectArmor(const Eigen::Vector3d& xyz_in_world,
                                                         double yaw, ArmorType type) const;

  // 计算给定 yaw 的重投影 RMS，若 out_proj 非空写入投影点
  [[nodiscard]] double ArmorReprojectionRms(const Eigen::Vector3d& xyz_in_world, double yaw,
                                             ArmorType type,
                                             const std::vector<cv::Point2f>& img_pts,
                                             std::array<cv::Point2f, 4>* out_proj) const;
  // SJTU 代价函数（像素距离 + 角度差）
  [[nodiscard]] double SJTUCost(const std::vector<cv::Point2f>& cv_refs,
                                const std::vector<cv::Point2f>& cv_pts,
                                const double& inclined) const;

  // ── 内参 ──────────────────────────────────────────────────────────────────
  cv::Mat camera_matrix_;  ///< 3×3，CV_64F
  cv::Mat dist_coeffs_;    ///< 1×5，CV_64F

  // ── 外参（相机 → 云台坐标系）────────────────────────────────────────────
  // 默认：Y 轴翻转（相机 down+Y → 云台 up+Y），等效于旧版简化实现
  // 提供 calibration.R_camera_to_gimbal / t_camera_to_gimbal 后可使用精确外参
  Eigen::Matrix3d R_camera2gimbal_{
      (Eigen::Matrix3d() << 1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 1.0).finished()};
  Eigen::Vector3d t_camera2gimbal_{Eigen::Vector3d::Zero()};

  //云台→imu坐标系的旋转矩阵
  Eigen::Matrix3d R_gimbal2imubody_{Eigen::Matrix3d::Identity()};
  //云台→imu坐标系的平移向量（单位：米）
  Eigen::Vector3d t_gimbal2imubody_{Eigen::Vector3d::Zero()};

  //云台→世界坐标系的旋转矩阵
  Eigen::Matrix3d R_gimbal2world_{Eigen::Matrix3d::Identity()};
  //云台→世界坐标系的平移向量（单位：米）
  Eigen::Vector3d t_gimbal2world_{Eigen::Vector3d::Zero()};

  // ── 世界坐标模板 ──────────────────────────────────────────────────────────
  // 默认值与 vision.yaml armor 节点一致；Init() 读取 yaml 后覆盖。
  float small_half_w_{0.0675F};  ///< 小装甲板半宽（m）
  float big_half_w_{0.115F};     ///< 大装甲板半宽（m）
  float half_h_{0.0275F};        ///< 装甲板半高，大小装甲相同（m）

  // ── 时序锁定（Temporal Continuity）──────────────────────────────────────
  // 近距离时 IPPE 两解的重投影误差非常接近，单帧比较容易翻解。
  // 记录上一帧最终选定解在相机坐标系下的 tvec，用空间距离作为第二判据：
  //   两解都满足 Z>0 且重投影误差差距 < kReprErrHysteresis 时，
  //   优先选空间上离上一帧更近的解，阻止无谓翻转。
  cv::Mat last_tvec_;  ///< 上一帧选定解的 tvec（空则表示无历史记录）
  /// 重投影误差差距阈值（px）：差距 < 此值则启用时序锁定
  static constexpr double REPR_ERR_HYSTERESIS = 2.0;
  /// 位置跳变上限：两帧间距超过此值（m）则认为目标切换，重置历史
  static constexpr double MAX_FRAME_JUMP = 1.0;

  bool initialized_{false};
};

}  // namespace mv::modules
