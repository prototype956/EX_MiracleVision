/**
 * @file param_tuner.cpp
 * @brief ParamTuner 实现
 */
#include "tool/debug/param_tuner.hpp"

#include <deque>
#include <fstream>
#include <stdexcept>

#include <filesystem>
#include <opencv2/highgui.hpp>
#include <yaml-cpp/yaml.h>

namespace mv::tool {

// ── Impl ──────────────────────────────────────────────────────────────────

struct ParamEntry {
  ParamDesc desc;
  int tb_val{0};  ///< Trackbar 绑定的整数变量（deque 保证地址稳定）
};

struct ParamTuner::Impl {
  std::string win_name;            // lower_case，无下划线后缀（struct public）
  std::deque<ParamEntry> entries;  // deque: push_back 不使已有元素地址失效
};

// ── 构造 / 析构 ────────────────────────────────────────────────────────────

ParamTuner::ParamTuner() : impl_(std::make_unique<Impl>()) {}
ParamTuner::~ParamTuner() = default;

ParamTuner::ParamTuner(ParamTuner&&) noexcept = default;
ParamTuner& ParamTuner::operator=(ParamTuner&&) noexcept = default;

// ── 公开接口实现 ────────────────────────────────────────────────────────────

void ParamTuner::AttachToWindow(const std::string& win_name) {
  impl_->win_name = win_name;
}

void ParamTuner::AddParam(ParamDesc desc) {
  if (impl_->win_name.empty()) {
    throw std::logic_error("ParamTuner::AddParam called before AttachToWindow");
  }
  // push_back 先追加，再取最后一个元素的地址（deque 稳定）
  impl_->entries.push_back({std::move(desc), 0});
  auto& entry = impl_->entries.back();
  entry.tb_val = entry.desc.init_val;

  cv::createTrackbar(entry.desc.label, impl_->win_name, &entry.tb_val, entry.desc.max_val, nullptr);
}

void ParamTuner::ApplyAll() {
  for (auto& entry : impl_->entries) {
    if (entry.desc.apply) {
      entry.desc.apply(entry.tb_val);
    }
  }
}

void ParamTuner::SaveTo(const std::string& yaml_path, std::string_view section) const {
  // 加载已有文件（若存在），否则从空节点开始
  YAML::Node root;
  {
    std::ifstream ifs(yaml_path);
    if (ifs.good()) {
      root = YAML::LoadFile(yaml_path);
    }
  }

  // 更新目标节点
  YAML::Node sec = root[std::string(section)];
  for (const auto& entry : impl_->entries) {
    if (entry.desc.get_val) {
      sec[entry.desc.yaml_key] = entry.desc.get_val();
    }
  }

  // 写回文件（自动创建父目录）
  std::filesystem::create_directories(std::filesystem::path(yaml_path).parent_path());
  std::ofstream ofs(yaml_path);
  if (!ofs.is_open()) {
    throw std::runtime_error("ParamTuner::SaveTo: cannot open file: " + yaml_path);
  }
  ofs << root;
}

}  // namespace mv::tool
