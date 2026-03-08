/**
 * @file detection_publisher.cpp
 * @brief DetectionPublisher 实现
 *
 * 2D（ImageAnnotations）：
 *   - 每块装甲板 → PointsAnnotation（LINE_LOOP），颜色区分 RED/BLUE
 *   - 标签文字   → TextAnnotation，显示 "R-1 120cm" 格式
 *
 * 3D（SceneUpdate）：
 *   - 仅 is_solved=true 的装甲板参与
 *   - CubePrimitive：按物理尺寸（SMALL/BIG）放置在云台坐标系
 *   - TextPrimitive：显示标签，位于方块正上方 0.05m 处
 */
#include "tool/foxglove/detail/detection_publisher.hpp"

#include <string>

#include <spdlog/spdlog.h>

#include "tool/foxglove/detail/utils.hpp"

namespace mv::tool::detail {

DetectionPublisher::DetectionPublisher(foxglove::Context ctx) : ctx_(std::move(ctx)) {}

void DetectionPublisher::EnsureChannels() {
  if (!annot_ch_.has_value()) {
    auto res = foxglove::schemas::ImageAnnotationsChannel::create("detections/annotations", ctx_);
    if (res.has_value()) {
      annot_ch_.emplace(std::move(res.value()));
    } else {
      spdlog::error("[DetectionPublisher] Failed to create annotations channel: {}",
                    foxglove::strerror(res.error()));
    }
  }
  if (!scene_ch_.has_value()) {
    auto res = foxglove::schemas::SceneUpdateChannel::create("detections/3d", ctx_);
    if (res.has_value()) {
      scene_ch_.emplace(std::move(res.value()));
    } else {
      spdlog::error("[DetectionPublisher] Failed to create 3d scene channel: {}",
                    foxglove::strerror(res.error()));
    }
  }
}

void DetectionPublisher::Publish(const std::vector<mv::Detection>& dets, uint64_t ts_ns) {
  std::lock_guard<std::mutex> lock(mtx_);
  EnsureChannels();

  // ── 2D ImageAnnotations ──────────────────────────────────────────────────

  if (annot_ch_.has_value()) {
    foxglove::schemas::ImageAnnotations annot_msg;
    foxglove::schemas::Timestamp ts = ToTs(ts_ns);

    for (const auto& det : dets) {
      foxglove::schemas::Color box_color = ArmorColorToFox(det.color);

      // 边框：LINE_LOOP（4 个角点）
      foxglove::schemas::PointsAnnotation box;
      box.timestamp = ts;
      box.type = foxglove::schemas::PointsAnnotation::PointsAnnotationType::LINE_LOOP;
      box.thickness = 2.0;
      box.outline_color = box_color;
      for (const auto& pt : det.points) {
        box.points.push_back({static_cast<double>(pt.x), static_cast<double>(pt.y)});
      }
      annot_msg.points.push_back(std::move(box));

      // 中心点（POINTS）
      foxglove::schemas::PointsAnnotation center_dot;
      center_dot.timestamp = ts;
      center_dot.type = foxglove::schemas::PointsAnnotation::PointsAnnotationType::POINTS;
      center_dot.thickness = 5.0;
      center_dot.outline_color = ColorYellow();
      cv::Point2f ctr = det.Center();
      center_dot.points.push_back({static_cast<double>(ctr.x), static_cast<double>(ctr.y)});
      annot_msg.points.push_back(std::move(center_dot));

      // 标签文字
      foxglove::schemas::TextAnnotation label;
      label.timestamp = ts;
      label.text = ArmorLabel(det);
      label.font_size = 14.0;
      label.text_color = ColorWhite();
      label.background_color = {box_color.r, box_color.g, box_color.b, 0.5};
      // 位置：左上角上方 15px
      label.position = foxglove::schemas::Point2{
          static_cast<double>(det.points[3].x),   // 左上角 x
          static_cast<double>(det.points[3].y) - 18.0};  // 上方 18px
      annot_msg.texts.push_back(std::move(label));
    }

    annot_ch_->log(annot_msg, ts_ns);
  }

  // ── 3D SceneUpdate ───────────────────────────────────────────────────────

  if (scene_ch_.has_value()) {
    foxglove::schemas::SceneUpdate scene_msg;

    for (size_t i = 0; i < dets.size(); ++i) {
      const auto& det = dets[i];
      if (!det.is_solved) {
        continue;
      }

      foxglove::schemas::SceneEntity entity;
      entity.frame_id = "gimbal";
      entity.id = "armor_" + std::to_string(i);
      entity.timestamp = ToTs(ts_ns);
      entity.lifetime = foxglove::schemas::Duration{0, 300'000'000};  // 300ms 自动消失
      entity.frame_locked = false;

      const double x = det.xyz_in_gimbal.x();
      const double y = det.xyz_in_gimbal.y();
      const double z = det.xyz_in_gimbal.z();

      // 装甲板立方体（物理尺寸）
      foxglove::schemas::CubePrimitive cube;
      cube.pose = MakePose(x, y, z, 0.0, 0.0, 0.0, 1.0);
      double half_w = (det.type == mv::ArmorType::SMALL) ? 0.135 : 0.230;
      cube.size = foxglove::schemas::Vector3{half_w, 0.055, 0.01};
      // 设置颜色时先构造 Color 值再赋给 optional 字段
      auto cube_color = ArmorColorToFox(det.color);
      cube_color.a = 0.55;
      cube.color = cube_color;
      entity.cubes.push_back(std::move(cube));

      // 3D 标签文字（TextPrimitive 只有单 color 字段）
      foxglove::schemas::TextPrimitive text;
      text.pose = MakePose(x, y + 0.07, z, 0.0, 0.0, 0.0, 1.0);
      text.text = ArmorLabel(det);
      text.font_size = 0.04;
      text.billboard = true;   // 始终面朝摄像机
      text.color = ColorWhite();
      entity.texts.push_back(std::move(text));

      scene_msg.entities.push_back(std::move(entity));
    }

    scene_ch_->log(scene_msg, ts_ns);
  }
}

}  // namespace mv::tool::detail
