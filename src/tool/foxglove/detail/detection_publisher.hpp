/**
 * @file detection_publisher.hpp
 * @brief 装甲板检测结果发布子模块
 *
 * 同时推送两个 topic：
 * - detections/annotations (ImageAnnotations)：2D 角点多边形 + 文字标签
 * - detections/3d          (SceneUpdate)      ：已解算装甲板的 3D 立方体 + 坐标标签
 */
#pragma once

#include "interfaces/types.hpp"

#include <mutex>
#include <vector>

#include <foxglove/context.hpp>
#include <foxglove/schemas.hpp>
#include <optional>

namespace mv::tool::detail {

class DetectionPublisher {
 public:
  explicit DetectionPublisher(foxglove::Context ctx);

  /**
   * @brief 发布检测结果
   * @param dets    检测结果列表
   * @param ts_ns   时间戳（纳秒）
   */
  void Publish(const std::vector<mv::Detection>& dets, uint64_t ts_ns);

 private:
  void EnsureChannels();

  foxglove::Context ctx_;
  std::optional<foxglove::schemas::ImageAnnotationsChannel> annot_ch_;
  std::optional<foxglove::schemas::SceneUpdateChannel> scene_ch_;
  std::mutex mtx_;
};

}  // namespace mv::tool::detail
