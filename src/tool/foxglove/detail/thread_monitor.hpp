/**
 * @file thread_monitor.hpp
 * @brief Pipeline 线程健康指标发布子模块
 *
 * 推送到 `pipeline/nodes` topic（JSON 编码），
 * Foxglove 原始 JSON 面板可查看各节点 fps / latency / drop / alive 状态。
 * drop_count > 0 时在 JSON 中以 "warn" 字段标注。
 */
#pragma once

#include "tool/foxglove/foxglove_sink.hpp"  // FoxgloveSink::ThreadMetrics

#include <mutex>
#include <vector>

#include <foxglove/channel.hpp>
#include <foxglove/context.hpp>
#include <optional>

namespace mv::tool::detail {

class ThreadMonitor {
 public:
  explicit ThreadMonitor(foxglove::Context ctx);

  /**
   * @brief 发布各节点健康指标快照
   * @param metrics  节点指标列表（由 VisionPipeline 汇聚）
   * @param ts_ns    时间戳（纳秒）
   */
  void Publish(const std::vector<mv::tool::FoxgloveSink::ThreadMetrics>& metrics, uint64_t ts_ns);

 private:
  void EnsureChannel();

  foxglove::Context ctx_;
  std::optional<foxglove::RawChannel> channel_;
  std::mutex mtx_;
};

}  // namespace mv::tool::detail
