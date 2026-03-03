/**
 * @file detect_node.cpp
 * @brief DetectNode 工作线程实现
 */
#include "detect_node.hpp"

#include "core/logger.hpp"

namespace mv::pipeline {

static constexpr auto kPopTimeout = std::chrono::milliseconds{10};

DetectNode::DetectNode(std::unique_ptr<IDetector> detector,
                       std::unique_ptr<ISolver> solver,
                       std::shared_ptr<Channel<FramePacket>> input_ch,
                       std::shared_ptr<Channel<DetectPacket>> output_ch,
                       std::atomic<ArmorColor>& enemy_color)
    : PipelineNode("DetectNode"),
      detector_(std::move(detector)),
      solver_(std::move(solver)),
      input_ch_(std::move(input_ch)),
      output_ch_(std::move(output_ch)),
      enemy_color_(enemy_color) {}

void DetectNode::OnStop() {
  if (input_ch_) {
    input_ch_->Shutdown();
  }
  if (output_ch_) {
    output_ch_->Shutdown();
  }
}

void DetectNode::WorkLoop() {
  MV_LOG_INFO("DetectNode", "Worker started.");

  while (!stop_requested_.load()) {
    FramePacket frame_pkt;
    if (!input_ch_->Pop(frame_pkt, kPopTimeout)) {
      // 超时或通道关闭，回到循环顶部检查 stop_requested_
      continue;
    }

    const ArmorColor COLOR = enemy_color_.load();

    // ── 检测（2D） ────────────────────────────────────────────────────────
    auto detections = detector_->Detect(frame_pkt.frame, COLOR);

    // ── PnP 解算（2D → 3D） ──────────────────────────────────────────────
    for (auto& det : detections) {
      const bool SOLVED = solver_->Solve(det);
      if (!SOLVED) {
        MV_LOG_WARN("DetectNode", "PnP solve failed for detection (frame_id={}).",
                    frame_pkt.frame_id);
      }
    }

    DetectPacket out_pkt{
        .detections = std::move(detections),
        .frame = frame_pkt.frame,  // 浅拷贝（引用计数），可选传递调试帧
        .timestamp = frame_pkt.timestamp,
        .frame_id = frame_pkt.frame_id,
        .enemy_color = COLOR,
    };

    if (!output_ch_->Push(std::move(out_pkt))) {
      break;  // 通道关闭，退出
    }

    ++processed_count_;
  }

  MV_LOG_INFO("DetectNode", "Worker stopped. Processed {} packets.",
              processed_count_.load());
}

}  // namespace mv::pipeline
