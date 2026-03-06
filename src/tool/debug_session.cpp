/**
 * @file debug_session.cpp
 * @brief DebugSession 实现
 */
#include "debug_session.hpp"

#include "tool/metrics_tracker.hpp"

#include <unordered_map>

#include <opencv2/highgui.hpp>

namespace mv::tool {

// ── Impl ──────────────────────────────────────────────────────────────────

struct DebugSession::Impl {
  Config          cfg_;
  ParamTuner      tuner_;
  ViewRenderer    renderer_;
  MetricsTracker  metrics_;

  std::unordered_map<int, std::function<void()>> key_map_;

  bool quit_{false};
  bool paused_{false};

  ~Impl() { cv::destroyAllWindows(); }

  // ── 注册内置按键 ────────────────────────────────────────────────────────
  void RegisterBuiltinKeys() {
    // 退出
    key_map_['q']  = [this] { quit_   = true;  };
    key_map_[27]   = [this] { quit_   = true;  };  // ESC
    // 暂停 / 继续
    key_map_[' ']  = [this] { paused_ = !paused_; };
    // 视图切换
    key_map_['1']  = [this] { renderer_.SetView(ViewMode::RESULT); };
    key_map_['2']  = [this] { renderer_.SetView(ViewMode::DIFF);   };
    key_map_['3']  = [this] { renderer_.SetView(ViewMode::BINARY); };
    key_map_['4']  = [this] { renderer_.SetView(ViewMode::LIGHTS); };
  }
};

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

DebugSession::DebugSession()  : impl_(std::make_unique<Impl>()) {}
DebugSession::~DebugSession() = default;

DebugSession::DebugSession(DebugSession&&) noexcept            = default;
DebugSession& DebugSession::operator=(DebugSession&&) noexcept = default;

// ── 公开接口实现 ────────────────────────────────────────────────────────────

void DebugSession::Init(const Config& cfg) {
  impl_->cfg_     = cfg;
  impl_->metrics_ = MetricsTracker(cfg.fps_window);

  // 创建窗口
  impl_->renderer_.Init(cfg.main_window, cfg.debug_window);

  // Trackbar 附加到 debug 窗口
  impl_->tuner_.AttachToWindow(cfg.debug_window);

  // 注册内置按键
  impl_->RegisterBuiltinKeys();
}

void DebugSession::AddParam(ParamDesc desc) {
  impl_->tuner_.AddParam(std::move(desc));
}

void DebugSession::ApplyParams() {
  impl_->tuner_.ApplyAll();
}

void DebugSession::SaveParams() {
  if (impl_->cfg_.save_yaml.empty()) { return; }
  impl_->tuner_.SaveTo(impl_->cfg_.save_yaml);
}

void DebugSession::BindKey(int key, std::function<void()> action) {
  impl_->key_map_[key] = std::move(action);
}

DebugSession::PollResult DebugSession::Poll() {
  const int key = cv::waitKey(impl_->paused_ ? 50 : 1) & 0xFF;
  if (key != 255) {  // 255 = no key
    auto it = impl_->key_map_.find(key);
    if (it != impl_->key_map_.end() && it->second) {
      it->second();
    }
  }
  return {impl_->quit_, impl_->paused_};
}

void DebugSession::Feed(
    const cv::Mat&                                  raw,
    const mv::modules::BasicArmorDetector::DebugData& dbg,
    const std::vector<mv::Detection>&               detections,
    const mv::GimbalControl&                        ctrl,
    const mv::modules::BasicArmorDetector::Params& params) {

  impl_->renderer_.Render(raw, dbg, detections, ctrl,
                          impl_->metrics_.TotalFrames(),
                          impl_->metrics_.CurrentFps(),
                          params);
}

void DebugSession::TickFrame(bool has_detection, int det_count) {
  impl_->metrics_.Tick(has_detection, det_count);
}

void DebugSession::PrintStats() const {
  impl_->metrics_.PrintStats();
}

void DebugSession::SetView(ViewMode m) {
  impl_->renderer_.SetView(m);
}

ViewMode DebugSession::GetView() const {
  return impl_->renderer_.GetView();
}

}  // namespace mv::tool
