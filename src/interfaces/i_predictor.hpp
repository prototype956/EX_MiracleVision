/**
 * @file i_predictor.hpp
 * @brief 目标跟踪与预测抽象接口 (IPredictor)
 *
 * 【职责边界】
 *   IPredictor 负责"跨帧跟踪目标并输出预判的云台角度"：
 *   - 输入：本帧 Detection 列表（含 3D 坐标）+ 当前时间戳
 *   - 输出：GimbalControl（要瞄准的位置 + 是否开火）
 *           TrackTarget（跟踪状态，调试/可视化用，可选）
 *
 *   预测器维护跟踪器状态机（detecting → tracking → temp_lost → lost），
 *   使用扩展卡尔曼滤波器（EKF）平滑轨迹并补偿飞行时间延迟。
 *
 * 【时间戳设计】
 *   使用 std::chrono::steady_clock（单调时钟），而不是系统时钟：
 *   - 不受 NTP 时间跳变影响，dt 计算稳定；
 *   - Pipeline 在 Grab() 后立即打时间戳，送入 Predict() 时已携带帧时间。
 *
 * 【实现约定】
 *   - Reset() 清除跟踪状态，丢失目标后重新开始；
 *   - Predict() 无目标时返回 GimbalControl{tracking=false}；
 *   - 预测器非线程安全，在专属线程（检测线程）顺序调用。
 */
#pragma once

#include "types.hpp"

#include <chrono>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace mv {

class IPredictor {
 public:
  // ── 生命周期 ─────────────────────────────────────────────────────────────
  IPredictor() = default;
  virtual ~IPredictor() = default;

  IPredictor(const IPredictor&) = delete;
  IPredictor& operator=(const IPredictor&) = delete;

 protected:
  IPredictor(IPredictor&&) = default;
  IPredictor& operator=(IPredictor&&) = default;

 public:
  // ── 核心接口 ─────────────────────────────────────────────────────────────

  /**
   * @brief 初始化预测器（加载 EKF 参数、优先级配置）
   * @param config  YAML 配置节点
   * @return true 成功
   */
  virtual bool Init(const YAML::Node& config) = 0;

  /**
   * @brief 用本帧检测结果更新跟踪器，返回云台控制指令
   *
   * @param detections  本帧已完成 PnP 解算的 Detection 列表
   * @param timestamp   本帧的采集时间（steady_clock，在 Grab() 后立即记录）
   * @param enemy_color 敌方颜色，用于过滤非目标颜色的检测结果
   * @return GimbalControl  tracking=true 时包含有效的 yaw/pitch；
   *                        tracking=false 时上位机应保持当前姿态
   */
  [[nodiscard]] virtual GimbalControl Predict(const std::vector<Detection>& detections,
                                              std::chrono::steady_clock::time_point timestamp,
                                              ArmorColor enemy_color) = 0;

  /**
   * @brief 获取当前跟踪目标的详细状态（用于 Foxglove 可视化）
   * @return TrackTarget 状态快照
   */
  [[nodiscard]] virtual TrackTarget GetTrackTarget() const = 0;

  /**
   * @brief 重置跟踪器状态（目标切换、模式切换时调用）
   *
   * 调用后下一次 Predict() 将从 detecting 状态重新开始。
   */
  virtual void Reset() = 0;

  /** @return 预测器是否已完成初始化 */
  [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
};

}  // namespace mv
