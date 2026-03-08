/**
 * @file image_publisher.cpp
 * @brief ImagePublisher 实现
 */
#include "tool/foxglove/detail/image_publisher.hpp"

#include "tool/foxglove/detail/utils.hpp"

#include <cstring>

#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>

namespace mv::tool::detail {

ImagePublisher::ImagePublisher(foxglove::Context ctx) : ctx_(std::move(ctx)) {}

std::string ImagePublisher::DetectEncoding(const cv::Mat& img) {
  switch (img.type()) {
    case CV_8UC3:
      return "bgr8";
    case CV_8UC1:
      return "mono8";
    case CV_16UC1:
      return "16UC1";
    case CV_8UC4:
      return "bgra8";
    default:
      return "bgr8";  // 最通用的回退值
  }
}

foxglove::schemas::RawImageChannel* ImagePublisher::GetOrCreateChannel(const std::string& topic) {
  auto it = channels_.find(topic);
  if (it != channels_.end()) {
    return &it->second;
  }

  auto result = foxglove::schemas::RawImageChannel::create(topic, ctx_);
  if (!result.has_value()) {
    spdlog::error("[ImagePublisher] Failed to create channel '{}': {}", topic,
                  foxglove::strerror(result.error()));
    return nullptr;
  }
  auto [eit, ok] = channels_.emplace(topic, std::move(result.value()));
  return ok ? &eit->second : nullptr;
}

void ImagePublisher::Publish(const cv::Mat& img, const std::string& topic,
                             const std::string& frame_id, uint64_t ts_ns) {
  if (img.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mtx_);
  auto* ch = GetOrCreateChannel(topic);
  if (ch == nullptr) {
    return;
  }

  // 构建 RawImage 消息
  foxglove::schemas::RawImage msg;
  msg.frame_id = frame_id;
  msg.width = static_cast<uint32_t>(img.cols);
  msg.height = static_cast<uint32_t>(img.rows);
  msg.step = static_cast<uint32_t>(img.step);
  msg.encoding = DetectEncoding(img);
  msg.timestamp = ToTs(ts_ns);

  const size_t data_size = img.total() * img.elemSize();
  msg.data.resize(data_size);
  std::memcpy(msg.data.data(), img.data, data_size);

  ch->log(msg, ts_ns);
}

}  // namespace mv::tool::detail
