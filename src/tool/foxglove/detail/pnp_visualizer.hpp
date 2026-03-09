/**
 * @file pnp_visualizer.hpp
 * @brief PnP 解算专项可视化（三层输出）
 *
 * 【三层输出说明】
 *
 *   Layer 1 — pnp/debug_image (RawImage)
 *     在底图上叠加原始角点（绿色实心圆）、重投影角点（青色）、
 *     第二 IPPE 解的重投影角点（橙色十字）；方便对比解的正确性。
 *
 *   Layer 2 — pnp/axes_3d (SceneUpdate)
 *     在云台坐标系中展示每块已解算装甲板的 RGB XYZ 坐标轴箭头，
 *     轴长按距离自适应 (dist*0.12, [0.08, 0.4] m)。
 *     若存在第二 IPPE 解，颜色为橙色半透明轴，便于识别解的歧义性。
 *
 *   Layer 3 — pnp/residuals (JSON)
 *     每块装甲板的 xyz (m) / yaw_deg / pitch_deg / dist_m / reproj_error_px，
 *     在 Foxglove 原始 JSON 面板中支持搭建图表。
 *
 * 【Channel 初始化时机】
 *   三个 channel 均在构造时即创建（就地 EnsureChannels()），
 *   确保客户端握手后即可看到所有话题，而不是等到有检测结果才出现。
 *
 * 线程安全：mtx_ 保护所有 channel 初始化和消息发送。
 */
#pragma once

#include "interfaces/types.hpp"

#include <mutex>
#include <vector>

#include <foxglove/channel.hpp>
#include <foxglove/context.hpp>
#include <foxglove/schemas.hpp>
#include <opencv2/core.hpp>
#include <optional>

namespace mv::tool::detail {

class PnpVisualizer {
 public:
  /** @param ctx  foxglove 全局上下文，由 FoxgloveSink::Impl 传入并共享 */
  explicit PnpVisualizer(foxglove::Context ctx);

  /**
   * @brief 发布 PnP 调试三层数据
   *
   * 若 frame 为空，则跳过 debug_image 层的发布；
   * 若 dets 中没有 is_solved=true 的结果，会推送空的 SceneUpdate，
   * 确保 Foxglove 清除上一帧残留的坐标轴实体。
   *
   * @param dets    检测结果（仅 is_solved=true 的影响 3D 输出）
   * @param frame   原始底图（用于 debug_image 绘制）
   * @param ts_ns   时间戳（纳秒，须已由 ResolveTs 解析）
   */
  void Publish(const std::vector<mv::Detection>& dets, const cv::Mat& frame, uint64_t ts_ns);

 private:
  /**
   * @brief 创建三个 channel（在构造时即调用）
   *
   * 不同于其他子发布器的懒创建策略，PnpVisualizer 在构造时就
   * advertise 三个 channel，确保客户端登陆后话题列表即刻可见。
   */
  void EnsureChannels();

  // ── 成员变量 ─────────────────────────────────────────────────────────────
  foxglove::Context ctx_;                                                  ///< SDK 全局上下文
  std::optional<foxglove::schemas::RawImageChannel> debug_img_ch_;        ///< pnp/debug_image channel
  std::optional<foxglove::schemas::SceneUpdateChannel> axes_ch_;          ///< pnp/axes_3d channel
  std::optional<foxglove::RawChannel> residuals_ch_;                      ///< pnp/residuals JSON channel
  std::mutex mtx_;                                                         ///< 保护 channel 初始化和消息发送
};

}  // namespace mv::tool::detail
