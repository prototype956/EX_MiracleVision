/**
 * @file tf_publisher.hpp
 * @brief TF 坐标系变换发布子模块
 *
 * 推送到 Foxglove 标准 /tf topic（FrameTransforms schema），
 * 3D 面板可直观显示 world→gimbal→camera 的坐标系树。
 */
#pragma once

#include <mutex>
#include <string>

#include <Eigen/Dense>
#include <foxglove/context.hpp>
#include <foxglove/schemas.hpp>
#include <optional>

namespace mv::tool::detail {

class TfPublisher {
 public:
  explicit TfPublisher(foxglove::Context ctx);

  /**
   * @brief 发布一条坐标系变换
   * @param parent   父坐标系名称（如 "world"）
   * @param child    子坐标系名称（如 "gimbal"）
   * @param T        4×4 齐次变换矩阵（child 相对于 parent 的位姿）
   * @param ts_ns    时间戳（纳秒）
   */
  void Publish(const std::string& parent, const std::string& child, const Eigen::Matrix4d& T,
               uint64_t ts_ns);

 private:
  void EnsureChannel();

  foxglove::Context ctx_;
  std::optional<foxglove::schemas::FrameTransformsChannel> tf_ch_;
  std::mutex mtx_;
};

}  // namespace mv::tool::detail
