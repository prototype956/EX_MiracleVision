/**
 * @file debug_session.cpp
 * @brief DebugSession 实现
 */
#include "tool/debug/debug_session.hpp"

#include "tool/debug/metrics_tracker.hpp"

#include <unordered_map>

#include <opencv2/highgui.hpp>

namespace mv::tool {

// ── Impl ──────────────────────────────────────────────────────────────────

struct DebugSession::Impl {
  // struct 成员全部 public，命名 lower_case 无后缀
  Config cfg;
  ParamTuner tuner;
  ViewRenderer renderer;
  MetricsTracker metrics;

  std::unordered_map<int, std::function<void()>> key_map;

  bool quit{false};
  bool paused{false};

  // 自定义析构 → 显式声明其余特殊函数（Rule of Five）
  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~Impl() { cv::destroyAllWindows(); }

  // ── 注册内置按键 ────────────────────────────────────────────────────────
  void RegisterBuiltinKeys() {
    key_map['q'] = [this] { quit = true; };
    key_map[27] = [this] { quit = true; };  // ESC
    key_map[' '] = [this] { paused = !paused; };
    key_map['1'] = [this] { renderer.SetView(ViewMode::RESULT); };
    key_map['2'] = [this] { renderer.SetView(ViewMode::DIFF); };
    key_map['3'] = [this] { renderer.SetView(ViewMode::BINARY); };
    key_map['4'] = [this] { renderer.SetView(ViewMode::LIGHTS); };
    key_map['5'] = [this] { renderer.SetView(ViewMode::ROI); };
  }
};

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

DebugSession::DebugSession() : impl_(std::make_unique<Impl>()) {}
DebugSession::~DebugSession() = default;

DebugSession::DebugSession(DebugSession&&) noexcept = default;
DebugSession& DebugSession::operator=(DebugSession&&) noexcept = default;

// ── 公开接口实现 ────────────────────────────────────────────────────────────

void DebugSession::Init(const Config& cfg) {
  impl_->cfg = cfg;
  impl_->metrics = MetricsTracker(cfg.fps_window);

  impl_->renderer.Init(cfg.main_window, cfg.debug_window);
  impl_->tuner.AttachToWindow(cfg.debug_window);
  impl_->RegisterBuiltinKeys();
}

void DebugSession::AddParam(ParamDesc desc) {
  impl_->tuner.AddParam(std::move(desc));
}

void DebugSession::ApplyParams() {
  impl_->tuner.ApplyAll();
}

void DebugSession::SaveParams() {
  if (impl_->cfg.save_yaml.empty()) {
    return;
  }
  impl_->tuner.SaveTo(impl_->cfg.save_yaml);
}

void DebugSession::BindKey(int key, std::function<void()> action) {
  impl_->key_map[key] = std::move(action);
}

DebugSession::PollResult DebugSession::Poll() {
  const int KEY = cv::waitKey(impl_->paused ? 50 : 1) & 0xFF;
  if (KEY != 255) {  // 255 = no key pressed
    const auto FOUND = impl_->key_map.find(KEY);
    if (FOUND != impl_->key_map.end() && FOUND->second) {
      FOUND->second();
    }
  }
  return {impl_->quit, impl_->paused};
}

void DebugSession::Feed(const cv::Mat& raw, const mv::modules::BasicArmorDetector::DebugData& dbg,
                        const std::vector<mv::Detection>& detections, const mv::GimbalControl& ctrl,
                        const mv::modules::BasicArmorDetector::Params& params,
                        const std::string& status, const cv::Rect2i& roi_rect) {
  impl_->renderer.Render(raw, dbg, detections, ctrl, impl_->metrics.TotalFrames(),
                         impl_->metrics.CurrentFps(), params, status, roi_rect);
}

void DebugSession::TickFrame(bool has_detection, int det_count) {
  impl_->metrics.Tick(has_detection, det_count);
}

void DebugSession::PrintStats() const {
  impl_->metrics.PrintStats();
}

void DebugSession::SetView(ViewMode mode) {
  impl_->renderer.SetView(mode);
}

[[nodiscard]] ViewMode DebugSession::GetView() const {
  return impl_->renderer.GetView();
}

}  // namespace mv::tool
