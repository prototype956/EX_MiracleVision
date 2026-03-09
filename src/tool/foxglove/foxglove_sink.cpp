/**
 * @file foxglove_sink.cpp
 * @brief FoxgloveSink Pimpl 实现
 *
 * 【Pimpl 布局】
 *   Impl struct 集中管理：
 *   - foxglove::Context      ：SDK 全局上下文，所有子发布器共享同一实例
 *   - foxglove::WebSocketServer ：WebSocket 服务端，负责客户端连接 / 断开回调
 *   - 五个子发布器            ：ImagePublisher / DetectionPublisher /
 *                              PnpVisualizer / TfPublisher / ThreadMonitor
 *   - param_store            ：foxglove::Parameter 的 key→value 映射，
 *                              onGetParameters / onSetParameters 从此读写
 *
 * 【Channel 生命周期】
 *   每个子发布器内部懒创建自己的 Channel（EnsureChannels()），
 *   首次 Publish() 时才 advertise。例外：PnpVisualizer 在构造时立即注册，
 *   确保客户端握手后即可看到该话题（详见 pnp_visualizer.cpp）。
 *
 * 【零客户端门控】
 *   PublishImage / PublishPnpResult 在 HasClients()==false 时提前返回，
 *   避免昂贵的图像 clone + memcpy 拖累 Pipeline 帧率。
 *   PublishDetections / PublishTransform / PublishThreadMetrics 不门控，
 *   因为其序列化开销远低于图像编码。
 *
 * 【参数双向调节实现】
 *   onSetParameters 回调加锁更新 param_store 后，释放锁再通知 param_callback，
 *   防止外部回调（如重载检测器参数）在持锁状态下再次调用 UpdateParameters
 *   导致死锁。
 */
#include "tool/foxglove/foxglove_sink.hpp"

#include "tool/foxglove/detail/detection_publisher.hpp"
#include "tool/foxglove/detail/image_publisher.hpp"
#include "tool/foxglove/detail/pnp_visualizer.hpp"
#include "tool/foxglove/detail/tf_publisher.hpp"
#include "tool/foxglove/detail/thread_monitor.hpp"
#include "tool/foxglove/detail/utils.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <foxglove/channel.hpp>
#include <foxglove/foxglove.hpp>
#include <foxglove/schemas.hpp>
#include <foxglove/server.hpp>
#include <foxglove/server/parameter.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <spdlog/spdlog.h>

namespace mv::tool {

// ============================================================================
// Impl
// ============================================================================

struct FoxgloveSink::Impl {
  // ── Server 核心 ──────────────────────────────────────────────────────────
  foxglove::Context ctx;
  std::unique_ptr<foxglove::WebSocketServer> server;
  bool is_running{false};

  // ── 子发布器 ──────────────────────────────────────────────────────────────
  std::unique_ptr<detail::ImagePublisher> image_pub;
  std::unique_ptr<detail::DetectionPublisher> detection_pub;
  std::unique_ptr<detail::PnpVisualizer> pnp_viz;
  std::unique_ptr<detail::TfPublisher> tf_pub;
  std::unique_ptr<detail::ThreadMonitor> thread_monitor;

  // ── 云台控制（JSON channel）──────────────────────────────────────────────
  std::optional<foxglove::RawChannel> ctrl_ch;
  std::mutex ctrl_mtx;

  // ── 连接状态 ──────────────────────────────────────────────────────────────
  /// 当前活跃客户端数量（原子计数，供零客户端时跳过图像编码）
  std::atomic<int> client_count{0};

  /// 是否有至少一个客户端连接（无锁快速查询）
  [[nodiscard]] bool HasClients() const noexcept {
    return client_count.load(std::memory_order_relaxed) > 0;
  }

  // ── 参数双向调节 ─────────────────────────────────────────────────────────
  std::unordered_map<std::string, foxglove::Parameter> param_store;
  std::mutex param_mutex;
  ParameterCallback param_callback;

  // ── 构造 ─────────────────────────────────────────────────────────────────
  explicit Impl(const FoxgloveSink::Config& cfg) {
    foxglove::setLogLevel(foxglove::LogLevel::Info);
    ctx = foxglove::Context::create();

    // --- WebSocket Server 选项 ---
    foxglove::WebSocketServerOptions options;
    options.context = ctx;
    options.name = cfg.name;
    options.host = cfg.host;
    options.port = cfg.port;
    options.capabilities = foxglove::WebSocketServerCapabilities::ClientPublish |
                           foxglove::WebSocketServerCapabilities::Parameters;
    options.supported_encodings = {"json", "protobuf"};

    // 1. 获取参数（Client → Server）
    options.callbacks.onGetParameters =
        [this](uint32_t /*client_id*/, std::optional<std::string_view> /*req_id*/,
               const std::vector<std::string_view>& names) -> std::vector<foxglove::Parameter> {
      std::lock_guard<std::mutex> lock(param_mutex);
      std::vector<foxglove::Parameter> result;
      if (names.empty()) {
        for (const auto& [k, v] : param_store) {
          result.push_back(v.clone());
        }
      } else {
        for (const auto& sv : names) {
          std::string name(sv);
          if (auto it = param_store.find(name); it != param_store.end()) {
            result.push_back(it->second.clone());
          }
        }
      }
      return result;
    };

    // 2. 设置参数（Client → Server）
    options.callbacks.onSetParameters = [this](uint32_t /*client_id*/,
                                               std::optional<std::string_view> /*req_id*/,
                                               const std::vector<foxglove::ParameterView>& params)
        -> std::vector<foxglove::Parameter> {
      std::vector<foxglove::Parameter> result;
      std::vector<std::string> changed;

      {
        std::lock_guard<std::mutex> lock(param_mutex);
        for (const auto& p : params) {
          std::string name(p.name());
          auto new_p = p.clone();
          param_store.insert_or_assign(name, new_p.clone());
          result.emplace_back(std::move(new_p));
          changed.push_back(name);
        }
      }  // 必须先释放锁，再通知外部回调
         // 原因：param_callback 可能调用 UpdateParameters() 再次加 param_mutex，
         // 持锁时通知会导致递归死锁。

      // 锁外通知，防止死锁
      if (param_callback) {
        for (const auto& name : changed) {
          param_callback(name, nlohmann::json::object());
        }
      }
      return result;
    };

    // 3. 参数订阅/取消订阅（占位）
    options.callbacks.onParametersSubscribe = [](const std::vector<std::string_view>&) {};
    options.callbacks.onParametersUnsubscribe = [](const std::vector<std::string_view>&) {};

    // 4. 订阅/取消订阅 channel 日志
    options.callbacks.onSubscribe = [](uint64_t ch_id, const foxglove::ClientMetadata& c) {
      spdlog::debug("[FoxgloveSink] Client {} subscribed to channel {}", c.id, ch_id);
    };
    options.callbacks.onUnsubscribe = [](uint64_t ch_id, const foxglove::ClientMetadata& c) {
      spdlog::debug("[FoxgloveSink] Client {} unsubscribed from channel {}", c.id, ch_id);
    };

    // 5a. 客户端连接：计数 +1，推送已存储参数快照
    options.callbacks.onClientConnect = [this]() {
      const int n = client_count.fetch_add(1, std::memory_order_relaxed) + 1;
      spdlog::info("[FoxgloveSink] Client connected (total: {}). Publishing parameter snapshot...",
                   n);
      std::vector<foxglove::Parameter> snapshot;
      {
        std::lock_guard<std::mutex> lock(param_mutex);
        for (const auto& [k, v] : param_store) {
          snapshot.push_back(v.clone());
        }
      }
      if (!snapshot.empty() && server) {
        server->publishParameterValues(std::move(snapshot));
      }
    };

    // 5b. 客户端断开：计数 -1
    options.callbacks.onClientDisconnect = [this]() {
      const int n = client_count.fetch_sub(1, std::memory_order_relaxed) - 1;
      spdlog::info("[FoxgloveSink] Client disconnected (remaining: {})", n);
    };

    // 创建 Server
    auto server_res = foxglove::WebSocketServer::create(std::move(options));
    if (!server_res.has_value()) {
      spdlog::error("[FoxgloveSink] Failed to create WebSocket server: {}",
                    foxglove::strerror(server_res.error()));
      throw std::runtime_error("FoxgloveSink: WebSocket server creation failed");
    }
    server = std::make_unique<foxglove::WebSocketServer>(std::move(server_res.value()));

    // 初始化子发布器（共享同一 Context）
    image_pub = std::make_unique<detail::ImagePublisher>(ctx);
    detection_pub = std::make_unique<detail::DetectionPublisher>(ctx);
    pnp_viz = std::make_unique<detail::PnpVisualizer>(ctx);
    tf_pub = std::make_unique<detail::TfPublisher>(ctx);
    thread_monitor = std::make_unique<detail::ThreadMonitor>(ctx);
  }
};

// ============================================================================
// FoxgloveSink public interface
// ============================================================================

FoxgloveSink::FoxgloveSink() : FoxgloveSink(Config{}) {}
FoxgloveSink::FoxgloveSink(Config cfg) : impl_(std::make_unique<Impl>(cfg)) {}
FoxgloveSink::~FoxgloveSink() = default;
FoxgloveSink::FoxgloveSink(FoxgloveSink&&) noexcept = default;
FoxgloveSink& FoxgloveSink::operator=(FoxgloveSink&&) noexcept = default;

void FoxgloveSink::Start() {
  // Server 在 Impl 构造时已通过 WebSocketServer::create() 建立并绑定端口，
  // Start() 只负责将 is_running 标志置位，供 Stop() 判断是否需要 server->stop()。
  // 如需明确启动监听，可在此调用 server->start()（取决于 foxglove SDK 版本）。
  if (impl_->server && !impl_->is_running) {
    impl_->is_running = true;
    spdlog::info("[FoxgloveSink] WebSocket server started on {}:{}", "port",
                 impl_->server != nullptr ? "8765" : "?");
  }
}

void FoxgloveSink::Stop() {
  if (impl_->is_running && impl_->server) {
    impl_->server->stop();
    impl_->is_running = false;
    spdlog::info("[FoxgloveSink] WebSocket server stopped");
  }
}

bool FoxgloveSink::HasClients() const noexcept {
  return impl_->HasClients();
}

// ── 图像 ─────────────────────────────────────────────────────────────────────

void FoxgloveSink::PublishImage(const cv::Mat& img, const std::string& topic,
                                const std::string& frame_id, int64_t ts_ns) {
  // 零客户端时提前返回：图像 clone + memcpy(~1.5MB/帧) 会占用约 2ms，
  // 在 120fps Pipeline 中累积为不可忽略的延迟，无接收方时完全没有意义。
  if (!impl_->HasClients())
    return;
  impl_->image_pub->Publish(img, topic, frame_id, detail::ResolveTs(ts_ns));
}

// ── 检测结果 ──────────────────────────────────────────────────────────────────

void FoxgloveSink::PublishDetections(const std::vector<mv::Detection>& dets, int64_t ts_ns) {
  impl_->detection_pub->Publish(dets, detail::ResolveTs(ts_ns));
}

// ── PnP 专项调试 ──────────────────────────────────────────────────────────────

void FoxgloveSink::PublishPnpResult(const std::vector<mv::Detection>& dets, const cv::Mat& frame,
                                    int64_t ts_ns) {
  // 零客户端时跳过调试图像绘制和编码
  if (!impl_->HasClients())
    return;
  impl_->pnp_viz->Publish(dets, frame, detail::ResolveTs(ts_ns));
}

// ── TF 坐标系 ─────────────────────────────────────────────────────────────────

void FoxgloveSink::PublishTransform(const std::string& parent, const std::string& child,
                                    const Eigen::Matrix4d& T, int64_t ts_ns) {
  impl_->tf_pub->Publish(parent, child, T, detail::ResolveTs(ts_ns));
}

// ── 线程健康 ──────────────────────────────────────────────────────────────────

void FoxgloveSink::PublishThreadMetrics(const std::vector<ThreadMetrics>& metrics, int64_t ts_ns) {
  impl_->thread_monitor->Publish(metrics, detail::ResolveTs(ts_ns));
}

// ── 云台控制指令 ──────────────────────────────────────────────────────────────

void FoxgloveSink::PublishGimbalControl(const mv::GimbalControl& ctrl, int64_t ts_ns) {
  uint64_t ts = detail::ResolveTs(ts_ns);

  std::lock_guard<std::mutex> lock(impl_->ctrl_mtx);

  // 懒创建 channel（带 JSON Schema，供 Foxglove 正确识别数据类型）
  if (!impl_->ctrl_ch.has_value()) {
    static const std::string SCHEMA_STR =
        R"({"type":"object","properties":{"timestamp_ns":{"type":"integer"},"yaw_rad":{"type":"number"},"pitch_rad":{"type":"number"},"distance_m":{"type":"number"},"fire":{"type":"boolean"},"tracking":{"type":"boolean"}}})";
    foxglove::Schema schema;
    schema.name = "GimbalControl";
    schema.encoding = "jsonschema";
    schema.data = reinterpret_cast<const std::byte*>(SCHEMA_STR.data());
    schema.data_len = SCHEMA_STR.size();
    auto res = foxglove::RawChannel::create("control/gimbal", "json", schema, impl_->ctx);
    if (res.has_value()) {
      impl_->ctrl_ch.emplace(std::move(res.value()));
    } else {
      spdlog::error("[FoxgloveSink] Failed to create control/gimbal channel: {}",
                    foxglove::strerror(res.error()));
      return;
    }
  }

  nlohmann::json msg;
  msg["timestamp_ns"] = ts;
  msg["yaw_rad"] = ctrl.yaw;
  msg["pitch_rad"] = ctrl.pitch;
  msg["distance_m"] = ctrl.distance;
  msg["fire"] = ctrl.fire;
  msg["tracking"] = ctrl.tracking;

  std::string json_str = msg.dump();
  impl_->ctrl_ch->log(reinterpret_cast<const std::byte*>(json_str.data()), json_str.size(), ts);
}

// ── 参数双向调节 ──────────────────────────────────────────────────────────────

void FoxgloveSink::SetParameterCallback(ParameterCallback cb) {
  impl_->param_callback = std::move(cb);
}

void FoxgloveSink::UpdateParameters(const nlohmann::json& params) {
  if (!impl_->server) {
    return;
  }

  std::lock_guard<std::mutex> lock(impl_->param_mutex);
  std::vector<foxglove::Parameter> to_publish;

  for (const auto& [key, val] : params.items()) {
    foxglove::Parameter param(key);

    if (val.is_boolean()) {
      param = foxglove::Parameter(key, val.get<bool>());
    } else if (val.is_number_integer()) {
      param = foxglove::Parameter(key, val.get<int64_t>());
    } else if (val.is_number_float()) {
      param = foxglove::Parameter(key, val.get<double>());
    } else if (val.is_string()) {
      param = foxglove::Parameter(key, val.get<std::string>());
    } else if (val.is_array() && !val.empty()) {
      if (val[0].is_number()) {
        param = foxglove::Parameter(key, val.get<std::vector<double>>());
      } else {
        param = foxglove::Parameter(key, val.dump());  // JSON 字符串
      }
    } else if (val.is_object()) {
      param = foxglove::Parameter(key, val.dump());
    }

    impl_->param_store.insert_or_assign(key, std::move(param));
    to_publish.push_back(impl_->param_store.at(key).clone());
  }

  if (!to_publish.empty()) {
    impl_->server->publishParameterValues(std::move(to_publish));
  }
}

nlohmann::json FoxgloveSink::GetParameter(const std::string& name) const {
  std::lock_guard<std::mutex> lock(impl_->param_mutex);
  auto it = impl_->param_store.find(name);
  if (it == impl_->param_store.end()) {
    return nullptr;
  }
  const auto& p = it->second;
  if (p.is<bool>())
    return p.get<bool>();
  if (p.is<int64_t>())
    return p.get<int64_t>();
  if (p.is<double>())
    return p.get<double>();
  if (p.is<std::string>())
    return p.get<std::string>();
  if (p.is<std::vector<double>>())
    return p.get<std::vector<double>>();
  if (p.is<std::vector<int64_t>>())
    return p.get<std::vector<int64_t>>();
  return nullptr;
}

}  // namespace mv::tool
