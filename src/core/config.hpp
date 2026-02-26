/**
 * @file config.hpp
 * @brief 统一配置管理器 —— 基于 yaml-cpp 的单例，支持多文件合并、类型安全读取、热重载
 *
 * 设计原则：
 *  - 所有配置统一由此类管理，禁止在其他模块中直接使用 YAML::LoadFile
 *  - 支持将多个 YAML 文件合并到同一个命名空间（namespace）下
 *  - 线程安全（读多写少，用 shared_mutex 实现读写锁）
 *
 * 使用示例：
 * @code
 *   auto& cfg = mv::ConfigManager::Instance();
 *
 *   // 加载配置文件
 *   cfg.Load("configs/auto_aim.yaml");                   // 加载到根命名空间
 *   cfg.Load("configs/armor/basic_armor.yaml", "armor"); // 加载到 "armor" 子命名空间
 *
 *   // 读取值（找不到则返回默认值，不抛异常）
 *   auto color  = cfg.Get<std::string>("enemy_color", "red");
 *   auto thresh = cfg.Get<int>("armor.light_thresh", 100);
 *
 *   // 检查键是否存在
 *   if (cfg.Has("armor.debug_mode")) { ... }
 *
 *   // 热重载（重新读取所有已加载的文件）
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
   * @param file_path    YAML 文件的绝对/相对路径
   * @param name_space   命名空间前缀（留空则合并到根节点）
   *                     文件中的 key "foo" 在 name_space="armor" 下访问路径为 "armor.foo"
   * @throw YAML::BadFile      文件不存在
   * @throw YAML::ParserException 解析失败
   */
  void Load(const std::string& file_path, const std::string& name_space = "") {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    // 方案 A：每个命名空间独立存储一棵 YAML::Node 树，完全绕开 yaml-cpp 树写入陷阱。
    // 同一命名空间多次 Load 时，新文件的顶层 key 覆盖旧值（Map 合并），非 Map 节点直接替换。
    YAML::Node incoming = YAML::LoadFile(file_path);
    auto& target        = namespaces_[name_space];  // 若不存在则默认构造空 Node
    FlatMerge(target, incoming);
    loaded_files_.emplace_back(file_path, name_space);
  }

  /**
   * @brief 热重载：重新读取所有已加载的文件
   * @note  重载期间持有写锁，避免在高频路径上调用
   */
  void Reload() {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    // 先在临时副本上重建，成功后才替换，避免部分失败导致数据丢失
    std::unordered_map<std::string, YAML::Node> new_namespaces;
    for (const auto& [path, name_space] : loaded_files_) {
      YAML::Node incoming = YAML::LoadFile(path);  // 失败时抛异常，new_namespaces 保持完整
      auto& target        = new_namespaces[name_space];
      FlatMerge(target, incoming);
    }
    namespaces_ = std::move(new_namespaces);
  }

  // ── 读取接口 ──────────────────────────────────────────────────────────────

  /**
   * @brief 检查一个点分隔的键路径是否存在
   * @param key_path  点分隔路径，如 "armor.light_thresh"
   */
  bool Has(const std::string& key_path) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return Resolve(key_path).IsDefined();
  }

  /**
   * @brief 读取值，找不到时返回 default_val（不抛异常）
   * @tparam T           目标类型（int / float / double / std::string / bool 等）
   * @param key_path     点分隔路径，如 "armor.light_thresh"
   * @param default_val  键不存在或类型转换失败时的默认值
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
   * @brief 读取值，键不存在时抛出异常
   * @tparam T        目标类型
   * @param key_path  点分隔路径
   * @throw std::out_of_range   键不存在
   * @throw std::runtime_error  类型转换失败
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
   * @brief 获取一个子命名空间下的完整 YAML 节点（只读副本）
   * @param ns_path  点分隔路径（留空则返回整棵树）
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

  // ── 方案 A 核心：按命名空间独立存储 ──────────────────────────────────────
  //
  // 存储结构：namespaces_[""] 存放无命名空间的根配置，
  //           namespaces_["armor"] 存放 Load(path, "armor") 的配置，以此类推。
  //
  // 查询时通过 key_path 的第一段匹配命名空间：
  //   "system.log_dir"         → ns=""      subpath="system.log_dir"
  //   "armor.light_thresh"     → 先查 ns="armor" subpath="light_thresh"，
  //                              找不到再查 ns="" subpath="armor.light_thresh"
  //   "auto_aim.tracker.xxx"   → 同上，先查 ns="auto_aim"，再回退到 ns=""
  //
  // 这样既支持带命名空间的隔离加载，也支持把整个配置加载到根节点后用全路径访问。

  // ── 内部工具函数 ──────────────────────────────────────────────────────────

  /**
   * @brief 将 from 的顶层 key 逐一写入 dest（利用 yaml-cpp 的 operator[] 就地赋值）
   *
   * 核心规则：对 dest 本身赋值（dest = x）只改变局部句柄，不修改 dest 指向的树节点。
   * 只有 dest[key] = x 才能真正把数据写进去。
   * 因此这里遍历 from 的所有 key，通过 dest[key] = ... 逐一写入，永远不对 dest 整体赋值。
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
   * @brief 递归合并两棵 Map 节点（dest[key] 层面的写入，不对 dest 本身赋值）
   * @param dest_map  目标 Map 节点（yaml-cpp 按值传参，内部通过 operator[] 就地写入）
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
   * 查找策略（两阶段）：
   *   1. 把 key_path 首段作为命名空间名，在 namespaces_[首段] 中找剩余路径
   *   2. 若命名空间不存在或路径找不到，回退到 namespaces_[""] 中找完整路径
   */
  YAML::Node Resolve(const std::string& key_path) const {
    auto parts = SplitKey(key_path);
    if (parts.empty()) {
      return YAML::Node{YAML::NodeType::Undefined};
    }

    // 阶段1：把第一段当命名空间，查剩余路径
    const auto& first = parts[0];
    auto ns_it        = namespaces_.find(first);
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
   * @brief 在一个 Node 副本上按路径数组逐层步进（Clone 后操作，不影响原树）
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
          cur   = kv.second;
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
   * @brief 构建所有命名空间合并后的视图（仅用于 Subtree() 返回整棵树时）
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
   * @brief 在视图 Node 上用路径数组步进（供 Subtree 使用）
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
