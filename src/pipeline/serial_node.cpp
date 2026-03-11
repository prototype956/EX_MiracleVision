/**
 * @file serial_node.cpp
 * @brief SerialNode 工作线程实现
 *
 * 【上行帧格式（占位实现）】
 *   目前 TryRecv() 使用最小上行协议：
 *     [0] 0xAA        帧头
 *     [1] enemy_color  (0=RED, 1=BLUE)
 *     [2] mode         (0=AUTO_AIM, 1=ENERGY_BUFF, 2=IDLE)
 *     [3] bullet_speed 整数部分（m/s * 10，如 150 = 15.0 m/s）
 *     [4] 0x55        帧尾
 *
 *   正式比赛协议（具体帧格式由下位机队友确认后修改此文件），
 *   原则是只修改 TryRecv() 的解析部分，节点框架保持不变。
 */
#include "serial_node.hpp"

#include "core/logger.hpp"

#include <array>
#include <thread>

namespace mv::pipeline {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static constexpr auto POP_TIMEOUT = std::chrono::milliseconds{5};

// ── 上行帧结构（占位）────────────────────────────────────────────────────────
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static constexpr size_t RECV_FRAME_LEN = 5;
static constexpr uint8_t RECV_HEADER = 0xAAU;
static constexpr uint8_t RECV_FOOTER = 0x55U;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

SerialNode::SerialNode(std::unique_ptr<hal::ISerial> serial, std::unique_ptr<IShooter> shooter,
                       std::shared_ptr<Channel<ControlPacket>> input_ch, SharedState& state,
                       int max_send_fail)
    : PipelineNode("SerialNode"),
      serial_(std::move(serial)),
      shooter_(std::move(shooter)),
      input_ch_(std::move(input_ch)),
      state_(state),
      max_send_fail_(max_send_fail) {}

void SerialNode::OnStop() {
  if (input_ch_) {
    input_ch_->Shutdown();
  }
}

void SerialNode::WorkLoop() {
  MV_LOG_INFO("SerialNode", "Worker started.");

  int send_fail_count = 0;

  while (!ShouldStop()) {
    ControlPacket ctrl_pkt;
    if (!input_ch_->Pop(ctrl_pkt, POP_TIMEOUT)) {
      // 超时时也尝试接收上行（保持串口接收响应性）
      TryRecv();
      continue;
    }

    // ── 下行：发送云台控制指令 ────────────────────────────────────────────
    const bool SENT = shooter_->Send(*serial_, ctrl_pkt.control);
    if (!SENT) {
      ++send_fail_count;
      MV_LOG_WARN("SerialNode", "Send failed ({}/{}).", send_fail_count, max_send_fail_);
      if (send_fail_count >= max_send_fail_) {
        MV_LOG_ERROR("SerialNode", "Serial send failed {} times. Setting error.", max_send_fail_);
        SetError(2);
        break;
      }
    } else {
      send_fail_count = 0;
    }

    // ── 上行：尝试接收下位机反馈 ─────────────────────────────────────────
    TryRecv();

    IncrementProcessed();
  }

  MV_LOG_INFO("SerialNode", "Worker stopped. Processed {} packets.", ProcessedCount());
}

void SerialNode::TryRecv() {
  if (!serial_ || !serial_->IsOpen()) {
    return;
  }

  std::array<uint8_t, RECV_FRAME_LEN> buf{};
  std::size_t received = 0;

  const bool GOT_DATA = serial_->Recv(buf.data(), RECV_FRAME_LEN, received);
  if (!GOT_DATA || received < RECV_FRAME_LEN) {
    return;  // 超时或数据不足，等下一帧
  }

  // 帧头/帧尾校验
  if (buf[0] != RECV_HEADER || buf[RECV_FRAME_LEN - 1] != RECV_FOOTER) {
    MV_LOG_WARN("SerialNode", "Invalid recv frame header/footer: {:#02x} ... {:#02x}", buf[0],
                buf[RECV_FRAME_LEN - 1]);
    return;
  }

  // 解析敌方颜色（0=RED, 1=BLUE, 其他=UNKNOWN）
  const uint8_t COLOR_RAW = buf[1];
  if (COLOR_RAW == 0) {
    state_.enemy_color.store(ArmorColor::RED);
  } else if (COLOR_RAW == 1) {
    state_.enemy_color.store(ArmorColor::BLUE);
  }
  // 其他値保持原有颜色，不更新（防止帧错误污染状态）

  // TODO(Stage 8-F): 解析 IMU 四元数并写入 state_.SetGimbalQuat()
  //   下位机上行协议内容（步骤三元组/四元数字段，字节序等）待与下位机队友确认后完善。
  //   现在 Identity 占位，等效于无坐标系转换修正。
  //
  // 示例（未来实现时替换此块）:
  //   Eigen::Quaterniond q;
  //   q.w() = ...; q.x() = ...; q.y() = ...; q.z() = ...;
  //   state_.SetGimbalQuat(q);

  // 注：mode 和 bullet_speed 字段留给 Stage 5（状态机）处理
  // buf[2] = mode，buf[3] = bullet_speed，暂不使用
}

}  // namespace mv::pipeline
