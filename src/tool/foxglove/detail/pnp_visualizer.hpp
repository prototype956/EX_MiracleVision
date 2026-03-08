/**
 * @file pnp_visualizer.hpp
 * @brief PnP 解算专项可视化（三层输出）
 *
 * 输出：
 *   pnp/debug_image (RawImage)   — 底图 + 绿色原始角点标注
 *   pnp/axes_3d     (SceneUpdate)— 每个已解算装甲板的 RGB XYZ 坐标轴箭头
 *   pnp/residuals   (JSON)       — 装甲板位姿 / 深度 / 角度调试数据
 */
#pragma once

#include "interfaces/types.hpp"

#include <mutex>
#include <vector>

#include <foxglove/channel.hpp>
#include <foxglove/context.hpp>
#include <foxglove/schemas.hpp>
#include <opencv2/core.hpp>
#include <optional>

namespace mv::tool::detail {

class PnpVisualizer {
 public:
  explicit PnpVisualizer(foxglove::Context ctx);

  /**
   * @brief 发布 PnP 调试三层数据
   * @param dets    检测结果（仅 is_solved=true 的影响 3D 输出）
   * @param frame   原始底图（用于 debug_image 绘制）
   * @param ts_ns   时间戳（纳秒）
   */
  void Publish(const std::vector<mv::Detection>& dets, const cv::Mat& frame, uint64_t ts_ns);

 private:
  void EnsureChannels();

  foxglove::Context ctx_;
  std::optional<foxglove::schemas::RawImageChannel> debug_img_ch_;
  std::optional<foxglove::schemas::SceneUpdateChannel> axes_ch_;
  std::optional<foxglove::RawChannel> residuals_ch_;
  std::mutex mtx_;
};

}  // namespace mv::tool::detail
