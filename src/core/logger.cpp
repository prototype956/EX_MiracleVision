/**
 * @file logger.cpp
 * @brief Logger 初始化实现
 */
#include "logger.hpp"

#include <stdexcept>

#include <filesystem>
#include <fmt/chrono.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace mv {

void Logger::Init(const std::string& log_dir, spdlog::level::level_enum level, bool console_on) {
  std::lock_guard<std::mutex> lock(init_mutex_);
  if (initialized_) {
    return;
  }

  // 确保日志目录存在
  std::filesystem::create_directories(log_dir);

  // 按当前时间生成日志文件名，格式：logs/2026-02-26_15-30-00.log
  auto file_name =
      fmt::format("{}/{:%Y-%m-%d_%H-%M-%S}.log", log_dir, std::chrono::system_clock::now());

  // 构建 spdlog sink 列表
  std::vector<spdlog::sink_ptr> sinks;

  // 文件 sink：记录所有级别
  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_name, true);
  file_sink->set_level(spdlog::level::trace);
  sinks.emplace_back(file_sink);

  // 控制台彩色 sink（可选）
  if (console_on) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(level);
    sinks.emplace_back(console_sink);
  }

  // 创建 logger，格式：[时间] [级别] 消息
  logger_ = std::make_shared<spdlog::logger>("mv", sinks.begin(), sinks.end());
  logger_->set_level(spdlog::level::trace);  // logger 本身不过滤，由 sink 决定级别
  logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  logger_->flush_on(spdlog::level::warn);  // warn 及以上立即刷盘

  // 注册为 spdlog 全局 logger（方便第三方库查找）
  spdlog::register_logger(logger_);

  initialized_ = true;
}

void Logger::SetLevel(spdlog::level::level_enum level) {
  if (logger_) {
    logger_->set_level(level);
  }
}

}  // namespace mv
