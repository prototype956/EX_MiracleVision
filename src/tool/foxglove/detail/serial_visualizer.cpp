/**
 * @file serial_visualizer.cpp
 * @brief 串口上行帧接收可视化助手实现
 */
#include "tool/foxglove/detail/serial_visualizer.hpp"

#include "tool/foxglove/foxglove_sink.hpp"

#include <chrono>
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

namespace mv::tool {

// ── OnRxData ─────────────────────────────────────────────────────────────────

void SerialVisualizer::OnRxData(const uint8_t* raw_data, std::size_t len,
                                const mv::protocol::UpFrame* up_frame, bool parse_ok) {
  ++stats_.rx_total;
  last_rx_raw_.assign(raw_data, raw_data + len);
  last_parse_ok_ = parse_ok;

  if (parse_ok && up_frame != nullptr) {
    ++stats_.rx_parse_ok;
    last_up_frame_ = *up_frame;

    // seq 跳变检测（已初始化后才检测，忽略 255→0 的正常回绕）
    if (stats_.seq_initialized) {
      const auto EXPECTED = static_cast<uint8_t>(stats_.last_seq + 1U);
      if (up_frame->seq != EXPECTED) {
        ++stats_.seq_jump_count;
      }
    }
    stats_.last_seq = up_frame->seq;
    stats_.seq_initialized = true;

    // 时序基准
    const auto NOW = std::chrono::steady_clock::now();
    if (!stats_.timing_initialized) {
      stats_.first_ok_time = NOW;
      stats_.timing_initialized = true;
    }
    stats_.last_ok_time = NOW;
  } else {
    ++stats_.parse_fail_count;
  }
}

// ── Publish ──────────────────────────────────────────────────────────────────

void SerialVisualizer::Publish(FoxgloveSink& sink, int64_t ts_ns) const {
  // ── serial/rx_status ─────────────────────────────────────────────────────
  {
    nlohmann::json rx_status;
    rx_status["parse_ok"] = last_parse_ok_;
    rx_status["rx_bytes"] = last_rx_raw_.size();

    if (last_parse_ok_) {
      constexpr double QUATERNION_SCALE = 1.0 / 10000.0;
      constexpr double ANGLE_SCALE = 1.0 / 10000.0;
      constexpr double BULLET_SPEED_SCALE = 1.0 / 100.0;

      rx_status["seq"] = last_up_frame_.seq;
      rx_status["color"] = last_up_frame_.color;
      rx_status["mode"] = static_cast<uint8_t>(last_up_frame_.mode);
      rx_status["robot_id"] = static_cast<uint8_t>(last_up_frame_.robot_id);
      rx_status["bullet_speed_mps"] = last_up_frame_.bullet_speed * BULLET_SPEED_SCALE;
      rx_status["q_w"] = last_up_frame_.q_w * QUATERNION_SCALE;
      rx_status["q_x"] = last_up_frame_.q_x * QUATERNION_SCALE;
      rx_status["q_y"] = last_up_frame_.q_y * QUATERNION_SCALE;
      rx_status["q_z"] = last_up_frame_.q_z * QUATERNION_SCALE;
      rx_status["yaw_rad"] = last_up_frame_.yaw * ANGLE_SCALE;
      rx_status["pitch_rad"] = last_up_frame_.pitch * ANGLE_SCALE;
      rx_status["yaw_vel_rps"] = last_up_frame_.yaw_vel * ANGLE_SCALE;
      rx_status["pitch_vel_rps"] = last_up_frame_.pitch_vel * ANGLE_SCALE;
    }

    if (stats_.timing_initialized) {
      const auto MS_AGO = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - stats_.last_ok_time)
                              .count();
      rx_status["last_rx_ms_ago"] = MS_AGO;
    }

    sink.PublishJson("serial/rx_status", rx_status, ts_ns);
  }

  // ── serial/rx_raw_hex ────────────────────────────────────────────────────
  if (!last_rx_raw_.empty()) {
    static constexpr std::string_view HEX_DIGITS = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(last_rx_raw_.size() * 3U);
    for (std::size_t i = 0; i < last_rx_raw_.size(); ++i) {
      if (i > 0) {
        hex += ' ';
      }
      hex += HEX_DIGITS[(last_rx_raw_[i] >> 4U) & 0x0FU];
      hex += HEX_DIGITS[last_rx_raw_[i] & 0x0FU];
    }

    nlohmann::json raw_hex;
    raw_hex["len"] = last_rx_raw_.size();
    raw_hex["hex"] = hex;
    sink.PublishJson("serial/rx_raw_hex", raw_hex, ts_ns);
  }

  // ── serial/stats ─────────────────────────────────────────────────────────
  {
    nlohmann::json stats;
    stats["rx_total"] = stats_.rx_total;
    stats["rx_parse_ok"] = stats_.rx_parse_ok;
    stats["parse_fail_count"] = stats_.parse_fail_count;
    stats["seq_jump_count"] = stats_.seq_jump_count;

    const auto PARSE_RATE = (stats_.rx_total > 0) ? static_cast<double>(stats_.rx_parse_ok) /
                                                        static_cast<double>(stats_.rx_total)
                                                  : 0.0;
    stats["parse_success_rate"] = PARSE_RATE;

    // rx_hz：成功解析帧的长期平均帧率
    double rx_hz = 0.0;
    if (stats_.rx_parse_ok > 1 && stats_.timing_initialized) {
      const auto ELAPSED_S =
          std::chrono::duration<double>(stats_.last_ok_time - stats_.first_ok_time).count();
      if (ELAPSED_S > 0.0) {
        rx_hz = static_cast<double>(stats_.rx_parse_ok - 1U) / ELAPSED_S;
      }
    }
    stats["rx_hz"] = rx_hz;

    sink.PublishJson("serial/stats", stats, ts_ns);
  }
}

}  // namespace mv::tool
