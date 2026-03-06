/**
 * @file rm_shooter.hpp
 * @brief RoboMaster 协议串口发射器
 *
 * 【帧格式（占位协议，等待下位机队友确认后替换）】
 * @code
 *   Byte 0:     0xA5          帧头
 *   Byte 1-2:   int16_t       yaw   × 100（0.01° 精度）
 *   Byte 3-4:   int16_t       pitch × 100
 *   Byte 5:     uint8_t       fire  (0=不开火, 1=开火)
 *   Byte 6:     uint8_t       CRC8  (对 Byte0..5 做 XOR 校验)
 *   Byte 7:     0x5A          帧尾
 * @endcode
 *
 * 【弹道补偿】
 *   bullet_speed 和 gravity 从配置读取，对 pitch 做简单重力补偿：
 *   Δpitch = atan2(g × d², 2 × v0² × cos²θ) / cos²θ  （近似抛物线）
 *   补偿默认关闭（enable_ballistic_compensation: false）。
 *
 * 【YAML 配置字段】（来自 vision.yaml 的 auto_aim 节点）
 * @code
 *   auto_aim:
 *     shooter:
 *       enable_ballistic_compensation: false
 *       bullet_speed:  15.0   # m/s
 *       gravity:        9.8   # m/s²
 * @endcode
 *
 * 工厂键：`"rm"`
 */
#pragma once

#include "interfaces/i_shooter.hpp"

#include <cstdint>

namespace mv::modules {

class RmShooter final : public IShooter {
 public:
  RmShooter();
  ~RmShooter() override;

  RmShooter(const RmShooter&) = delete;
  RmShooter& operator=(const RmShooter&) = delete;
  RmShooter(RmShooter&&) = delete;
  RmShooter& operator=(RmShooter&&) = delete;

  bool Init(const YAML::Node& config) override;

  bool Send(hal::ISerial& serial, const GimbalControl& control) override;

  [[nodiscard]] bool IsInitialized() const noexcept override { return initialized_; }

 private:
  // ── 协议常量 ──────────────────────────────────────────────────────────────

  static constexpr uint8_t FRAME_HEADER = 0xA5U;
  static constexpr uint8_t FRAME_TAIL = 0x5AU;
  static constexpr std::size_t FRAME_LEN = 8U;

  // ── 参数 ─────────────────────────────────────────────────────────────────

  bool ballistic_comp_{false};
  float bullet_speed_{15.0F};
  float gravity_{9.8F};

  bool initialized_{false};

  // ── 内部方法 ──────────────────────────────────────────────────────────────

  /** 简单 XOR CRC8（对帧头到 fire 字节） */
  [[nodiscard]] static uint8_t CalcCrc8(const uint8_t* data, std::size_t len) noexcept;

  /** 弹道补偿：根据 pitch + distance 返回修正后的 pitch */
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] double BallisticCompensation(double pitch_rad, double target_dist) const noexcept;
};

}  // namespace mv::modules
