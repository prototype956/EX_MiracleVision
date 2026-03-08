/**
 * @file image_publisher.hpp
 * @brief 图像发布子模块（cv::Mat → foxglove RawImage）
 *
 * 按 topic 懒创建 RawImageChannel，支持 bgr8 / mono8 / 16UC1 自动编码检测。
 * 线程安全：内部 mutex 保护 channel 映射表。
 */
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include <foxglove/context.hpp>
#include <foxglove/schemas.hpp>
#include <opencv2/core.hpp>

namespace mv::tool::detail {

class ImagePublisher {
 public:
  explicit ImagePublisher(foxglove::Context ctx);

  /**
   * @brief 发布图像到指定 topic
   * @param img       OpenCV Mat（BGR8 / MONO8 / 16UC1）
   * @param topic     Foxglove topic 名称
   * @param frame_id  坐标系名称
   * @param ts_ns     时间戳（纳秒）
   */
  void Publish(const cv::Mat& img, const std::string& topic, const std::string& frame_id,
               uint64_t ts_ns);

 private:
  /** 根据 Mat 类型推断 Foxglove 编码字符串 */
  [[nodiscard]] static std::string DetectEncoding(const cv::Mat& img);

  /** 懒创建或取出 channel，调用前须持有 mtx_ */
  foxglove::schemas::RawImageChannel* GetOrCreateChannel(const std::string& topic);

  foxglove::Context ctx_;
  std::unordered_map<std::string, foxglove::schemas::RawImageChannel> channels_;
  std::mutex mtx_;
};

}  // namespace mv::tool::detail
