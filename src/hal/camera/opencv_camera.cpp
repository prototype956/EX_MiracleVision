/**
 * @file opencv_camera.cpp
 * @brief OpenCV VideoCapture 相机 Pimpl 实现
 *
 * 【source 字段的双重语义】
 *   YAML 中 `source` 可以是整数（设备索引）或字符串（文件路径 / RTSP URL）。
 *   yaml-cpp 没有联合类型，我们用 try-as-int 先尝试解析为整数，
 *   失败则当字符串处理，兼顾两种用例而不需要在配置中区分字段名。
 *
 * 【为什么设置 CAP_PROP_BUFFERCOUNT = 1？】
 *   默认 V4L2 会缓冲 4 帧，cap.read() 实际返回的是最老的那帧，
 *   导致自瞄看到的是约 4 帧前的图像（~66ms @ 60fps），目标已经移走了。
 *   设置 buffer=1 后，每次 read() 都会丢弃旧帧、取最新帧，
 *   代价是偶尔掉帧（buffer 太小会来不及 DMA），实测 60fps 下稳定。
 */

#include "opencv_camera.hpp"

#include <opencv2/videoio.hpp>

#include "../../core/logger.hpp"

namespace mv::hal {

// ============================================================================
// Impl 定义
// ============================================================================

struct OpenCvCamera::Impl {
  cv::VideoCapture cap;
  bool is_open{false};
};

// ============================================================================
// 构造 / 析构 / 移动
// ============================================================================

OpenCvCamera::OpenCvCamera() : impl_(std::make_unique<Impl>()) {}

OpenCvCamera::~OpenCvCamera() {
  Close();
}

OpenCvCamera::OpenCvCamera(OpenCvCamera&&) noexcept = default;
OpenCvCamera& OpenCvCamera::operator=(OpenCvCamera&&) noexcept = default;

// ============================================================================
// ICamera 接口实现
// ============================================================================

bool OpenCvCamera::Open(const YAML::Node& config) {
  if (impl_->is_open) {
    return true;
  }

  // source 字段：先尝试解析为 int（设备索引），否则当字符串（路径/URL）
  const YAML::Node src_node = config["source"];
  bool opened = false;

  if (src_node && src_node.IsScalar()) {
    try {
      const int idx = src_node.as<int>();
      opened = impl_->cap.open(idx, cv::CAP_V4L2);
      if (!opened) {
        MV_LOG_ERROR("HAL.Camera.OpenCV", "failed to open device index {}", idx);
        return false;
      }
      MV_LOG_INFO("HAL.Camera.OpenCV", "opened device index {}", idx);
    } catch (const YAML::BadConversion&) {
      const std::string path = src_node.as<std::string>();
      opened = impl_->cap.open(path);
      if (!opened) {
        MV_LOG_ERROR("HAL.Camera.OpenCV", "failed to open '{}'", path);
        return false;
      }
      MV_LOG_INFO("HAL.Camera.OpenCV", "opened '{}'", path);
    }
  } else {
    MV_LOG_ERROR("HAL.Camera.OpenCV", "missing or invalid 'source' field in config");
    return false;
  }

  // 设置分辨率和帧率（仅作 hint，硬件可能调整到最近支持的值）
  if (const int width = config["width"].as<int>(0); width > 0) {
    impl_->cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
  }
  if (const int height = config["height"].as<int>(0); height > 0) {
    impl_->cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
  }
  if (const int fps = config["fps"].as<int>(0); fps > 0) {
    impl_->cap.set(cv::CAP_PROP_FPS, fps);
  }

  // 减少 V4L2 帧缓冲数，获取最新帧（原因见文件头注释）
  impl_->cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

  impl_->is_open = true;
  return true;
}

void OpenCvCamera::Close() {
  if (!impl_->is_open) {
    return;
  }
  impl_->cap.release();
  impl_->is_open = false;
  MV_LOG_INFO("HAL.Camera.OpenCV", "closed");
}

bool OpenCvCamera::Grab(cv::Mat& frame) {
  if (!impl_->is_open) {
    MV_LOG_WARN("HAL.Camera.OpenCV", "Grab called on closed camera");
    return false;
  }

  // cv::VideoCapture::read() 是阻塞调用，超时由驱动决定
  if (!impl_->cap.read(frame)) {
    // 视频文件播放完毕或设备断开
    MV_LOG_WARN("HAL.Camera.OpenCV", "cap.read() returned false (EOF or device error)");
    impl_->is_open = false;
    return false;
  }

  return !frame.empty();
}

bool OpenCvCamera::IsOpen() const {
  return impl_->is_open;
}

}  // namespace mv::hal
