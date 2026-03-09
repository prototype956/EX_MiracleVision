/**
 * @file image_publisher.hpp
 * @brief 图像发布子模块（cv::Mat → foxglove RawImage）
 *
 * 【设计说明】
 *
 *   FoxgloveSink 可能同时向多个 topic 发布图像（如 "camera/raw" 和
 *   "camera/debug"）。每个 topic 对应一个独立的 RawImageChannel，
 *   采用懒创建策略（首次 Publish 时才 advertise），避免在无人订阅时
 *   无谓创建频道占用 SDK 资源。
 *
 *   编码检测：根据 cv::Mat::type() 自动推断 Foxglove encoding 字符串，
 *   支持 BGR8 / MONO8 / 16UC1 / BGRA8，未知类型回退到 bgr8。
 *
 *   线程安全：mtx_ 保护 channels_ 映射表，支持多线程并发 Publish。
 *   注意：同一 topic 的并发调用在锁内串行，性能敏感路径需避免高频并发。
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
  /** @param ctx  foxglove 全局上下文，由 FoxgloveSink::Impl 传入并共享 */
  explicit ImagePublisher(foxglove::Context ctx);

  /**
   * @brief 发布图像到指定 topic
   *
   * 若 img 为空或 channel 创建失败，静默忽略（不抛异常）。
   *
   * @param img       OpenCV Mat（BGR8 / MONO8 / 16UC1）
   * @param topic     Foxglove topic 名称，如 "camera/raw"
   * @param frame_id  坐标系名称，写入 RawImage.frame_id
   * @param ts_ns     时间戳（纳秒，须已由 ResolveTs 解析为绝对时间）
   */
  void Publish(const cv::Mat& img, const std::string& topic, const std::string& frame_id,
               uint64_t ts_ns);

 private:
  /**
   * @brief 根据 Mat 类型推断 Foxglove 编码字符串
   *
   * Foxglove RawImage encoding 取值参见：
   * https://docs.foxglove.dev/docs/visualization/message-schemas/raw-image
   * 常见映射：CV_8UC3→bgr8，CV_8UC1→mono8，CV_16UC1→16UC1，CV_8UC4→bgra8。
   */
  [[nodiscard]] static std::string DetectEncoding(const cv::Mat& img);

  /**
   * @brief 懒创建或取出已有 channel，调用前须持有 mtx_
   *
   * @return 成功返回 channel 指针，创建失败返回 nullptr（会在 Publish 中被静默忽略）
   */
  foxglove::schemas::RawImageChannel* GetOrCreateChannel(const std::string& topic);

  // ── 成员变量 ─────────────────────────────────────────────────────────────
  foxglove::Context ctx_;                                                    ///< SDK 全局上下文（共享引用计数）
  std::unordered_map<std::string, foxglove::schemas::RawImageChannel> channels_;  ///< topic → channel 映射（懒创建）
  std::mutex mtx_;                                                           ///< 保护 channels_ 的互斥锁
};

}  // namespace mv::tool::detail
