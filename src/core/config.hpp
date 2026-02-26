/**
 * @file config.hpp
 * @brief 统一配置管理器（header-only 单例）
 *
 * 【为什么用单例而不是全局变量或依赖注入】
 *   比赛代码生命周期简单，配置对象整个程序只需要一份，
 *   单例比全局变量多一层懒初始化保证（第一次 Instance() 时构造），
 *   比依赖注入少大量样板代码。若未来要做单测，用 Load() 重置即可。
 *
 * 【为什么用 yaml-cpp 而不是 nlohmann_json / XML】
 *   YAML 支持注释，方便调参时在文件里写备注；
 *   yaml-cpp 已是项目依赖（Foxglove 传递引入），无需额外安装；
 *   nlohmann_json 不支持注释，XML 层级冗余、解析慢。
 *
 * 【为什么要命名空间隔离（Solution A 的根本原因）】
 *   yaml-cpp 的 YAML::Node 是引用计数句柄，"赋值"只改变句柄指向，
 *   不修改原树节点。尝试用单棵树 + 递归合并的方案均失败（见 docs 记录）。
 *   改为每个命名空间独立持有一棵树，写入时通过 dest[key]=val 对已持有的
 *   树节点赋值，彻底绕开这个陷阱，同时还顺便实现了命名空间隔离。
 *
 * 【为什么用 shared_mutex 而不是 mutex】
 *   配置在运行时几乎只读（写只发生在启动和热重载两个时机），
 *   shared_mutex 允许多线程并发读，只有 Load/Reload 时独占写，
 *   读操作零锁争用。
 *
 * 【快速上手】
 * @code
 *   auto& cfg = mv::ConfigManager::Instance();
 *
 *   // 启动时加载（通常在 main 里做一次）
 *   cfg.Load("configs/vision.yaml");                     // 根命名空间
 *   cfg.Load("configs/armor/basic_armor.yaml", "armor"); // armor 命名空间
 *
 *   // 读取：找不到时返回默认值，绝不抛异常
 *   auto color  = cfg.Get<std::string>("enemy_color", "red");
 *   auto thresh = cfg.Get<int>("armor.light_thresh", 100);
 *
 *   // 读取：必须存在，不存在抛 std::out_of_range
 *   auto fps = cfg.GetRequired<int>("camera.fps");
 *
 *   // 检查 key 是否存在
 *   if (cfg.Has("armor.debug_mode")) { ... }
 *
 *   // 热重载（如接收到 SIGUSR1 时调用）
 *   cfg.Reload();
 * @endcode
 */
#pragma once

#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace mv {

class ConfigManager {
 public:
  // ── 单例访问 ─────────────────────────────────────────────────────────────
  static ConfigManager& Instance() {
    static ConfigManager inst;
    return inst;
  }

  // 禁止拷贝 / 移动
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;
  ConfigManager(ConfigManager&&) = delete;
  ConfigManager& operator=(ConfigManager&&) = delete;
  ~ConfigManager() = default;

  // ── 加载接口 ──────────────────────────────────────────────────────────────

  /**
   * @brief 加载一个 YAML 文件，合并到指定命名空间
   *
   * 同一命名空间多次 Load 时，后加载的文件会覆盖同名顶层 key，
   * 嵌套 Map 采用深度合并（保留已有 key）。
   * 这样可以先加载通用配置，再加载机器特定配置来覆盖少数参数。
   *
   * @param file_path    YAML 文件路径（相对路径以进程工作目录为基准）
   * @param name_space   命名空间前缀。留空则合并到根节点，
   *                     "armor" 则文件中的 "foo" 通过 "armor.foo" 访问。
   * @throw YAML::BadFile         文件不存在或无权限读取
   * @throw YAML::ParserException YAML 语法错误
   */
  void Load(const std::string& file_path, const std::string& name_space = "") {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    // namespaces_[name_space] 若不存在会默认构造出空 Node，
    // FlatMerge 拿到的是 map 里实际存储的 Node 引用，operator[] 写入永久生效。
    // 若改为先 Clone 一份再合并，写入结果不会回到 map——yaml-cpp 的值语义陷阱。
    YAML::Node incoming = YAML::LoadFile(file_path);
    auto& target = namespaces_[name_space];
    FlatMerge(target, incoming);
    loaded_files_.emplace_back(file_path, name_space);
  }

  /**
   * @brief 热重载：重新读取所有已加载的文件，刷新内存中的配置
   *
   * 先在临时 map 里完整重建，成功后用 move 原子替换 namespaces_。
   * 这样即使某个文件解析失败（抛异常），原有配置也不受影响。
   * 若用"先 clear 再 Load"的写法，任何中途失败都会导致配置部分丢失。
   *
   * @note 重载期间持有独占写锁，不要在图像处理主循环里调用，
   *       建议在独立的管理线程中接收信号后调用（如 SIGUSR1）。
   * @throw YAML::Exception 任意文件解析失败时抛出，原配置保持不变
   */
  void Reload() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    // 先在临时副本上重建，成功后才替换，避免部分失败导致数据丢失
    std::unordered_map<std::string, YAML::Node> new_namespaces;
    for (const auto& [path, name_space] : loaded_files_) {
      YAML::Node incoming = YAML::LoadFile(path);  // 失败时抛异常，new_namespaces 保持完整
      auto& target = new_namespaces[name_space];
      FlatMerge(target, incoming);
    }
    namespaces_ = std::move(new_namespaces);
  }

  // ── 读取接口 ──────────────────────────────────────────────────────────────

  /**
   * @brief 检查一个点分隔的键路径是否存在
   *
   * 用于在读取前做条件判断，避免 GetRequired 抛异常。
   * 底层调用 Resolve()，两阶段查找（命名空间优先 → 根回退）。
   *
   * @param key_path  点分隔路径，如 "armor.light_thresh"
   * @return true 路径存在且不为 YAML::Null
   */
  bool Has(const std::string& key_path) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return Resolve(key_path).IsDefined();
  }

  /**
   * @brief 读取值，键不存在或类型转换失败时返回 default_val
   *
   * 绝不抛异常——所有错误路径都静默返回默认值。
   * 这样调用方不需要 try-catch，适合频繁调用的参数读取。
   * 如果需要发现配置缺失，用 GetRequired 或先 Has 再 Get。
   *
   * @tparam T           目标类型（int / float / double / std::string / bool 等）
   * @param key_path     点分隔路径，如 "armor.light_thresh"
   * @param default_val  键不存在或类型转换失败时的兜底值
   */
  template <typename T>
  T Get(const std::string& key_path, const T& default_val = T{}) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    try {
      YAML::Node node = Resolve(key_path);
      if (!node.IsDefined() || node.IsNull()) {
        return default_val;
      }
      return node.as<T>();
    } catch (...) {
      return default_val;
    }
  }

  /**
   * @brief 读取值，键不存在时抛出异常（硬依赖参数用此接口）
   *
   * 与 Get 的区别：这里的"缺失"是程序逻辑错误，不是正常情况，
   * 用异常而非默认值可以让错误在启动阶段就暴露，而不是带着错误值运行。
   * 适用于相机分辨率、通信波特率等启动必须有的参数。
   *
   * @tparam T        目标类型
   * @param key_path  点分隔路径
   * @throw std::out_of_range   键不存在
   * @throw std::runtime_error  类型转换失败（如把字符串转 int）
   */
  template <typename T>
  T GetRequired(const std::string& key_path) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    YAML::Node node = Resolve(key_path);
    if (!node.IsDefined() || node.IsNull()) {
      throw std::out_of_range("ConfigManager: required key not found: " + key_path);
    }
    try {
      return node.as<T>();
    } catch (const std::exception& err) {
      throw std::runtime_error("ConfigManager: type conversion failed for key '" + key_path +
                               "': " + err.what());
    }
  }

  /**
   * @brief 获取某命名空间下的完整 YAML 节点（只读副本）
   *
   * 返回的是 Clone 副本，外部修改不影响内部存储。
   * 适合把整个子树传给另一个模块自行解析，避免多次 Get 调用。
   * 例如把 "armor" 子树传给装甲板检测器，让它自己取参数。
   *
   * @param ns_path  点分隔路径（留空则返回所有命名空间合并后的整棵树）
   */
  YAML::Node Subtree(const std::string& ns_path = "") const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    if (ns_path.empty()) {
      // 返回整棵合并视图（克隆一份，防止外部修改）
      return YAML::Clone(MergedView());
    }
    // 先按第一个 '.' 切分：namespace 部分 vs 子路径部分
    auto parts = SplitKey(ns_path);
    // 在合并视图上查找
    return YAML::Clone(ResolveInView(MergedView(), parts));
  }

  /**
   * @brief 返回所有已加载的文件路径列表（用于调试/诊断）
   */
  std::vector<std::string> LoadedFiles() const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    std::vector<std::string> paths;
    paths.reserve(loaded_files_.size());
    for (const auto& [path, ignored] : loaded_files_) {
      paths.emplace_back(path);
    }
    return paths;
  }

 private:
  ConfigManager() = default;

  // ── Solution A 存储模型 ───────────────────────────────────────────────────
  //
  // 用 unordered_map<string, YAML::Node> 而不是单棵 YAML::Node 树的原因：
  //
  // yaml-cpp 的 YAML::Node 是引用计数句柄，行为类似 shared_ptr<NodeData>。
  // "node = other_node" 让 node 指向 other_node 的数据，不修改原来指向的数据。
  // "local_copy[key] = val" 只修改 local_copy 指向的树，不影响原始树。
  //
  // 这导致所有"先 Clone/拷贝，再写入，最后赋回"的方案都失效。
  // 唯一可靠的写入方式是：直接对 map 里存储的 Node 用 operator[] 赋值，
  // 因为 map 存储的是实际的 Node 对象（不是指针），引用绑定到它上面写入是安全的。
  //
  // 查询时的两阶段策略（为什么不直接用全路径在根树上查）：
  //   假设用户加载了 vision.yaml 到根命名空间，又加载了 basic_armor.yaml 到 "armor" 命名空间。
  //   访问 "armor.light_thresh" 时：
  //     阶段1：发现 namespaces_["armor"] 存在，在其中找 "light_thresh" ✓
  //     阶段2（兜底）：如果用户把 armor 配置直接放在 vision.yaml 里，
  //                    "armor.light_thresh" 在根命名空间的树上也能找到。
  //   这样两种组织方式都能工作，调用方不需要关心文件怎么分割。

  // ── 内部工具函数 ──────────────────────────────────────────────────────────

  /**
   * @brief 将 from 的顶层 key 逐一写入 dest（Map 深度合并，非 Map 直接覆盖）
   *
   * 为什么不直接 dest = from：
   *   对局部引用赋值只改变引用绑定，不修改 map 里存储的 Node 数据。
   *   必须用 dest[key] = item.second 对树节点内的字段一一赋值。
   *
   * 为什么 dest 用引用而 from 用 const 引用：
   *   dest 必须是 map 里存储的实际 Node 的引用，才能让写入永久生效。
   *   from 只读，const ref 避免不必要的拷贝。
   */
  static void FlatMerge(YAML::Node& dest, const YAML::Node& from) {
    if (!from.IsMap()) {
      // from 是标量/序列：无法通过 key 遍历，用 YAML::Node 的赋值操作符
      // 注意：dest 是引用，这里赋值的是引用绑定的那个 Node 对象本身——
      // 对于 unordered_map 里存储的 Node，这是合法的
      dest = YAML::Clone(from);
      return;
    }
    for (const auto& item : from) {
      auto key = item.first.as<std::string>();
      if (dest[key] && dest[key].IsMap() && item.second.IsMap()) {
        // 两边都是 Map：递归合并，保留 dest 中已有但 from 中没有的 key
        FlatMergeMap(dest[key], item.second);
      } else {
        dest[key] = item.second;
      }
    }
  }

  /**
   * @brief 递归合并两棵 Map 节点
   *
   * 为什么 dest_map 按值传（而不是引用）：
   *   yaml-cpp 的 operator[] 会自动通过内部引用计数找到树节点并修改，
   *   按值传入的 Node 句柄和原始句柄指向同一块数据，写入同样生效。
   *   若改成引用，clang-tidy 会要求 const ref，但 const Node 上 operator[] 会返回
   *   Undefined，写不进去。这是 yaml-cpp API 设计的历史遗留问题，按值传是惯用写法。
   *
   * @param dest_map  目标 Map 节点句柄（按值传，但写入通过引用计数生效到原树）
   * @param from_map  源 Map 节点（只读）
   */
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  static void FlatMergeMap(YAML::Node dest_map, const YAML::Node& from_map) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    for (const auto& item : from_map) {
      auto key = item.first.as<std::string>();
      if (dest_map[key] && dest_map[key].IsMap() && item.second.IsMap()) {
        FlatMergeMap(dest_map[key], item.second);
      } else {
        dest_map[key] = item.second;
      }
    }
  }

  /**
   * @brief 解析 key_path，返回对应的 YAML 节点（找不到时返回 Undefined）
   *
   * 两阶段查找而不是直接在根树上查的原因：
   *   加载到命名空间 "armor" 的配置存在 namespaces_["armor"] 树里，
   *   根树 namespaces_[""] 里不包含这部分数据。
   *   只有先检查命名空间树，才能找到 "armor.light_thresh"。
   *   阶段2 的根回退是为了兼容"所有配置都加载到根"的使用方式。
   */
  YAML::Node Resolve(const std::string& key_path) const {
    auto parts = SplitKey(key_path);
    if (parts.empty()) {
      return YAML::Node{YAML::NodeType::Undefined};
    }

    // 阶段1：把第一段当命名空间，查剩余路径
    const auto& first = parts[0];
    auto ns_it = namespaces_.find(first);
    if (ns_it != namespaces_.end() && parts.size() > 1) {
      std::vector<std::string> sub_parts(parts.begin() + 1, parts.end());
      YAML::Node result = WalkNode(YAML::Clone(ns_it->second), sub_parts);
      if (result.IsDefined()) {
        return result;
      }
    }

    // 阶段2：回退到根命名空间（ns=""），用完整路径查找
    auto root_it = namespaces_.find("");
    if (root_it != namespaces_.end()) {
      return WalkNode(YAML::Clone(root_it->second), parts);
    }

    return YAML::Node{YAML::NodeType::Undefined};
  }

  /**
   * @brief 在一个 Node 上按路径数组逐层步进，返回目标节点
   *
   * 为什么按值接收 node 并立即 move 到局部变量：
   *   yaml-cpp 的 operator[] 在 const Node 上会返回 Undefined，
   *   遍历时需要对 cur 做非 const 访问。
   *   Clone 后按值传入，保证操作的是副本，不影响原始树。
   *   NOLINT 标注原因：clang-tidy 建议改成 const ref + 内部 Clone，
   *   但 yaml-cpp 句柄语义下两种写法等价，保持现有写法可读性更高。
   */
  static YAML::Node WalkNode(YAML::Node node, const std::vector<std::string>& parts) {
    YAML::Node cur = std::move(node);
    for (const auto& part : parts) {
      if (!cur.IsMap()) {
        return YAML::Node{YAML::NodeType::Undefined};
      }
      bool found = false;
      for (const auto& kv : cur) {
        if (kv.first.as<std::string>() == part) {
          cur = kv.second;
          found = true;
          break;
        }
      }
      if (!found) {
        return YAML::Node{YAML::NodeType::Undefined};
      }
    }
    return cur;
  }

  /**
   * @brief 构建所有命名空间合并后的视图（仅供 Subtree() 返回整棵树时使用）
   *
   * 为什么不直接存一棵合并树：
   *   Load 时需要按命名空间独立写入（Solution A 的核心），
   *   合并视图只在 Subtree() 这个低频调用时才需要，临时构建比维护双份数据更简单。
   */
  YAML::Node MergedView() const {
    YAML::Node view;
    // 先把根命名空间的内容铺进去
    auto root_it = namespaces_.find("");
    if (root_it != namespaces_.end() && root_it->second.IsMap()) {
      for (const auto& item : root_it->second) {
        view[item.first.as<std::string>()] = item.second;
      }
    }
    // 再把各命名空间作为顶层 key 铺进去（覆盖根命名空间同名 key）
    for (const auto& [ns, node] : namespaces_) {
      if (!ns.empty()) {
        view[ns] = node;
      }
    }
    return view;
  }

  /**
   * @brief 在视图 Node 上用路径数组步进（供 Subtree 使用，与 WalkNode 同理）
   *
   * 独立成函数而不复用 WalkNode 的原因：
   *   Subtree 的入口参数是 MergedView() 返回的临时对象，
   *   单独命名可以在调用点表达"在视图上查"的语义，区别于"在存储树上查"。
   */
  static YAML::Node ResolveInView(YAML::Node view, const std::vector<std::string>& parts) {
    return WalkNode(std::move(view), parts);
  }

  /**
   * @brief 按 '.' 分割键路径
   */
  static std::vector<std::string> SplitKey(const std::string& key_path) {
    std::vector<std::string> parts;
    std::istringstream stream(key_path);
    std::string token;
    while (std::getline(stream, token, '.')) {
      if (!token.empty()) {
        parts.emplace_back(token);
      }
    }
    return parts;
  }

  // ── 数据成员 ──────────────────────────────────────────────────────────────
  mutable std::shared_mutex rw_mutex_;
  // 方案 A 核心存储：namespace_name → YAML::Node
  // ""（空字符串）表示无命名空间的根配置
  std::unordered_map<std::string, YAML::Node> namespaces_;
  // 用于热重载：记录 {文件路径, 命名空间}
  std::vector<std::pair<std::string, std::string>> loaded_files_;
};

}  // namespace mv
