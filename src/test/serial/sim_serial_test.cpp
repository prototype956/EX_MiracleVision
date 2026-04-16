/**
 * @file sim_serial_test.cpp
 * @brief SimSerial 行为测试：连接收发与断连软降级
 */

#include "hal/serial/sim_serial.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

namespace {

bool AssertOrFail(bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << "[FAIL] " << message << "\n";
  return false;
}

class TestTcpServer {
 public:
  explicit TestTcpServer(int port) : port_(port) {}

  ~TestTcpServer() { Stop(); }

  TestTcpServer(const TestTcpServer&) = delete;
  TestTcpServer& operator=(const TestTcpServer&) = delete;
  TestTcpServer(TestTcpServer&&) = delete;
  TestTcpServer& operator=(TestTcpServer&&) = delete;

  bool Start() {
    stop_.store(false);
    thread_ = std::thread([this] { Run(); });
    // 给监听线程一个最短启动窗口，避免连接方先发起导致误判。
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    return started_.load();
  }

  void Stop() {
    stop_.store(true);
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
    if (client_fd_ >= 0) {
      close(client_fd_);
      client_fd_ = -1;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool SendToClient(const std::vector<uint8_t>& bytes) const {
    if (client_fd_ < 0) {
      return false;
    }
    const ssize_t WRITTEN = send(client_fd_, bytes.data(), bytes.size(), 0);
    return WRITTEN == static_cast<ssize_t>(bytes.size());
  }

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  bool WaitUntilReceivedSize(std::size_t expected, int timeout_ms) const {
    const auto DEADLINE = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < DEADLINE) {
      if (received_.size() >= expected) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return received_.size() >= expected;
  }

  std::vector<uint8_t> ReceivedBytes() const { return received_; }

 private:
  void Run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    if (listen(listen_fd_, 1) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }

    started_.store(true);

    sockaddr_in cli_addr{};
    socklen_t cli_len = sizeof(cli_addr);
    client_fd_ = accept(listen_fd_, reinterpret_cast<sockaddr*>(&cli_addr), &cli_len);
    if (client_fd_ < 0) {
      return;
    }

    std::array<uint8_t, 256> buf{};
    while (!stop_.load()) {
      const ssize_t NUM_BYTES = recv(client_fd_, buf.data(), buf.size(), MSG_DONTWAIT);
      if (NUM_BYTES > 0) {
        received_.insert(received_.end(), buf.begin(), buf.begin() + NUM_BYTES);
      } else if (NUM_BYTES == 0) {
        break;
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }

  int port_ = 0;
  mutable std::vector<uint8_t> received_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> started_{false};
  std::thread thread_;
  int listen_fd_ = -1;
  int client_fd_ = -1;
};

}  // namespace

int main() {
  try {
    static constexpr int K_PORT = 19191;

    TestTcpServer server(K_PORT);
    if (!AssertOrFail(server.Start(), "Test TCP server should start")) {
      return 1;
    }

    mv::hal::SimSerial serial;
    YAML::Node cfg;
    cfg["endpoint"] = std::string("127.0.0.1:") + std::to_string(K_PORT);
    cfg["connect_timeout_ms"] = 300;
    cfg["recv_timeout_ms"] = 100;
    cfg["reconnect_interval_ms"] = 50;

    if (!AssertOrFail(serial.Open(cfg), "SimSerial::Open should succeed with local server")) {
      return 1;
    }

    const std::vector<uint8_t> OUTBOUND{0xAA, 0x0F, 0x01, 0x02};
    if (!AssertOrFail(serial.Send(OUTBOUND.data(), OUTBOUND.size()),
                      "SimSerial::Send should report success when connected")) {
      return 1;
    }

    if (!AssertOrFail(server.WaitUntilReceivedSize(OUTBOUND.size(), 300),
                      "Server should receive bytes sent by SimSerial")) {
      return 1;
    }

    const std::vector<uint8_t> INBOUND{0x11, 0x22, 0x33};
    if (!AssertOrFail(server.SendToClient(INBOUND), "Server should send bytes to client")) {
      return 1;
    }

    std::array<uint8_t, 8> recv_buf{};
    std::size_t received = 0;
    if (!AssertOrFail(serial.Recv(recv_buf.data(), INBOUND.size(), received),
                      "SimSerial::Recv should read inbound bytes")) {
      return 1;
    }
    if (!AssertOrFail(received == INBOUND.size(), "Received size should equal inbound size")) {
      return 1;
    }
    if (!AssertOrFail(std::memcmp(recv_buf.data(), INBOUND.data(), INBOUND.size()) == 0,
                      "Received payload should match inbound payload")) {
      return 1;
    }

    // 断连边界：服务端停止后，Send 仍应软降级返回 true，避免触发 SerialNode 累计失败。
    server.Stop();
    const std::vector<uint8_t> DEGRADED{0x55, 0x66};
    if (!AssertOrFail(serial.Send(DEGRADED.data(), DEGRADED.size()),
                      "SimSerial::Send should soft-degrade to success when disconnected")) {
      return 1;
    }

    std::cout << "[PASS] sim_serial_test\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[FAIL] exception: " << exception.what() << "\n";
    return 1;
  }
}
