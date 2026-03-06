/**
 * @file basic_armor_detector.hpp
 * @brief 传统视觉装甲板检测器（灯条轮廓法）
 *
 * 【算法流程】
 *   1. 按敌方颜色做通道差（R-B 或 B-R），过滤非目标颜色灯条；
 *   2. 二值化 → findContours → RotatedRect；
 *   3. 按面积、长宽比、倾斜角过滤灯条；
 *   4. 两两配对：检查角度差、高度比、装甲宽高比；
 *   5. 输出 Detection（2D 四角点 + 置信度 = 1.0，3D 部分由 ISolver 填充）。
 *
 * 【YAML 配置字段（均有默认值，可不提供）】
 * @code
 *   detector:
 *     light_thresh:    160    # 亮度二值化阈值
 *     min_light_ratio: 0.05   # 灯条短/长轴最小比（过细则滤除）
 *     max_light_ratio: 0.40   # 灯条短/长轴最大比（过粗则滤除）
 *     max_light_angle: 40.0   # 灯条允许的最大倾斜度（相对垂直，°）
 *     min_armor_ratio: 1.0    # 装甲板宽高比下界（档板间距 / 平均灯条长）
 *     max_armor_ratio: 5.5    # 装甲板宽高比上界
 *     max_angle_diff:  8.0    # 两灯条角度之差上界（°）
 *     min_area:        10.0   # 灯条轮廓最小面积（px²，过滤噪点）
 * @endcode
 *
 * 工厂键：`"basic"`
 *   使用 MV_REGISTER_DETECTOR("basic", BasicArmorDetector) 在 .cpp 中注册。
 */
#pragma once

#include "factory/factory.hpp"
#include "interfaces/i_detector.hpp"

#include <vector>

#include <opencv2/core.hpp>

namespace mv::modules {

class BasicArmorDetector final : public IDetector {
 public:
  BasicArmorDetector();
  ~BasicArmorDetector() override;

  BasicArmorDetector(const BasicArmorDetector&) = delete;
  BasicArmorDetector& operator=(const BasicArmorDetector&) = delete;
  BasicArmorDetector(BasicArmorDetector&&) = delete;
  BasicArmorDetector& operator=(BasicArmorDetector&&) = delete;

  // ── IDetector 接口实现 ────────────────────────────────────────────────────

  bool Init(const YAML::Node& config) override;

  [[nodiscard]] std::vector<Detection> Detect(const cv::Mat& frame,
                                              ArmorColor enemy_color) override;

  [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

  // ── 可热调参数（调试工具读写）────────────────────────────────────────────

  /** 所有可调参数聚合在此，便于 SetParams/GetParams 原子传递 */
  struct Params {
    int light_thresh{160};
    float min_light_ratio{0.05F};
    float max_light_ratio{0.40F};
    float max_light_angle{40.0F};
    float min_armor_ratio{1.0F};
    float max_armor_ratio{5.5F};
    float max_angle_diff{8.0F};
    float min_area{10.0F};
  };

  void SetParams(const Params& p) noexcept { params_ = p; }
  [[nodiscard]] const Params& GetParams() const noexcept { return params_; }

  // ── 调试中间数据（仅 EnableDebug(true) 后有效）──────────────────────────

  /** 每帧 Detect() 的三张中间图（debug_enabled_=false 时为空）*/
  struct DebugData {
    cv::Mat diff;        ///< 通道差分图（灰度单通道）
    cv::Mat binary;      ///< 二值化 + 膨胀图
    cv::Mat lights_vis;  ///< 原图叠加所有通过过滤的灯条轮廓
  };

  void EnableDebug(bool on) noexcept { debug_enabled_ = on; }
  [[nodiscard]] const DebugData& GetDebugData() const noexcept { return debug_data_; }

 private:
  // ── 内部数据结构 ──────────────────────────────────────────────────────────

  /** 灯条描述符：RotatedRect + 归一化后的倾斜角 */
  struct LightBar {
    cv::RotatedRect rect;
    cv::Point2f top;     ///< 灯条顶端中点（图像坐标，y 较小）
    cv::Point2f bottom;  ///< 灯条底端中点（图像坐标，y 较大）
    float length{0.0F};  ///< 长轴（高度）
    float width{0.0F};   ///< 短轴（宽度）
    float tilt{0.0F};    ///< 相对垂直方向的倾斜角（°，垂直=0，水平=90）
  };

  // ── 配置参数（带默认值）──────────────────────────────────────────────────

  Params params_;
  bool debug_enabled_{false};
  DebugData debug_data_;
  bool initialized_{false};

  // ── 内部方法 ──────────────────────────────────────────────────────────────

  /** 从二值图中提取所有通过过滤的灯条 */
  [[nodiscard]] std::vector<LightBar> FindLightBars(const cv::Mat& binary) const;

  /** 判断两个灯条是否构成合法装甲板，注意 left.center.x < right.center.x */
  [[nodiscard]] bool IsValidArmor(const LightBar& left, const LightBar& right) const;

  /** 由一对灯条构造 Detection（不填充 3D 信息） */
  [[nodiscard]] static Detection MakeDetection(const LightBar& left, const LightBar& right);

  /** 构建通道差分图：red=R-B, blue=B-R, unknown=grayscale */
  [[nodiscard]] static cv::Mat BuildChannelDiff(const cv::Mat& bgr, ArmorColor color);
};

}  // namespace mv::modules
