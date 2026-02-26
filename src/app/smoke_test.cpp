/**
 * @file smoke_test.cpp
 * @brief 阶段一冒烟测试 —— 验证 ConfigManager 和 Logger 的基本行为
 *        非正式测试，用于快速确认功能，后续会迁移到 GTest
 */
#include "core/config.hpp"
#include "core/logger.hpp"

#include <cassert>
#include <iostream>

// 简单断言宏（输出文件名 + 行号）
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define ASSERT_EQ(a, b)                                                                   \
  do {                                                                                    \
    if ((a) != (b)) {                                                                     \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << "  " << #a << " != " << #b \
                << "  (got: " << (a) << ")\n";                                            \
      return 1;                                                                           \
    }                                                                                     \
    std::cout << "[PASS] " << #a << " == " << #b << "\n";                                 \
  } while (false)

#define ASSERT_TRUE(cond)                                                                      \
  do {                                                                                         \
    if (!(cond)) {                                                                             \
      std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << "  " << #cond << " is false\n"; \
      return 1;                                                                                \
    }                                                                                          \
    std::cout << "[PASS] " << #cond << "\n";                                                   \
  } while (false)
// NOLINTEND(cppcoreguidelines-macro-usage)

int main() {
  std::cout << "===== 阶段一冒烟测试 =====\n\n";

  // ── 1. Logger 初始化 ──────────────────────────────────────────────────────
  std::cout << "--- Logger ---\n";
  mv::Logger::Instance().Init("logs");
  MV_LOG_INFO("smoke_test", "Logger 初始化成功");
  MV_LOG_DEBUG("smoke_test", "调试信息: value = {}", 42);
  MV_LOG_WARN("smoke_test", "这是一条警告");
  std::cout << "[PASS] Logger 无崩溃\n\n";

  // ── 2. ConfigManager 加载 ─────────────────────────────────────────────────
  std::cout << "--- ConfigManager ---\n";
  auto& cfg = mv::ConfigManager::Instance();

  cfg.Load(CONFIG_FILE_PATH "/vision.yaml");

  // 检查键是否存在
  ASSERT_TRUE(cfg.Has("system.log_dir"));
  ASSERT_TRUE(cfg.Has("auto_aim.enemy_color"));
  ASSERT_TRUE(!cfg.Has("this.key.does.not.exist"));

  // 读取字符串
  auto log_dir = cfg.Get<std::string>("system.log_dir", "default_logs");
  ASSERT_EQ(log_dir, std::string("logs"));

  auto enemy_color = cfg.Get<std::string>("auto_aim.enemy_color", "blue");
  ASSERT_EQ(enemy_color, std::string("red"));

  // 读取数值
  auto min_count = cfg.Get<int>("auto_aim.tracker.min_detect_count", 0);
  ASSERT_EQ(min_count, 5);

  auto yaw_offset = cfg.Get<double>("auto_aim.aimer.yaw_offset", 0.0);
  ASSERT_EQ(yaw_offset, -2.0);

  // 读取布尔
  auto auto_fire = cfg.Get<bool>("auto_aim.shooter.auto_fire", false);
  ASSERT_TRUE(auto_fire);

  // 读取不存在的键，应返回默认值
  auto missing = cfg.Get<int>("no.such.key", 999);
  ASSERT_EQ(missing, 999);

  // GetRequired 对存在的键
  try {
    auto color = cfg.GetRequired<std::string>("auto_aim.enemy_color");
    ASSERT_EQ(color, std::string("red"));
  } catch (...) {
    std::cerr << "[FAIL] GetRequired 对已存在的键抛了异常\n";
    return 1;
  }

  // GetRequired 对不存在的键应抛 out_of_range
  bool threw = false;
  try {
    cfg.GetRequired<int>("totally.missing");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  ASSERT_TRUE(threw);

  // 命名空间加载：把 auto_aim.yaml 加载到 "aim" 命名空间，验证命名空间隔离
  cfg.Load(CONFIG_FILE_PATH "/auto_aim.yaml", "aim");
  ASSERT_TRUE(cfg.Has("aim.enemy_color"));  // 命名空间路径
  auto aim_color = cfg.Get<std::string>("aim.enemy_color", "blue");
  ASSERT_EQ(aim_color, std::string("red"));

  // 热重载：重新读取所有已加载文件，数据应保持不变
  cfg.Reload();
  ASSERT_TRUE(cfg.Has("auto_aim.enemy_color"));

  // 加载文件列表
  auto files = cfg.LoadedFiles();
  ASSERT_TRUE(files.size() >= 1);

  std::cout << "\n===== 所有测试通过 ✓ =====\n";
  MV_LOG_INFO("smoke_test", "阶段一冒烟测试全部通过");
  return 0;
}
