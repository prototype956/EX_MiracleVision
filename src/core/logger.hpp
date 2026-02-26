/**
 * @file logger.hpp
 * @brief 统一日志系统 —— 基于 spdlog 的单例封装
 *
 * 设计原则：
 *  - 全局唯一 Logger 实例，通过 mv::Logger::Instance() 获取
 *  - 支持控制台彩色输出 + 文件输出（自动按日期命名）
 *  - 支持带模块前缀的格式化日志，方便定位来源
 *  - 项目统一使用此接口，禁止直接调用 spdlog 全局函数
 *
 * 使用示例：
 * @code
 *   // 初始化（在 main 最开始调用一次）
 *   mv::Logger::Instance().Init("logs");
 *
 *   // 使用宏（推荐，自动注入文件名/行号）
 *   MV_LOG_INFO("armor",  "Detected {} armors", count);
 *   MV_LOG_WARN("serial", "Packet checksum failed");
 *   MV_LOG_ERROR("cam",   "Camera open failed: {}", err_msg);
 *
 *   // 直接使用 logger 对象
 *   mv::Logger::Instance().Info("armor", "Detected {} armors", count);
 * @endcode
 */
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

namespace mv {

class Logger {
 public:
  // ── 单例访问 ─────────────────────────────────────────────────────────────
  static Logger& Instance() {
    static Logger inst;
    return inst;
  }

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;
  ~Logger() = default;

  // ── 初始化 ────────────────────────────────────────────────────────────────

  /**
   * @brief 初始化日志系统（应在 main() 最开始调用一次）
   * @param log_dir      日志文件输出目录，不存在时自动创建
   * @param level        初始日志级别（默认 debug）
   * @param console_on   是否开启控制台彩色输出（默认开启）
   */
  void Init(const std::string& log_dir = "logs",
            spdlog::level::level_enum level = spdlog::level::debug, bool console_on = true);

  /**
   * @brief 设置全局日志级别（初始化后也可调用）
   */
  void SetLevel(spdlog::level::level_enum level);

  // ── 日志接口 ──────────────────────────────────────────────────────────────

  /**
   * @brief 获取底层 spdlog logger（供高级用法使用）
   */
  std::shared_ptr<spdlog::logger> Raw() const { return logger_; }

  // 带模块前缀的格式化日志
  template <typename... Args>
  void Trace(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::trace, module, fmt_str, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Debug(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::debug, module, fmt_str, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Info(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::info, module, fmt_str, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Warn(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::warn, module, fmt_str, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Error(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::err, module, fmt_str, std::forward<Args>(args)...);
  }

  template <typename... Args>
  void Critical(const std::string& module, fmt::format_string<Args...> fmt_str, Args&&... args) {
    Log(spdlog::level::critical, module, fmt_str, std::forward<Args>(args)...);
  }

 private:
  Logger() = default;

  template <typename... Args>
  void Log(spdlog::level::level_enum level, const std::string& module,
           fmt::format_string<Args...> fmt_str, Args&&... args) {
    if (!logger_) {
      Init();  // 懒初始化兜底，保证未显式初始化时也能用
    }
    // 拼接模块前缀：[module] message
    auto message =
        fmt::format("[{}] {}", module, fmt::format(fmt_str, std::forward<Args>(args)...));
    logger_->log(level, message);
  }

  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex init_mutex_;
  bool initialized_{false};
};

}  // namespace mv

// ── 便捷宏（推荐使用，自动传入模块名）────────────────────────────────────────
// 使用方式：MV_LOG_INFO("module_name", "message {}", value)

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define MV_LOG_TRACE(module, ...) mv::Logger::Instance().Trace(module, __VA_ARGS__)
#define MV_LOG_DEBUG(module, ...) mv::Logger::Instance().Debug(module, __VA_ARGS__)
#define MV_LOG_INFO(module, ...) mv::Logger::Instance().Info(module, __VA_ARGS__)
#define MV_LOG_WARN(module, ...) mv::Logger::Instance().Warn(module, __VA_ARGS__)
#define MV_LOG_ERROR(module, ...) mv::Logger::Instance().Error(module, __VA_ARGS__)
#define MV_LOG_CRITICAL(module, ...) mv::Logger::Instance().Critical(module, __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
