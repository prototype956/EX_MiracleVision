/**
 * @file pnp_visualizer.cpp
 * @brief PnpVisualizer 实现
 *
 * 三层输出：
 *  1. pnp/debug_image  — 在底图上绘制 4 个绿色角点 + 中心点，并标注装甲板 ID
 *  2. pnp/axes_3d      — 每个已解算装甲板的 RGB XYZ 坐标轴箭头（在 gimbal 坐标系）
 *  3. pnp/residuals    — JSON: 每块装甲板的 xyz / yaw_deg / pitch_deg / dist_m
 */
#include "tool/foxglove/detail/pnp_visualizer.hpp"

#include "tool/foxglove/detail/utils.hpp"

#include <cstring>
#include <sstream>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

namespace mv::tool::detail {

PnpVisualizer::PnpVisualizer(foxglove::Context ctx) : ctx_(std::move(ctx)) {}

void PnpVisualizer::EnsureChannels() {
  if (!debug_img_ch_.has_value()) {
    auto res = foxglove::schemas::RawImageChannel::create("pnp/debug_image", ctx_);
    if (res.has_value()) {
      debug_img_ch_.emplace(std::move(res.value()));
    } else {
      spdlog::error("[PnpVisualizer] Failed to create pnp/debug_image: {}",
                    foxglove::strerror(res.error()));
    }
  }
  if (!axes_ch_.has_value()) {
    auto res = foxglove::schemas::SceneUpdateChannel::create("pnp/axes_3d", ctx_);
    if (res.has_value()) {
      axes_ch_.emplace(std::move(res.value()));
    } else {
      spdlog::error("[PnpVisualizer] Failed to create pnp/axes_3d: {}",
                    foxglove::strerror(res.error()));
    }
  }
  if (!residuals_ch_.has_value()) {
    auto res = foxglove::RawChannel::create("pnp/residuals", "json", std::nullopt, ctx_);
    if (res.has_value()) {
      residuals_ch_.emplace(std::move(res.value()));
    } else {
      spdlog::error("[PnpVisualizer] Failed to create pnp/residuals: {}",
                    foxglove::strerror(res.error()));
    }
  }
}

void PnpVisualizer::Publish(const std::vector<mv::Detection>& dets, const cv::Mat& frame,
                            uint64_t ts_ns) {
  std::lock_guard<std::mutex> lock(mtx_);
  EnsureChannels();

  // ── Layer 1: debug_image ─────────────────────────────────────────────────

  if (debug_img_ch_.has_value() && !frame.empty()) {
    cv::Mat annotated;
    if (frame.channels() == 1) {
      cv::cvtColor(frame, annotated, cv::COLOR_GRAY2BGR);
    } else {
      annotated = frame.clone();
    }

    for (size_t i = 0; i < dets.size(); ++i) {
      const auto& det = dets[i];

      // 4 个角点（绿色实心圆）
      for (const auto& pt : det.points) {
        cv::circle(annotated, cv::Point(static_cast<int>(pt.x), static_cast<int>(pt.y)), 5,
                   cv::Scalar(0, 255, 0), -1);
      }

      // 中心点（黄色）
      cv::Point2f ctr = det.Center();
      cv::circle(annotated, cv::Point(static_cast<int>(ctr.x), static_cast<int>(ctr.y)), 4,
                 cv::Scalar(0, 255, 255), -1);

      // 角点间连线（绿色虚线轮廓）
      for (int k = 0; k < 4; ++k) {
        cv::line(annotated,
                 cv::Point(static_cast<int>(det.points[k].x), static_cast<int>(det.points[k].y)),
                 cv::Point(static_cast<int>(det.points[(k + 1) % 4].x),
                           static_cast<int>(det.points[(k + 1) % 4].y)),
                 cv::Scalar(0, 200, 0), 1);
      }

      // 标签（距离或序号）
      std::string text =
          "#" + std::to_string(i) + " " +
          (det.is_solved ? std::to_string(static_cast<int>(det.xyz_in_gimbal.norm() * 100)) + "cm"
                         : "no pnp");
      cv::putText(annotated, text,
                  cv::Point(static_cast<int>(ctr.x) + 5, static_cast<int>(ctr.y) - 10),
                  cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(100, 255, 100), 1, cv::LINE_AA);
    }

    // 发布标注图
    foxglove::schemas::RawImage img_msg;
    img_msg.frame_id = "camera";
    img_msg.width = static_cast<uint32_t>(annotated.cols);
    img_msg.height = static_cast<uint32_t>(annotated.rows);
    img_msg.step = static_cast<uint32_t>(annotated.step);
    img_msg.encoding = "bgr8";
    img_msg.timestamp = ToTs(ts_ns);
    const size_t data_sz = annotated.total() * annotated.elemSize();
    img_msg.data.resize(data_sz);
    std::memcpy(img_msg.data.data(), annotated.data, data_sz);
    debug_img_ch_->log(img_msg, ts_ns);
  }

  // ── Layer 2: axes_3d ─────────────────────────────────────────────────────

  if (axes_ch_.has_value()) {
    foxglove::schemas::SceneUpdate scene;

    for (size_t i = 0; i < dets.size(); ++i) {
      const auto& det = dets[i];
      if (!det.is_solved) {
        continue;
      }

      Eigen::Vector3d origin = det.xyz_in_gimbal;
      foxglove::schemas::SceneEntity entity;
      entity.frame_id = "gimbal";
      entity.id = "pnp_axes_" + std::to_string(i);
      entity.timestamp = ToTs(ts_ns);
      entity.lifetime = foxglove::schemas::Duration{0, 400'000'000};  // 400ms
      entity.frame_locked = false;

      // X 轴（红色）
      entity.arrows.push_back(MakeArrow(origin, Eigen::Vector3d::UnitX(), ColorRed(), 0.1));
      // Y 轴（绿色）
      entity.arrows.push_back(MakeArrow(origin, Eigen::Vector3d::UnitY(), ColorGreen(), 0.1));
      // Z 轴（蓝色）
      entity.arrows.push_back(MakeArrow(origin, Eigen::Vector3d::UnitZ(), ColorBlue(), 0.1));

      scene.entities.push_back(std::move(entity));
    }

    axes_ch_->log(scene, ts_ns);
  }

  // ── Layer 3: residuals (JSON) ────────────────────────────────────────────

  if (residuals_ch_.has_value()) {
    nlohmann::json doc;
    doc["timestamp_ns"] = ts_ns;
    doc["count"] = static_cast<int>(dets.size());
    nlohmann::json armors = nlohmann::json::array();

    for (size_t i = 0; i < dets.size(); ++i) {
      const auto& det = dets[i];
      nlohmann::json a;
      a["index"] = static_cast<int>(i);
      a["solved"] = det.is_solved;
      a["label"] = ArmorLabel(det);
      if (det.is_solved) {
        double dist = det.xyz_in_gimbal.norm();
        a["x_m"] = det.xyz_in_gimbal.x();
        a["y_m"] = det.xyz_in_gimbal.y();
        a["z_m"] = det.xyz_in_gimbal.z();
        a["dist_m"] = dist;
        // rad → deg
        constexpr double RAD_TO_DEG = 180.0 / std::numbers::pi;
        a["yaw_deg"] = det.yaw_angle * RAD_TO_DEG;
        a["pitch_deg"] = det.pitch_angle * RAD_TO_DEG;
      }
      armors.push_back(std::move(a));
    }
    doc["armors"] = std::move(armors);

    std::string json_str = doc.dump();
    residuals_ch_->log(reinterpret_cast<const std::byte*>(json_str.data()), json_str.size(), ts_ns);
  }
}

}  // namespace mv::tool::detail
