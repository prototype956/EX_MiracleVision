/**
 * @file serial_visualizer.hpp
 * @brief 串口上行帧接收可视化助手（Foxglove Topic 推送）
 *
 * 【职责】
 *   聚合每帧串口接收事件，通过 FoxgloveSink 推送三路调试 Topic：
 *     - serial/rx_status  : 最近一帧上行数据解析结果（含全部字段、物理单位）
 *     - serial/rx_raw_hex : 最近一帧原始字节十六进制转储
 *     - serial/stats      : 累计统计（帧率、解析成功率、序号跳变计数等）
 *
 * 【典型用法】
 * @code
 *   mv::tool::SerialVisualizer serial_viz;
 *
 *   // 主循环 — 串口接收处
 *   mv::protocol::UpFrame up_frame{};
 *   bool ok = mv::protocol::TryParseUpFrame(buf, received, &up_frame);
 *   serial_viz.OnRxData(buf, received, ok ? &up_frame : nullptr, ok);
 *
 *   // Foxglove 发布处（节流门控后）
 *   serial_viz.Publish(sink, ts_ns);
 * @endcode
 */
#pragma once

#include "hal/serial/rm_protocol.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mv::tool {

// 前向声明，避免引入 foxglove_sink.hpp 的重量级依赖
class FoxgloveSink;

/**
 * @brief 串口上行帧接收可视化助手
 *
 * 非线程安全：与主循环同线程使用，无需加锁。
 */
class SerialVisualizer {
 public:
  /**
   * @brief 记录一次串口接收事件
   *
   * 每次非阻塞 Recv() 返回数据后调用，无论解析成功与否。
   *
   * @param raw_data  原始字节缓冲区首地址
   * @param len       本次接收的有效字节数
   * @param up_frame  解析成功时传入解析结果指针；失败时为 nullptr
   * @param parse_ok  true = 帧头 / 帧尾 / CRC 全部校验通过
   */
  void OnRxData(const uint8_t* raw_data, std::size_t len,
                const mv::protocol::UpFrame* up_frame, bool parse_ok);

  /**
   * @brief 将上行串口可视化数据推送到 Foxglove
   *
   * 调用前无需判断 HasClients()，FoxgloveSink 内部已有门控。
   *
   * @param sink   FoxgloveSink 引用
   * @param ts_ns  帧时间戳（纳秒）
   */
  void Publish(FoxgloveSink& sink, int64_t ts_ns) const;

 private:
  // ── 最近一帧原始字节 ────────────────────────────────────────────────────────
  std::vector<uint8_t> last_rx_raw_{};

  // ── 最近一帧解析结果 ────────────────────────────────────────────────────────
  mv::protocol::UpFrame last_up_frame_{};
  bool last_parse_ok_{false};

  // ── 累计统计 ─────────────────────────────────────────────────────────────────
  struct RxStats {
    uint64_t rx_total{0};        ///< Recv() 有数据的调用总次数
    uint64_t rx_parse_ok{0};     ///< 成功解析为 UpFrame 的次数
    uint64_t parse_fail_count{0};  ///< 解析失败次数（含 CRC 错误、帧格式错误）
    uint64_t seq_jump_count{0};  ///< seq 不连续次数
    uint8_t last_seq{0};         ///< 上一帧序号（用于 seq jump 检测）
    bool seq_initialized{false};  ///< 是否已收到第一帧有效 seq

    // 用于计算 rx_hz 的时间基准（仅含成功解析帧）
    std::chrono::steady_clock::time_point first_ok_time{};
    std::chrono::steady_clock::time_point last_ok_time{};
    bool timing_initialized{false};
  };

  RxStats stats_{};
};

}  // namespace mv::tool
