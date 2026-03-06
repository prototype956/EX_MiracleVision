/**
 * @file view_renderer.cpp
 * @brief ViewRenderer 实现
 */
#include "tool/debug/view_renderer.hpp"

#include <array>
#include <iomanip>
#include <sstream>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

namespace mv::tool {

// ── 内部绘制辅助（匿名 namespace，仅本文件可见）─────────────────────────────

namespace {

const char* ViewModeName(ViewMode m) noexcept {
  switch (m) {
    case ViewMode::DIFF:   return "diff";
    case ViewMode::BINARY: return "binary";
    case ViewMode::LIGHTS: return "lights";
    default:               return "result";
  }
}

/** @brief 在帧上绘制单个 Detection（四角点多边形、中心点、3D 标签）*/
void DrawDetection(cv::Mat& frame, const mv::Detection& det) {
  // 四角点连线（绿色）
  const auto& pts = det.points;
  for (int i = 0; i < 4; ++i) {
    cv::line(frame, pts[i], pts[(i + 1) % 4], cv::Scalar(0, 255, 0), 2);
  }
  // 中心点（红色）
  const cv::Point2f c = det.Center();
  cv::circle(frame, c, 5, cv::Scalar(0, 0, 255), -1);

  // 标签
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << "conf:" << det.confidence;
  if (det.is_solved) {
    oss << " y:" << std::setprecision(1) << det.yaw_angle * 180.0 / M_PI << "d"
        << " p:" << det.pitch_angle * 180.0 / M_PI << "d";
  }
  cv::putText(frame, oss.str(),
              cv::Point(static_cast<int>(c.x) - 55, static_cast<int>(c.y) - 10),
              cv::FONT_HERSHEY_SIMPLEX, 0.42, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
}

/** @brief 在帧上叠加 HUD（左上角：帧号/FPS/视图；右上角：参数摘要）*/
void DrawHUD(cv::Mat& frame,
             int                                             frame_idx,
             double                                          fps,
             const mv::GimbalControl&                        ctrl,
             ViewMode                                        view,
             const mv::modules::BasicArmorDetector::Params& p) {
  // 第 1 行：帧号 / FPS / 视图提示
  {
    std::ostringstream oss;
    oss << "Frame:" << frame_idx
        << "  FPS:" << std::fixed << std::setprecision(1) << fps
        << "  View:[" << ViewModeName(view) << "] 1/2/3/4  [s]save";
    cv::putText(frame, oss.str(), cv::Point(10, 24),
                cv::FONT_HERSHEY_SIMPLEX, 0.50, cv::Scalar(255, 255, 0), 1, cv::LINE_AA);
  }
  // 第 2 行：跟踪状态
  if (ctrl.tracking) {
    std::ostringstream oss;
    oss << "TRACKING  yaw:" << std::fixed << std::setprecision(1)
        << ctrl.yaw   * 180.0 / M_PI << "d"
        << "  pitch:" << ctrl.pitch * 180.0 / M_PI << "d";
    cv::putText(frame, oss.str(), cv::Point(10, 44),
                cv::FONT_HERSHEY_SIMPLEX, 0.46, cv::Scalar(0, 255, 128), 1, cv::LINE_AA);
  } else {
    cv::putText(frame, "LOST", cv::Point(10, 44),
                cv::FONT_HERSHEY_SIMPLEX, 0.46, cv::Scalar(80, 80, 255), 1, cv::LINE_AA);
  }
  // 右上角：参数摘要
  {
    std::ostringstream oss;
    oss << "thr:" << p.light_thresh
        << " ang:" << std::fixed << std::setprecision(0) << p.max_light_angle
        << " ar:[" << std::setprecision(1) << p.min_armor_ratio
        << "," << p.max_armor_ratio << "]"
        << " dff:" << std::setprecision(0) << p.max_angle_diff;
    const std::string txt = oss.str();
    const int x = std::max(0, frame.cols - static_cast<int>(txt.size()) * 7 - 4);
    cv::putText(frame, txt, cv::Point(x, 18),
                cv::FONT_HERSHEY_SIMPLEX, 0.40, cv::Scalar(180, 180, 255), 1, cv::LINE_AA);
  }
}

/** @brief 在 debug 窗口的 binary 图上叠加参数单行文字 */
void DrawDebugOverlay(cv::Mat& bin_bgr,
                      const mv::modules::BasicArmorDetector::Params& p) {
  std::ostringstream oss;
  oss << "thr:" << p.light_thresh
      << " ang:" << std::fixed << std::setprecision(0) << p.max_light_angle
      << " ar:[" << std::setprecision(1) << p.min_armor_ratio
      << "," << p.max_armor_ratio << "]"
      << " dff:" << std::setprecision(0) << p.max_angle_diff;
  cv::putText(bin_bgr, oss.str(), cv::Point(4, 20),
              cv::FONT_HERSHEY_SIMPLEX, 0.46, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
}

}  // anonymous namespace

// ── Impl ──────────────────────────────────────────────────────────────────

struct ViewRenderer::Impl {
  std::string main_win_;
  std::string debug_win_;
  ViewMode    view_{ViewMode::RESULT};
};

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

ViewRenderer::ViewRenderer()  : impl_(std::make_unique<Impl>()) {}
ViewRenderer::~ViewRenderer() = default;

ViewRenderer::ViewRenderer(ViewRenderer&&) noexcept            = default;
ViewRenderer& ViewRenderer::operator=(ViewRenderer&&) noexcept = default;

// ── 公开接口实现 ────────────────────────────────────────────────────────────

void ViewRenderer::Init(const std::string& main_win, const std::string& debug_win) {
  impl_->main_win_  = main_win;
  impl_->debug_win_ = debug_win;
  cv::namedWindow(main_win,  cv::WINDOW_NORMAL);
  cv::namedWindow(debug_win, cv::WINDOW_NORMAL);
  cv::resizeWindow(main_win,  960, 540);   // 初始大小可拖拽调整
  cv::resizeWindow(debug_win, 640, 480);
}

void     ViewRenderer::SetView(ViewMode m) noexcept { impl_->view_ = m; }
ViewMode ViewRenderer::GetView()    const noexcept  { return impl_->view_; }

void ViewRenderer::Render(
    const cv::Mat&                                  raw,
    const mv::modules::BasicArmorDetector::DebugData& dbg,
    const std::vector<mv::Detection>&               detections,
    const mv::GimbalControl&                        ctrl,
    int                                             frame_idx,
    double                                          fps,
    const mv::modules::BasicArmorDetector::Params& params) {

  // ── 选择主视图内容 ────────────────────────────────────────────────────
  cv::Mat display;
  switch (impl_->view_) {
    case ViewMode::DIFF:
      if (!dbg.diff.empty()) {
        cv::cvtColor(dbg.diff, display, cv::COLOR_GRAY2BGR);
      } else {
        display = raw.clone();
      }
      break;
    case ViewMode::BINARY:
      if (!dbg.binary.empty()) {
        cv::cvtColor(dbg.binary, display, cv::COLOR_GRAY2BGR);
      } else {
        display = raw.clone();
      }
      break;
    case ViewMode::LIGHTS:
      display = dbg.lights_vis.empty() ? raw.clone() : dbg.lights_vis.clone();
      break;
    default:  // ViewMode::RESULT
      display = raw.clone();
      for (const auto& det : detections) {
        DrawDetection(display, det);
      }
      break;
  }

  // ── HUD 叠加 ──────────────────────────────────────────────────────────
  DrawHUD(display, frame_idx, fps, ctrl, impl_->view_, params);
  cv::imshow(impl_->main_win_, display);

  // ── Debug 窗口：始终显示 binary 图 ────────────────────────────────────
  if (!dbg.binary.empty()) {
    cv::Mat bin_bgr;
    cv::cvtColor(dbg.binary, bin_bgr, cv::COLOR_GRAY2BGR);
    DrawDebugOverlay(bin_bgr, params);
    cv::imshow(impl_->debug_win_, bin_bgr);
  }
}

}  // namespace mv::tool
