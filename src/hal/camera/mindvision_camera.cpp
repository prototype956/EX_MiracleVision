/**
 * @file mindvision_camera.cpp
 * @brief MindVision 工业相机 Pimpl 实现
 *
 * 此文件是唯一需要 include CameraApi.h 的地方。
 * 通过 CMake 的 target_compile_definitions 在有 SDK 时定义 MV_HAS_MVSDK，
 * 无 SDK 时（CI / 开发机）编译为"始终返回失败"的桩实现，
 * 保证整个项目在无硬件环境下仍可编译通过。
 *
 * 【帧获取流程（有 SDK 时）】
 *   CameraGetImageBuffer()  → 阻塞等待 DMA 帧完成
 *   CameraImageProcess()    → SDK 内部去马赛克 / 白平衡
 *   cvCreateImageHeader()   → 包装为 IplImage（SDK 遗留接口）
 *   cv::cvarrToMat()        → 转为 cv::Mat（深拷贝，释放 SDK buffer）
 *   CameraReleaseImageBuffer() → 归还 DMA buffer，允许 SDK 继续采集
 *
 * 【为什么深拷贝而不是零拷贝？】
 *   SDK 的 DMA buffer 数量有限（通常 2-3 个），
 *   如果上层处理太慢不归还，SDK 会丢帧并报 CAMERA_STATUS_BUFFER_FAILED。
 *   深拷贝到 cv::Mat 后立即归还 buffer，
 *   把"SDK buffer 占用时间"压缩到最短（几微秒），代价是一次内存拷贝。
 *   对于 1280x800 BGR 图像约 3MB，现代 CPU 拷贝耗时 ~0.5ms，可接受。
 */

#include "mindvision_camera.hpp"

#include <opencv2/imgproc.hpp>

#ifdef MV_HAS_MVSDK
#include <CameraApi.h>
#endif

#include "../../core/logger.hpp"

namespace mv::hal {

// ============================================================================
// Impl 定义（所有 SDK 类型都在这里，不污染头文件）
// ============================================================================

struct MindVisionCamera::Impl {
  bool is_open{false};

  // 图像参数
  int width{1280};
  int height{800};
  int exposure_us{5000};
  int channel{3};

#ifdef MV_HAS_MVSDK
  int h_camera{0};
  unsigned char* rgb_buffer{nullptr};

  tSdkCameraDevInfo  dev_info{};
  tSdkCameraCapbility capability{};
  tSdkFrameHead       frame_head{};
  tSdkImageResolution resolution{};
  BYTE*               raw_buffer{nullptr};
  IplImage*           ipl_image{nullptr};
#endif
};

// ============================================================================
// 构造 / 析构 / 移动
// ============================================================================

MindVisionCamera::MindVisionCamera() : impl_(std::make_unique<Impl>()) {}

// 析构必须在 .cpp 定义，因为此时 Impl 是完整类型
MindVisionCamera::~MindVisionCamera() {
  Close();
}

MindVisionCamera::MindVisionCamera(MindVisionCamera&&) noexcept = default;
MindVisionCamera& MindVisionCamera::operator=(MindVisionCamera&&) noexcept = default;

// ============================================================================
// ICamera 接口实现
// ============================================================================

bool MindVisionCamera::Open(const YAML::Node& config) {
#ifndef MV_HAS_MVSDK
  // 无 SDK 时记录日志并返回 false，而不是编译错误
  // 这让 CI 和开发机在没有 MindVision 硬件时仍能跑通流水线
  (void)config;  // suppress unused parameter warning in stub mode
  MV_LOG_WARN("HAL.Camera.MV", "MV_HAS_MVSDK not defined, running in stub mode");
  return false;
#else
  if (impl_->is_open) {
    return true;  // 幂等：已打开则直接返回
  }

  // 从 YAML 读取参数，提供合理默认值（防止配置文件缺字段时崩溃）
  impl_->exposure_us = config["exposure_us"].as<int>(5000);
  impl_->width       = config["resolution"]["width"].as<int>(1280);
  impl_->height      = config["resolution"]["height"].as<int>(800);
  impl_->channel     = config["channel"].as<int>(3);

  CameraSdkInit(1);

  int camera_count = 1;
  int status = CameraEnumerateDevice(&impl_->dev_info, &camera_count);
  if (camera_count == 0) {
    MV_LOG_ERROR("MindVisionCamera: no device found (CameraEnumerateDevice returned {})", status);
    return false;
  }

  status = CameraInit(&impl_->dev_info, -1, -1, &impl_->h_camera);
  if (status != CAMERA_STATUS_SUCCESS) {
    MV_LOG_ERROR("MindVisionCamera: CameraInit failed, status={}", status);
    return false;
  }

  CameraGetCapability(impl_->h_camera, &impl_->capability);

  // 分配 RGB 缓冲区（大小取相机支持的最大分辨率，避免反复 realloc）
  const std::size_t buf_size =
      static_cast<std::size_t>(impl_->capability.sResolutionRange.iHeightMax) *
      static_cast<std::size_t>(impl_->capability.sResolutionRange.iWidthMax) * 3;
  impl_->rgb_buffer = static_cast<unsigned char*>(malloc(buf_size));  // NOLINT(cppcoreguidelines-no-malloc)
  if (impl_->rgb_buffer == nullptr) {
    MV_LOG_ERROR("MindVisionCamera: failed to allocate RGB buffer ({} bytes)", buf_size);
    CameraUnInit(impl_->h_camera);
    return false;
  }

  // 设置分辨率
  CameraGetImageResolution(impl_->h_camera, &impl_->resolution);
  impl_->resolution.iIndex     = 0xFF;  // 0xFF = 自定义分辨率
  impl_->resolution.iWidthFOV  = impl_->width;
  impl_->resolution.iHeightFOV = impl_->height;
  CameraSetImageResolution(impl_->h_camera, &impl_->resolution);

  // 设置曝光（关闭 AE，手动控制）
  CameraSetAeState(impl_->h_camera, FALSE);
  CameraSetExposureTime(impl_->h_camera, static_cast<double>(impl_->exposure_us));

  CameraPlay(impl_->h_camera);

  impl_->is_open = true;
  MV_LOG_INFO("MindVisionCamera: opened ({}x{}, exposure={}us)",
              impl_->width, impl_->height, impl_->exposure_us);
  return true;
#endif
}

void MindVisionCamera::Close() {
#ifdef MV_HAS_MVSDK
  if (!impl_->is_open) {
    return;  // 幂等
  }

  CameraUnInit(impl_->h_camera);

  if (impl_->rgb_buffer != nullptr) {
    free(impl_->rgb_buffer);  // NOLINT(cppcoreguidelines-no-malloc)
    impl_->rgb_buffer = nullptr;
  }
  if (impl_->ipl_image != nullptr) {
    cvReleaseImageHeader(&impl_->ipl_image);
    impl_->ipl_image = nullptr;
  }

  impl_->is_open = false;
  MV_LOG_INFO("MindVisionCamera: closed");
#endif
}

bool MindVisionCamera::Grab(cv::Mat& frame) {
#ifndef MV_HAS_MVSDK
  (void)frame;
  return false;
#else
  if (!impl_->is_open) {
    MV_LOG_WARN("MindVisionCamera::Grab called on closed camera");
    return false;
  }

  // 超时设为 1000ms：比正常帧周期长，但不会让调用方卡太久
  constexpr int kTimeoutMs = 1000;
  const int status = CameraGetImageBuffer(
      impl_->h_camera, &impl_->frame_head, &impl_->raw_buffer, kTimeoutMs);

  if (status != CAMERA_STATUS_SUCCESS) {
    MV_LOG_WARN("MindVisionCamera: CameraGetImageBuffer timeout/error, status={}", status);
    return false;
  }

  // SDK 做去马赛克 / 颜色空间转换，结果写入 rgb_buffer
  CameraImageProcess(impl_->h_camera, impl_->raw_buffer, impl_->rgb_buffer, &impl_->frame_head);

  // 用 IplImage 头包装 rgb_buffer（零拷贝）
  if (impl_->ipl_image != nullptr) {
    cvReleaseImageHeader(&impl_->ipl_image);
  }
  impl_->ipl_image = cvCreateImageHeader(
      cvSize(impl_->frame_head.iWidth, impl_->frame_head.iHeight),
      IPL_DEPTH_8U, impl_->channel);
  cvSetData(impl_->ipl_image, impl_->rgb_buffer,
            impl_->frame_head.iWidth * impl_->channel);

  // 深拷贝到 cv::Mat，之后立即归还 DMA buffer
  // 深拷贝原因见文件头注释：尽快释放 SDK buffer 防止丢帧
  frame = cv::cvarrToMat(impl_->ipl_image, true);

  CameraReleaseImageBuffer(impl_->h_camera, impl_->raw_buffer);
  impl_->raw_buffer = nullptr;

  return true;
#endif
}

bool MindVisionCamera::IsOpen() const {
  return impl_->is_open;
}

}  // namespace mv::hal
