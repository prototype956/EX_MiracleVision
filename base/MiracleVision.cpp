/**
 * @file MiracleVision.cpp
 * @author dgsyrc (yrcminecraft@foxmail.com)
 * @brief Main Function // 主函数
 * @date 2024-02-20
 * @copyright Copyright (c) 2024 dgsyrc
 */
#include "MiracleVision.hpp"

// 编译控制宏定义（通过注释/取消注释来切换功能）

// 视频调试模式：使用本地视频文件作为输入，而不是相机
//#define VIDEO_DEBUG
// 视频录制功能
// #define RECORD

// 自动开火 (Auto Fire)
// #define MANUAL_FIRE // 手动开火（宏名似乎与注释不符，实际功能需看代码中如何使用）

// 调试模式
#define PARM_EDIT  // 参数编辑模式（用于显示参考线等）

// 发布/运行模式 (Release Mode)
// #define RELEASE

int main() {
  // 打印编译信息
  fmt::print("[{}] MiracleVision built on g++ version: {}\n", idntifier, __VERSION__);
  // 打印配置文件路径
  fmt::print("[{}] MiracleVision config file path: {}\n", idntifier, CONFIG_FILE_PATH);

  cv::Mat src_img, roi_img;  // 原始图像和 ROI 图像 (ROI 未在主循环中直接使用)

// --------------------------- 视频捕获初始化 ---------------------------
#ifndef VIDEO_DEBUG
  // 非视频调试模式：使用 MindVision 工业相机
  mindvision::VideoCapture* mv_capture_ = new mindvision::VideoCapture(
      mindvision::CameraParam(0, mindvision::RESOLUTION_1280_X_1024, mindvision::EXPOSURE_2500));
  // 备用：OpenCV 默认相机捕获（可能用于 USB 摄像头）
  cv::VideoCapture cap_ = cv::VideoCapture(0);
#else
  // 视频调试模式：使用本地视频文件
  cv::VideoCapture cap_(fmt::format("{}{}", SOURCE_PATH, "/video/BD-002.mp4"));
#endif
  fmt::print("Capture init pass.\n");  // 打印捕获初始化成功信息

  // --------------------------- 模块初始化 ---------------------------
  // 初始化 Plotter
  tools::Plotter plotter;

  // 串口通信模块
  uart::SerialPort serial_ =
      uart::SerialPort(fmt::format("{}{}", CONFIG_FILE_PATH, "/serial/uart_serial_config.xml"));

  // 基础装甲板检测模块
  basic_armor::Detector basic_armor_ =
      basic_armor::Detector(fmt::format("{}{}", CONFIG_FILE_PATH, "/armor/basic_armor_config.xml"));

  // 基础能量机关检测模块
  basic_buff::Detector basic_buff_ =
      basic_buff::Detector(fmt::format("{}{}", CONFIG_FILE_PATH, "/buff/basic_buff_config.xml"));

  // PnP (Perspective-n-Point) 位姿解算模块
  basic_pnp::PnP pnp_ = basic_pnp::PnP(
      fmt::format("{}{}", CONFIG_FILE_PATH, "/camera/mv_camera_config_407.xml"),    // 相机参数
      fmt::format("{}{}", CONFIG_FILE_PATH, "/angle_solve/basic_pnp_config.xml"));  // PnP 参数

  // DNN (深度神经网络) 模块初始化
  // onnx_inferring::model model_ = onnx_inferring::model(fmt::format("{}{}", SOURCE_PATH,
  // "/module/ml/mnist-8.onnx")); // 示例代码，未启用
  Ort::Env env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "PoseEstimate");  // ONNX Runtime 环境
  Ort::SessionOptions session_options;                                       // ONNX Session 选项
  session_options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_ALL);  // 设置图优化级别

  // DNN 装甲板检测模型 (如 YOLOv8)
  DNN_armor::DNN_Model dnn_model = DNN_armor::DNN_Model(
      fmt::format("{}{}", SOURCE_PATH, "/module/armor/yolov8.onnx"), env, session_options);
  // DNN 装甲板检测器
  DNN_armor::DNN_Dectect dnn_armor =
      DNN_armor::DNN_Dectect(fmt::format("{}{}", CONFIG_FILE_PATH, "/armor/DNN_armor_config.xml"));

  // 角度解算器（用于装甲板）
  angle_solve::solve solution;
  solution.set_config(fmt::format("{}{}", CONFIG_FILE_PATH, "/angle_solve/angle_solve_config.xml"));

  // 角度解算器（用于能量机关）
  angle_solve::solve buff_solution;
  buff_solution.set_config(
      fmt::format("{}{}", CONFIG_FILE_PATH, "/angle_solve/buff_angle_solve_config.xml"));

  // 其他工具和辅助模块
  basic_roi::RoI save_roi;                   // 保存的 ROI (未在主循环中直接使用)
  fps::FPS global_fps_;                      // 全局 FPS 计算
  basic_roi::RoI roi_;                       // ROI (未在主循环中直接使用)
  std::time_t st_time = std::time(nullptr);  // 开始时间（用于 FPS 估算）

  // --------------------------- 主循环 ---------------------------
  while (true) {
    // 获取当前时间点
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    rec_time++;  // 已处理帧数 +1
    fmt::print("[time] {}\n", rec_time);

#ifdef RELEASE
    // 发布模式下的看门狗/重启机制（如果运行帧数过多）
    if (rec_time > 5000) {
      // int i [[maybe_unused]] = std::system("reboot"); // 相机断开时重启系统
    }
#endif
    // global_fps_.getTick(); // FPS 计时开始
    // new_buff::new_buff_fps.getTick(); // 新 Buff FPS 计时开始

    // --------------------------- 图像捕获 ---------------------------
#ifndef VIDEO_DEBUG
    // MindVision 相机
    if (mv_capture_->isindustryimgInput()) {
      src_img = mv_capture_->image();  // 使用 MindVision SDK 获取图像
    } else {
      cap_.read(src_img);  // 使用 OpenCV 捕获备用摄像头图像
    }
    cv::Size frameSize = {src_img.cols, src_img.rows};  // 图像尺寸
                                                        // cap_fps = cap_.get(cv::CAP_PROP_FPS);
                                                        // cap_fps = 30; // 默认/固定 FPS
#else
    // 视频调试模式
    cap_.read(src_img);  // 从视频文件读取下一帧

    cv::waitKey(30);  // 模拟等待时间
    cv::Size frameSize(cap_.get(cv::CAP_PROP_FRAME_WIDTH),
                       cap_.get(cv::CAP_PROP_FRAME_HEIGHT));  // 视频尺寸
    cap_fps = cap_.get(cv::CAP_PROP_FPS);                     // 获取视频文件帧率
#endif

    if (!src_img.empty())  // 检查图像是否成功捕获
    {
#ifdef RECORD
      // --------------------------- 视频录制逻辑 ---------------------------
      if (!test_fps && rec_cnt < 200) {
        rec_cnt++;  // 累积前 200 帧进行 FPS 估算
      } else {
        if (!test_fps) {
          // 估算 FPS（基于前 200 帧耗时）
          std::time_t ed_time = std::time(nullptr);
          cap_fps = (int)(200.0 / (ed_time - st_time));
          test_fps = true;  // FPS 估算完成
          rec_cnt = 0;
        }
      }
      if (rec_cnt == 0 && test_fps) {
        // FPS 估算完成后，开始初始化视频录制器
        std::stringstream tmp;
        tmp << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
        std::string str_time = tmp.str();  // 格式化当前时间作为文件名
        std::string video_name = fmt::format("{}/video/record/{}.mp4", SOURCE_PATH, str_time);
        cout << frameSize.width << ' ' << frameSize.height << ' ' << cap_fps << '\n';
        tools::Tools::recordInit(video_name, writer, frameSize, cap_fps);  // 初始化 VideoWriter
        writer.write(src_img);                                             // 写入第一帧
        rec_cnt++;
      } else {
        // 持续录制
        cout << "[REC] frame:" << rec_cnt << " fps:" << cap_fps << "\n";
        if (test_fps) {
          if (rec_cnt > cap_fps * 60 * 1)  // 录制 1 分钟 (60秒 * 1)
          {
            writer.write(src_img);
            writer.release();  // 释放 VideoWriter
            rec_cnt = 0;       // 重置计数器以准备下一次录制
          } else {
            writer.write(src_img);  // 写入当前帧
            rec_cnt++;
          }
        }
      }

#endif
      // --------------------------- 调试显示 ---------------------------
#ifndef RELEASE
      cv::imshow("[origin]", src_img);  // 显示原始图像
      cv::waitKey(30);
#endif
#ifdef PARM_EDIT
      // 参数编辑模式：绘制参考线
      // cv::line(src_img, {1024, 0}, {1024, 1024}, cv::Scalar(255, 0, 255), 2);
      cv::imshow("[armor_parm_edit]", src_img);
      cv::waitKey(30);
#endif

      // --------------------------- 主逻辑处理 ---------------------------
      fire = false;                        // 默认不发射
      serial_.updateReceiveInformation();  // 更新接收到的串口数据（如模式、敌方颜色）

      switch (uart::AUTO_AIM)  // 根据接收到的模式切换功能
      {
        // 基础自动瞄准模式
        case uart::AUTO_AIM: {
          fmt::print("[{}] AUTO_AIM\n", idntifier);
          // dnn_armor.Detect(src_img, dnn_model); // 深度学习装甲板检测（如果启用）
          // 运行基础装甲板检测
          if (basic_armor_.runBasicArmor(src_img, serial_.returnReceive()) /*basic_armor_.sentryMode(src_img, serial_.returnReceive())*/)
        {
            // 检测成功，进行角度解算
            solution.angleSolve(basic_armor_.returnFinalArmorRotatedRect(0), src_img.size().height,
                                src_img.size().width, serial_);
          }
          if (basic_armor_.returnArmorNum())  // 如果检测到装甲板
          {
            fire = true;  // 设置开火标志
          }
          // 更新并发送串口数据
          serial_.updataWriteData(basic_armor_.returnArmorNum(), fire,
                                  solution.returnYawAngle(),          // 偏航角
                                  solution.returnPitchAngle(),        // 俯仰角
                                  basic_armor_.returnArmorCenter(0),  // 目标中心点
                                  0);                                 // 预留字段
          // 发送数据到 PlotJuggler
          nlohmann::json data;
          // data["timestamp"] = t;
          auto now = std::chrono::steady_clock::now();
          double timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();
          data["timestamp"] = timestamp;
          data["yaw"] = solution.returnYawAngle();
          data["pitch"] = solution.returnPitchAngle();
          // data["roll"] = solution.returnRollAngle();
          plotter.plot(data);
          break;
        }

        // 能量机关模式
        case uart::ENERGY_BUFF:
          if (basic_buff_.runTask(src_img, serial_.returnReceive()))  // 运行能量机关检测任务
          {
            fmt::print("[buff] PASS\n");
            // 检测成功，进行角度解算
            buff_solution.angleSolve(basic_buff_.returnObjectRect(), src_img.size().height,
                                     src_img.size().width, serial_);
            // 更新并发送串口数据
            serial_.updataWriteData(1, basic_buff_.isfire(),  // 目标数 (1), 是否开火
                                    buff_solution.returnYawAngle(),
                                    buff_solution.returnPitchAngle(),
                                    basic_buff_.returnObjectforUart(), 0);
          } else {
            // 未检测到目标，发送空数据
            serial_.updataWriteData(0, 0, 0, 0, {0, 0}, 0);
          }
          break;

        // 哨兵自主模式 [未解决/待完善]
        case uart::SENTINEL_AUTONOMOUS_MODE:
          break;

        // 相机标定模式
        case uart::CAMERA_CALIBRATION:
          // cam::create_images(src_img); // 采集标定图像（手动模式）
          // cam::calibrate();           // 运行标定
          // cam::auto_create_images(src_img); // 采集标定图像（自动模式）
          cam::assess(src_img);  // 评估标定效果（如重投影误差）
          // cam::CalibrationEvaluate(); // 评估标定（未启用）
          break;

        default:
          break;
      }
    } else {
#ifdef VIDEO_DEBUG
      // 视频调试模式下，如果读到空帧（视频结束），可以循环播放
      // cap_.open(fmt::format("{}{}", SOURCE_PATH, "/video/1080.mp4"));
#endif
    }

    // --------------------------- 循环清理与看门狗 ---------------------------

#ifndef VIDEO_DEBUG
    mv_capture_->cameraReleasebuff();  // MindVision 相机释放缓存
#endif
    // 释放装甲板检测模块的内存或更新配置
    basic_armor_.freeMemory(
        fmt::format("{}{}", CONFIG_FILE_PATH, "/armor/basic_armor_config_new.xml"));
    // 看门狗：如果 FPS 异常高（可能意味着相机断开连接）
    /*global_fps_.calculateFPSGlobal();*/  // 计算全局 FPS
    if (global_fps_.returnFps() > 500)     // 阈值 500 FPS，通常代表相机未能提供图像
    {
#ifndef VIDEO_DEBUG
      mv_capture_->~VideoCapture();  // 释放旧的相机对象
#endif
      static int counter_for_dev{100};  // 尝试设备重启计数器
      static int counter_for_new{30};   // 尝试创建新对象计数器
      while (!utils::resetMVCamera())   // 尝试重置 MindVision 相机
      {
        if (!--counter_for_dev)  // 如果尝试次数耗尽
        {
          // int i [[maybe_unused]] = std::system("reboot"); // 重启整个系统
        }
        usleep(100);
      }
      usleep(100);
#ifndef VIDEO_DEBUG
      // 重启成功，重新创建 MindVision 相机对象
      mv_capture_ = new mindvision::VideoCapture(mindvision::CameraParam(
          0, mindvision::RESOLUTION_1280_X_800, mindvision::EXPOSURE_40000));
#endif
      if (!--counter_for_new)  // 如果尝试次数耗尽
      {
        // int i [[maybe_unused]] = std::system("reboot"); // 重启整个系统
      }
    }
  }
  return 0;
}
