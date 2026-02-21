#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <foxglove/foxglove.hpp>
#include <foxglove/server.hpp>

using namespace std::chrono_literals;

int main(int argc, const char* argv[]) {
  foxglove::WebSocketServerOptions options;
  auto server_result = foxglove::WebSocketServer::create(std::move(options));
  if (!server_result.has_value()) {
    std::cerr << foxglove::strerror(server_result.error()) << '\n';
    return 1;
  }

  auto server = std::move(server_result.value());
  auto channel = foxglove::RawChannel::create("/hello", "json").value();
  auto start = std::chrono::steady_clock::now();

  // Log until interrupted
  static std::function<void()> sigint_handler;
  std::atomic_bool done = false;
  sigint_handler = [&] { done = true; };
  std::signal(SIGINT, [](int) {
    if (sigint_handler) {
      sigint_handler();
    }
  });

  while (!done) {
    auto dur = std::chrono::steady_clock::now() - start;
    float elapsed_seconds = std::chrono::duration<float>(dur).count();
    std::string msg = "{\"elapsed\": " + std::to_string(elapsed_seconds) + "}";
    channel.log(reinterpret_cast<const std::byte*>(msg.data()), msg.size());

    std::this_thread::sleep_for(33ms);
  }

  return 0;
}
