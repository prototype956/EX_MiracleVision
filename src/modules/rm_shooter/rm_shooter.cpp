/**
 * @file rm_shooter.cpp
 * @brief RoboMaster 协议串口发射器实现
 *
 * 【占位协议说明】
 *   当前帧格式为临时约定，等待下位机队友确认后替换（参见 docs 技术债说明）。
 *   帧总长 8 字节：[0xA5][yaw_lo][yaw_hi][pitch_lo][pitch_hi][fire][CRC8][0x5A]
 */
#include "rm_shooter.hpp"

#include "core/logger.hpp"
#include "factory/factory.hpp"
#include "hal/serial/i_serial.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace mv::modules {

namespace {
const bool RM_SHOOTER_REGISTERED = [] {
  ::mv::Factory<::mv::IShooter>::Register("rm", [] { return std::make_unique<RmShooter>(); });
  return true;
}();
}  // namespace

RmShooter::RmShooter() = default;
RmShooter::~RmShooter() = default;

// ── Init ──────────────────────────────────────────────────────────────────

bool RmShooter::Init(const YAML::Node& config) {
  if (config && config["auto_aim"] && config["auto_aim"]["shooter"]) {
    const auto& shooter = config["auto_aim"]["shooter"];
    if (shooter["enable_ballistic_compensation"]) {
      ballistic_comp_ = shooter["enable_ballistic_compensation"].as<bool>();
    }
    if (shooter["bullet_speed"]) {
      bullet_speed_ = shooter["bullet_speed"].as<float>();
    }
    if (shooter["gravity"]) {
      gravity_ = shooter["gravity"].as<float>();
    }
  }
  initialized_ = true;
  MV_LOG_INFO("RmShooter", "Init OK — ballistic_comp={} v0={:.1f}m/s g={:.2f}m/s²", ballistic_comp_,
              bullet_speed_, gravity_);
  return true;
}

// ── Send ──────────────────────────────────────────────────────────────────

bool RmShooter::Send(hal::ISerial& serial, const GimbalControl& control) {
  if (!serial.IsOpen()) {
    MV_LOG_DEBUG("RmShooter", "Serial not open, skipping send");
    return false;
  }

  // 弹道补偿（可选）
  double pitch_out = control.pitch;
  if (ballistic_comp_ && control.tracking && control.distance > 0.01) {
    pitch_out = BallisticCompensation(control.pitch, control.distance);
  }

  // 将 yaw/pitch 从弧度转换为 0.01° 精度的 int16_t
  static constexpr double RAD_TO_CDEG = 180.0 / M_PI * 100.0;  // rad → 0.01°

  const auto YAW_I16 = static_cast<int16_t>(control.yaw * RAD_TO_CDEG);
  const auto PITCH_I16 = static_cast<int16_t>(pitch_out * RAD_TO_CDEG);
  const auto FIRE_U8 = static_cast<uint8_t>(control.fire ? 1U : 0U);

  // 组帧（小端序）
  std::array<uint8_t, FRAME_LEN> frame{};
  frame[0] = FRAME_HEADER;
  frame[1] = static_cast<uint8_t>(YAW_I16 & 0xFF);
  frame[2] = static_cast<uint8_t>((YAW_I16 >> 8) & 0xFF);
  frame[3] = static_cast<uint8_t>(PITCH_I16 & 0xFF);
  frame[4] = static_cast<uint8_t>((PITCH_I16 >> 8) & 0xFF);
  frame[5] = FIRE_U8;
  frame[6] = CalcCrc8(frame.data(), 6);
  frame[7] = FRAME_TAIL;

  const bool SEND_OK = serial.Send(frame.data(), FRAME_LEN);
  if (!SEND_OK) {
    MV_LOG_WARN("RmShooter", "Serial.Send() failed (yaw={:.2f}° pitch={:.2f}° fire={})",
                control.yaw * 180.0 / M_PI, pitch_out * 180.0 / M_PI, FIRE_U8);
  }
  return SEND_OK;
}

// ── 内部方法 ──────────────────────────────────────────────────────────────

uint8_t RmShooter::CalcCrc8(const uint8_t* data, std::size_t len) noexcept {
  uint8_t crc = 0x00U;
  for (std::size_t idx = 0; idx < len; ++idx) {
    crc ^= data[idx];
  }
  return crc;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double RmShooter::BallisticCompensation(double pitch_rad, double target_dist) const noexcept {
  // 简化重力补偿：Δpitch ≈ atan(g×d / (2×v0²)) （小角度近似）
  if (bullet_speed_ < 0.1F) {
    return pitch_rad;
  }
  const double DELTA_V =
      static_cast<double>(gravity_) * target_dist /
      (2.0 * static_cast<double>(bullet_speed_) * static_cast<double>(bullet_speed_));
  return pitch_rad + std::atan(DELTA_V);
}

}  // namespace mv::modules
