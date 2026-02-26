/**
 * @file logger.hpp
 * @brief 统一日志系统（header-only 接口 + logger.cpp 实现）
 *
 * 【为什么封装 spdlog 而不直接用】
 *   spdlog 的全局函数（spdlog::info() 等）共享一个全局 logger，
 *   各模块混用时日志格式不统一，也无法在不改调用点的情况下整体控制级别。
 *   封装后，所有日志都经过 [module] 前缀格式化，可以在 grep 时精确过滤来源模块。
 *
 * 【为什么用单例而不是依赖注入】
 *   日志是横切关注点（cross-cutting concern），每个模块都需要，
 *   如果作为参数传递会导致所有构造函数都多一个 Logger 参数。
 *   单例在这里是合理的，与配置管理器同理。
 *
 * 【为什么 Init 分离出去（不在构造函数里初始化）】
 *   构造函数里无法控制日志目录路径和级别，而这两个参数需要从配置文件读取。
 *   先构造单例（零开销），等 ConfigManager 加载完配置后再调用 Init()，
 *   顺序依赖关系更清晰。未调用 Init 时会懒初始化到默认值，保证不崩溃。
 *
 * 【快速上手】
 * @code
 *   // main() 最开始调用一次（在 ConfigManager 加载配置之后）
 *   mv::Logger::Instance().Init("logs");
 *
 *   // 各模块里直接用宏（推荐），module 字段用于过滤日志来源
 *   MV_LOG_INFO("armor",  "检测到 {} 块装甲板", count);
 *   MV_LOG_WARN("serial", "串口校验失败，重试...");
 *   MV_LOG_ERROR("cam",   "相机打开失败: {}", err_msg);
 *
 *   // 不用宏也可以直接调用
 *   mv::Logger::Instance().Debug("tracker", "预测坐标 ({}, {})", x, y);
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
   * @brief 初始化日志系统，应在 main() 加载完配置后调用一次
   *
   * 为什么要显式 Init 而不是在构造函数里做：
   *   日志目录和级别来自 ConfigManager，而 ConfigManager 比 Logger 稍晚就绪。
   *   显式 Init 把这个顺序依赖暴露出来，避免隐式耦合。
   *   若忘记调用，Log() 会触发懒初始化兜底（使用默认参数），不崩溃但会有警告。
   *
   * @param log_dir      日志文件目录，不存在时自动创建
   * @param level        控制台输出的最低级别（文件始终记录 trace 及以上）
   * @param console_on   false 时只写文件，不打印控制台（如部署到无终端环境）
   */
  void Init(const std::string& log_dir = "logs",
            spdlog::level::level_enum level = spdlog::level::debug, bool console_on = true);

  /**
   * @brief 运行时修改日志级别（调试时临时开启 trace，比赛时切换到 warn）
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
      Init();  // 懒初始化兜底：若 main 里忘记调用 Init，至少不会崩溃
    }
    // 把 [module] 拼到消息前缀，而不是用 spdlog 的 logger name 字段，
    // 原因是 spdlog logger name 是构造时固定的，运行时无法按调用点变化。
    auto message =
        fmt::format("[{}] {}", module, fmt::format(fmt_str, std::forward<Args>(args)...));
    logger_->log(level, message);
  }

  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex init_mutex_;
  bool initialized_{false};
};

}  // namespace mv

// ── 便捷宏 ────────────────────────────────────────────────────────────────────
// 用宏而不是 inline 模板函数的原因：
//   fmt::format_string<Args...> 是编译期字符串字面量类型，
//   通过宏传递时保留了调用点的类型信息（编译期格式检查）。
//   若改成 inline 函数转发，格式串退化为运行时 std::string，失去编译期检查。
//   clang-tidy 的 "variadic macro" 警告在这里是误报，已在 CI 配置中豁免。
//
// 使用方式：MV_LOG_INFO("模块名", "消息 {}", 变量)
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define MV_LOG_TRACE(module, ...) mv::Logger::Instance().Trace(module, __VA_ARGS__)
#define MV_LOG_DEBUG(module, ...) mv::Logger::Instance().Debug(module, __VA_ARGS__)
#define MV_LOG_INFO(module, ...) mv::Logger::Instance().Info(module, __VA_ARGS__)
#define MV_LOG_WARN(module, ...) mv::Logger::Instance().Warn(module, __VA_ARGS__)
#define MV_LOG_ERROR(module, ...) mv::Logger::Instance().Error(module, __VA_ARGS__)
#define MV_LOG_CRITICAL(module, ...) mv::Logger::Instance().Critical(module, __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
