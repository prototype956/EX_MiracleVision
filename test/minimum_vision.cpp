/**
 * @file minimum_vision.cpp
 * @brief 最小视觉系统 - 视频播放 + 装甲板识别功能
 * @date 2026-01-23
 */

#include <opencv2/opencv.hpp>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <chrono>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <mutex>


#include "utils/img_tools.hpp"
#include "utils/plotter.hpp"

// 装甲板识别相关头文件
#include "module/armor/basic_armor.hpp"
#include "devices/serial/uart_serial.hpp"

// 预测器相关头文件
#include "module/predictor/tracker.hpp"
#include "module/predictor/solver.hpp"
#include "module/predictor/armor.hpp"

// Foxglove 发布器
#include "module/foxglove_publisher/foxglove_publisher.hpp"

namespace fs = std::filesystem;

// 视频调试模式：使用本地视频文件作为输入
#define VIDEO_DEBUG

// 鼠标回调函数 - 显示像素信息
void onMouse(int event, int x, int y, int flags, void* userdata)
{
    cv::Mat* img = static_cast<cv::Mat*>(userdata);
    if (img && x >= 0 && x < img->cols && y >= 0 && y < img->rows) {
        cv::Vec3b pixel = img->at<cv::Vec3b>(y, x);
        // 直接在控制台输出像素信息（不使用 displayStatusBar）
        if (event == cv::EVENT_MOUSEMOVE) {
            // 只在鼠标移动时更新，避免过多输出
            static int last_x = -1, last_y = -1;
            if (std::abs(x - last_x) > 10 || std::abs(y - last_y) > 10) {
                fmt::print("\r(x={}, y={}) : R:{} G:{} B:{}    ", 
                           x, y, pixel[2], pixel[1], pixel[0]);
                std::cout << std::flush;
                last_x = x;
                last_y = y;
            }
        }
    }
}

int main(int argc, char **argv)
{
    // 打印编译信息
    fmt::print("[MinimumVision] Built on g++ version: {}\n", __VERSION__);

    // 视频文件夹路径（写死）
    std::string video_folder = "/home/prototype152/桌面/EX_MiracleVision/video";
    
    // 如果提供了命令行参数，则使用命令行参数
    if (argc > 1)
    {
        video_folder = argv[1];
    }
    
    fmt::print("[Info] 使用视频文件夹: {}\n", video_folder);

    // 检查文件夹是否存在
    if (!fs::exists(video_folder) || !fs::is_directory(video_folder))
    {
        fmt::print("[Error] 文件夹不存在: {}\n", video_folder);
        return -1;
    }

    // 扫描视频文件
    std::vector<std::string> video_files;
    std::vector<std::string> extensions = {".mp4", ".avi", ".mov", ".mkv", ".flv"};

    for (const auto &entry : fs::directory_iterator(video_folder))
    {
        if (entry.is_regular_file())
        {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
            {
                video_files.push_back(entry.path().string());
            }
        }
    }

    if (video_files.empty())
    {
        fmt::print("[Error] 文件夹中没有找到视频文件\n");
        return -1;
    }

    std::sort(video_files.begin(), video_files.end());
    fmt::print("[Info] 找到 {} 个视频文件\n", video_files.size());

    // 控制说明
    fmt::print("\n=== 控制说明 ===\n");
    fmt::print("  空格 - 暂停/继续\n");
    fmt::print("  n - 下一个视频\n");
    fmt::print("  q/ESC - 退出\n");
    fmt::print("================\n\n");

    // --------------------------- 模块初始化 ---------------------------
    // 初始化绘图器
    tools::Plotter plotter;

    // 串口通信模块（用于接收模式和颜色信息）
    uart::SerialPort serial_ = uart::SerialPort(
        fmt::format("{}/serial/uart_serial_config.xml", CONFIG_FILE_PATH));

    // 基础装甲板检测模块
    basic_armor::Detector basic_armor_ = basic_armor::Detector(
        fmt::format("{}/armor/basic_armor_config.xml", CONFIG_FILE_PATH));

    fmt::print("[Info] 装甲板检测器初始化完成\n");

    // --------------------------- 预测器模块初始化 ---------------------------
    // 预测器配置文件路径（Solver 和 Tracker 共用一个配置文件）
    std::string predictor_config = fmt::format("{}/predictor/predictor.yaml", CONFIG_FILE_PATH);
    
    // 角度解算器（Solver）
    predictor::Solver solver(predictor_config);
    fmt::print("[Info] Solver 初始化完成\n");

    // 追踪器（Tracker）
    predictor::Tracker tracker(predictor_config, solver);
    fmt::print("[Info] Tracker 初始化完成\n");

    // 初始化 Foxglove 发布器
    // 注意：目前 config_file 参数只是占位符，配置是硬编码的
    FoxglovePublisher foxglove_pub(fmt::format("{}/foxglove/config.xml", CONFIG_FILE_PATH));
    foxglove_pub.start();
    fmt::print("[Info] Foxglove Publisher 初始化完成\n");
    
    // 全局模拟数据互斥锁
    std::mutex mock_data_mutex;
    // 默认模拟数据
    uart::Receive_Data mock_data;
    mock_data.my_color = uart::Color::BLUE;
    mock_data.now_run_mode = uart::RunMode::AUTO_AIM;
    mock_data.my_robot_id = uart::RobotID::INFANTRY;
    mock_data.bullet_velocity = 28.0f;
    mock_data.yaw = 0.0f;
    mock_data.pitch = 0.0f;
    mock_data.yaw_velocity = 0.0f;
    mock_data.pitch_velocity = 0.0f;
    
    // 初始化参数
    nlohmann::json params;
    params["my_color"] = "BLUE"; // 默认为蓝队
    params["run_mode"] = "AUTO_AIM";
    params["bullet_speed"] = 28.0;
    params["gimbal_yaw"] = 0.0;
    params["gimbal_pitch"] = 0.0;
    
    foxglove_pub.updateParameters(params);
    
    foxglove_pub.setParameterCallback([&](const std::string& name, const nlohmann::json& val) {
        // 注意: val 现在是空对象,需要主动查询
        std::lock_guard<std::mutex> lock(mock_data_mutex);
        try {
            // 主动查询参数值
            auto value = foxglove_pub.getParameterValue(name);
            
            if (value.is_null()) {
                fmt::print("[Param] Parameter '{}' not found or has no value\n", name);
                return;
            }
            
            if (name == "bullet_speed" && value.is_number()) {
                mock_data.bullet_velocity = value.get<double>();
            }
            else if (name == "gimbal_yaw" && value.is_number()) {
                mock_data.yaw = value.get<double>();
            }
            else if (name == "gimbal_pitch" && value.is_number()) {
                mock_data.pitch = value.get<double>();
            }
            else if (name == "my_color" && value.is_string()) {
                std::string c = value.get<std::string>();
                if (c == "BLUE") mock_data.my_color = uart::Color::BLUE;
                else if (c == "RED") mock_data.my_color = uart::Color::RED;
            }
            else if (name == "run_mode" && value.is_string()) {
                std::string m = value.get<std::string>();
                if (m == "AUTO_AIM") mock_data.now_run_mode = uart::RunMode::AUTO_AIM;
                else if (m == "ENERGY_BUFF") mock_data.now_run_mode = uart::RunMode::ENERGY_BUFF;
                else if (m == "DEFAULT_MODE") mock_data.now_run_mode = uart::RunMode::DEFAULT_MODE;
            }
            fmt::print("[Param] Updated {}: {}\n", name, value.dump());
        } catch (const std::exception& e) {
            fmt::print("[Error] Failed to update param {}: {}\n", name, e.what());
        }
    });

    cv::Mat src_img;
    cv::Mat display_img;
    size_t current_video_idx = 0;
    int frame_count = 0;
    
    // 创建窗口并设置鼠标回调
    cv::namedWindow("Minimum Vision", cv::WINDOW_NORMAL | cv::WINDOW_GUI_EXPANDED);
    cv::setMouseCallback("Minimum Vision", onMouse, &display_img);
    // 主循环
    while (current_video_idx < video_files.size())
    {
        // 打开当前视频
        std::string current_video = video_files[current_video_idx];
        fmt::print("[Playing] ({}/{}) {}\n",
                   current_video_idx + 1,
                   video_files.size(),
                   current_video);

        cv::VideoCapture cap_(current_video);

        if (!cap_.isOpened())
        {
            fmt::print("[Error] 无法打开视频: {}\n", current_video);
            current_video_idx++;
            continue;
        }

        // 获取视频信息
        double cap_fps = cap_.get(cv::CAP_PROP_FPS);
        int total_frames = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
        cv::Size frameSize(cap_.get(cv::CAP_PROP_FRAME_WIDTH),
                           cap_.get(cv::CAP_PROP_FRAME_HEIGHT));

        fmt::print("[Info] FPS: {:.2f}, 分辨率: {}x{}, 总帧数: {}\n",
                   cap_fps, frameSize.width, frameSize.height, total_frames);

        int delay = static_cast<int>(1000.0 / cap_fps);
        bool video_finished = false;
        
        // 重置帧计数
        frame_count = 0;
        auto last_t = std::chrono::steady_clock::now();

        // 视频播放循环
        while (!video_finished)
        {
            auto t = std::chrono::steady_clock::now();
            frame_count++;

            // 读取视频帧
            cv::Mat frame;
            cap_.read(frame);

            if (frame.empty())
            {
                fmt::print("[Info] 视频播放完毕\n");
                video_finished = true;
                break;
            }
            
            // 克隆一份用于显示和绘制（保持原始帧不被修改）
            src_img = frame.clone();

            // --------------------------- 装甲板识别 ---------------------------
            // serial_.updateReceiveInformation(); // 更新串口接收信息
            
            // === [Debug] 手动设置模拟下位机数据 ===
            // 在视频调试且无串口连接时，必须手动初始化这些数据，否则云台坐标系会错误
            uart::Receive_Data current_mock_data;
            {
                std::lock_guard<std::mutex> lock(mock_data_mutex);
                current_mock_data = mock_data;
            }
            
            // 设置初始云台姿态 (单位: 度)
            // 提示: 如果视频中的云台有旋转，这里设置为固定值会导致解算误差
            /* mock_data used to be here */

            // 关键: 将模拟的欧拉角转换为四元数，并传递给 Solver 初始化云台坐标系
            {
                double yaw_rad = current_mock_data.yaw * (M_PI / 180.0);
                double pitch_rad = current_mock_data.pitch * (M_PI / 180.0);
                double roll_rad = 0.0; // 假设无横滚
                
                // 欧拉角转四元数 (ZYX顺序)
                Eigen::Quaterniond q_init = 
                    Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
                    Eigen::AngleAxisd(pitch_rad, Eigen::Vector3d::UnitY()) *
                    Eigen::AngleAxisd(roll_rad, Eigen::Vector3d::UnitX());
                
                solver.set_R_gimbal2world(q_init);
                
                // 调试: 定期打印云台姿态
                if (frame_count % 30 == 0) {
                    fmt::print("[Frame {}] Gimbal: Yaw={:.2f}° Pitch={:.2f}°\n", 
                               frame_count, current_mock_data.yaw, current_mock_data.pitch);
                }
            }

            int armor_num = 0;
            
            // 用于存储预测器所需的装甲板列表
            std::list<predictor::Armor> predictor_armors;

            // 运行装甲板检测（传入模拟数据）
            bool detected = basic_armor_.runBasicArmor(src_img, current_mock_data);
            
            if (detected)
            {
                armor_num = basic_armor_.returnArmorNum();
                fmt::print("[Armor] 成功识别到 {} 个装甲板\n", armor_num);

                // 将检测到的装甲板转换为预测器格式
                for (int i = 0; i < armor_num; i++)
                {
                    // 获取完整的装甲板数据（包含灯条信息）
                    auto armor_data = basic_armor_.returnFinalArmor(i);
                    auto armor_rect = armor_data.armor_rect;
                    
                    // 从左右灯条提取角点（类似 sp_vision_25 的方法）
                    cv::Point2f left_corners[4], right_corners[4];
                    armor_data.left_light.points(left_corners);
                    armor_data.right_light.points(right_corners);
                    
                    // 对灯条角点按 Y 坐标排序，获取上下两个点
                    std::vector<cv::Point2f> left_pts(left_corners, left_corners + 4);
                    std::vector<cv::Point2f> right_pts(right_corners, right_corners + 4);
                    
                    std::sort(left_pts.begin(), left_pts.end(),
                             [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; });
                    std::sort(right_pts.begin(), right_pts.end(),
                             [](const cv::Point2f& a, const cv::Point2f& b) { return a.y < b.y; });
                    
                    // 计算灯条的上下中点
                    cv::Point2f left_top = (left_pts[0] + left_pts[1]) / 2;
                    cv::Point2f left_bottom = (left_pts[2] + left_pts[3]) / 2;
                    cv::Point2f right_top = (right_pts[0] + right_pts[1]) / 2;
                    cv::Point2f right_bottom = (right_pts[2] + right_pts[3]) / 2;
                    
                    // 按 sp_vision_25 的顺序构建角点：[左上, 右上, 右下, 左下]
                    // 这对应 3D 模型的 [右上, 左上, 左下, 右下]（图像左=3D右）
                    std::vector<cv::Point2f> armor_keypoints;
                    armor_keypoints.push_back(left_top);       // 左上 (对应 3D 的右上 +w/2)
                    armor_keypoints.push_back(right_top);      // 右上 (对应 3D 的左上 -w/2)
                    armor_keypoints.push_back(right_bottom);   // 右下 (对应 3D 的左下 -w/2)
                    armor_keypoints.push_back(left_bottom);    // 左下 (对应 3D 的右下 +w/2)
                    
                    // 使用 predictor::Armor 的构造函数
                    // Armor(int class_id, float confidence, const cv::Rect & box, std::vector<cv::Point2f> armor_keypoints)
                    predictor::Armor pred_armor(
                        0,  // class_id (暂时设为0)
                        0.9f,  // confidence
                        armor_rect.boundingRect(),  // bounding box
                        armor_keypoints  // 装甲板角点
                    );
                    
                    // 设置装甲板类型
                    pred_armor.type = (armor_rect.size.width / armor_rect.size.height > 3.0) 
                                        ? predictor::ArmorType::big 
                                        : predictor::ArmorType::small;
                    pred_armor.color = predictor::Color::blue; // 根据配置文件中的 enemy_color
                    pred_armor.name = predictor::ArmorName::not_armor;
                    
                    predictor_armors.push_back(pred_armor);
                    
                    // 绘制原始检测结果（蓝色矩形框）
                    cv::rectangle(src_img, armor_rect.boundingRect(), cv::Scalar(255, 0, 0), 2);
                    cv::circle(src_img, armor_rect.center, 3, cv::Scalar(255, 0, 0), -1);
                    
                    std::string label = fmt::format("D{}", i + 1);  // D for Detection
                    tools::draw_text(src_img, label,
                                   cv::Point(armor_rect.center.x + 10, armor_rect.center.y),
                                   cv::Scalar(255, 0, 0), 0.5, 2);
                }
            }
            else
            {
                // 检测失败时的调试信息
                fmt::print("[Armor] 未能成功配对装甲板（可能找到灯条但配对失败）\n");
            }
            
            // --------------------------- 角度解算 ---------------------------
            // 对所有检测到的装甲板进行角度解算（PnP）
            int armor_idx = 0;
            for (auto& armor : predictor_armors)
            {
                solver.solve(armor);  // 计算装甲板的3D位置和姿态
                
                // 输出每个装甲板的yaw角度（optimize_yaw后的值）
                fmt::print("[Armor {}] PnP yaw after optimize: {:.4f}rad ({:.1f}°) | xyz: ({:.2f}, {:.2f}, {:.2f})\n",
                         armor_idx, armor.ypr_in_world[0], armor.ypr_in_world[0] * 57.3,
                         armor.xyz_in_world[0], armor.xyz_in_world[1], armor.xyz_in_world[2]);
                armor_idx++;
            }
            
            // 保存装甲板数量（tracker.track() 会修改 predictor_armors）
            size_t armor_input_count = predictor_armors.size();
            
            // --------------------------- 预测器追踪 ---------------------------
            auto targets = tracker.track(predictor_armors, t, true);
            
            // 输出追踪器状态（调试）
            fmt::print("[Tracker] State: {} | Targets: {} | Armors input: {} | Armors after: {}\n", 
                     tracker.state(), targets.size(), armor_input_count, predictor_armors.size());
            
            // 绘制追踪器状态
            tools::draw_text(src_img, 
                           fmt::format("[{}]", tracker.state()), 
                           {10, 180}, 
                           {255, 255, 255}, 0.8, 2);
            
            // 绘制预测目标的反投影
            if (!targets.empty())
            {
                fmt::print("[Predictor] 有 {} 个追踪目标\n", targets.size());
                
                for (const auto& target : targets)
                {
                    // 获取当前帧 target 更新后的装甲板位置列表
                    std::vector<Eigen::Vector4d> armor_xyza_list = target.armor_xyza_list();
                    
                    // 获取 EKF 观测器内部数据
                    Eigen::VectorXd x = target.ekf_x();
                    // x[0,2,4] 是车体中心坐标 (center_x, center_y, center_z)
                    // x[6] 是车体yaw角
                    // x[8] 是半径r
                    fmt::print("[Target] EKF车体yaw: {:.4f}rad ({:.1f}°) | 装甲板数: {}\n", 
                             x[6], x[6] * 57.3, armor_xyza_list.size());
                    fmt::print("[Target] 车体中心: ({:.2f}, {:.2f}, {:.2f}) | 半径r: {:.2f}\n",
                             x[0], x[2], x[4], x[8]);
                    
                    // 将车体中心投影到图像平面
                    Eigen::Vector3d center_xyz(x[0], x[2], x[4]);
                    auto center_2d = solver.reproject_point(center_xyz);
                    
                    // 绘制车体中心（紫色十字）
                    if (center_2d.x >= 0 && center_2d.x < src_img.cols && 
                        center_2d.y >= 0 && center_2d.y < src_img.rows) {
                        cv::drawMarker(src_img, center_2d, cv::Scalar(255, 0, 255), 
                                     cv::MARKER_CROSS, 20, 3);
                        tools::draw_text(src_img, "Center", 
                                       cv::Point(center_2d.x + 15, center_2d.y - 15),
                                       cv::Scalar(255, 0, 255), 0.5, 2);
                    }
                    
                    // ==========================================
                    // 注意: 云台姿态已在帧开始时设置 (current_mock_data)
                    // PnP、追踪、重投影都使用相同的云台姿态以保持一致性
                    // 修改 gimbal_yaw/pitch 会在下一帧生效
                    // ==========================================
                    
                    // 绘制所有可能的装甲板位置（绿色 - 当前观测）
                    int plate_idx = 0;
                    for (const Eigen::Vector4d& xyza : armor_xyza_list)
                    {
                        // xyza: [x, y, z, angle]
                        // angle = 车体yaw + 装甲板相位偏移
                        fmt::print("  [Plate {}] angle(yaw+phase): {:.4f}rad ({:.1f}°) | xyz: ({:.2f}, {:.2f}, {:.2f})\n",
                                 plate_idx, xyza[3], xyza[3] * 57.3, xyza[0], xyza[1], xyza[2]);
                        
                        // 使用 solver 将 3D 坐标反投影回图像平面
                        auto image_points = solver.reproject_armor(
                            xyza.head(3),      // xyz 位置
                            xyza[3],           // 装甲板角度（车体yaw + 相位偏移）
                            target.armor_type, // 装甲板类型（大/小）
                            target.name        // 装甲板编号
                        );
                        
                        // 绘制反投影的装甲板角点（绿色多边形）
                        tools::draw_points(src_img, image_points, {0, 255, 0});
                        
                        // 计算并绘制装甲板中心点（绿色圆点）
                        Eigen::Vector3d armor_center_xyz(xyza[0], xyza[1], xyza[2]);
                        auto armor_center_2d = solver.reproject_point(armor_center_xyz);
                        if (armor_center_2d.x >= 0 && armor_center_2d.x < src_img.cols && 
                            armor_center_2d.y >= 0 && armor_center_2d.y < src_img.rows) {
                            cv::circle(src_img, armor_center_2d, 5, cv::Scalar(0, 255, 0), -1);
                            tools::draw_text(src_img, fmt::format("P{}", plate_idx), 
                                           cv::Point(armor_center_2d.x + 8, armor_center_2d.y - 8),
                                           cv::Scalar(0, 255, 0), 0.4, 1);
                        }
                        
                        plate_idx++;
                    }
                    
                    // 绘制目标状态信息
                    std::string target_info = fmt::format(
                        "ID:{} | x:{:.2f} y:{:.2f} z:{:.2f} | r:{:.2f} w:{:.2f}", 
                        target.last_id, x[0], x[2], x[4], x[8], x[7]);
                    tools::draw_text(src_img, target_info,
                                   cv::Point(10, 210),
                                   cv::Scalar(255, 255, 255), 0.5, 2);
                    
                    // 输出预测状态到终端
                    fmt::print("[Predictor] State:{} | ID:{} | xyz:({:.2f},{:.2f},{:.2f}) | r:{:.2f} w:{:.2f}\n",
                             tracker.state(), target.last_id, x[0], x[2], x[4], x[8], x[7]);
                }
            }
            else
            {
                // 没有追踪目标时的提示
                fmt::print("[Predictor] 当前无追踪目标（状态: {}）\n", tracker.state());
            }
            
            // 每帧检测后清空结果容器（避免累积），但不写入配置文件
            basic_armor_.freeMemory("");

            // === 绘制调试信息 (sp_vision_25 风格) ===
            
            // 1. 绘制帧号
            tools::draw_text(src_img,
                             fmt::format("[{}]", frame_count),
                             cv::Point(10, 30),
                             cv::Scalar(255, 255, 255),
                             0.8, 2);

            // 2. 绘制视频信息
            tools::draw_text(src_img,
                             fmt::format("Video: {}/{} | FPS: {:.1f}",
                                         current_video_idx + 1,
                                         video_files.size(),
                                         cap_fps),
                             cv::Point(10, 60),
                             cv::Scalar(154, 50, 205), // 紫色
                             0.6, 2);

            // 3. 绘制装甲板检测信息
            tools::draw_text(src_img,
                             fmt::format("Armors: {} | Targets: {}", 
                                       armor_num, targets.size()),
                             cv::Point(10, 90),
                             cv::Scalar(0, 255, 0), // 绿色
                             0.6, 2);

            // 4. 绘制追踪状态
            tools::draw_text(src_img,
                             fmt::format("Tracker: {}", tracker.state()),
                             cv::Point(10, 120),
                             cv::Scalar(255, 165, 0), // 橙色
                             0.6, 2);

            // 5. 绘制时间信息
            tools::draw_text(src_img,
                             fmt::format("Time: {:.2f}s / {:.2f}s",
                                         frame_count / cap_fps,
                                         total_frames / cap_fps),
                             cv::Point(10, 150),
                             cv::Scalar(0, 255, 255), // 黄色
                             0.6, 2);

            // 6. 收集数据用于实时绘图
            auto dt = std::chrono::duration<double>(t - last_t).count();
            last_t = t;

            nlohmann::json data;
            data["frame"] = frame_count;
            data["video_index"] = current_video_idx;
            data["timestamp"] = frame_count / cap_fps;
            data["dt"] = dt;
            data["fps"] = (dt > 0) ? (1.0 / dt) : 0;
            data["armor_num"] = armor_num;
            data["target_num"] = targets.size();
            data["tracker_state"] = tracker.state();
            
            // 添加装甲板观测数据
            if (!predictor_armors.empty())
            {
                auto& armor = predictor_armors.front();
                data["armor_x"] = armor.xyz_in_world[0];
                data["armor_y"] = armor.xyz_in_world[1];
                data["armor_z"] = armor.xyz_in_world[2];
                data["armor_yaw"] = armor.ypr_in_world[0] * 57.3;  // 转换为度
                data["armor_pitch"] = armor.ypr_in_world[1] * 57.3;
            }
            
            // 添加目标追踪数据
            if (!targets.empty())
            {
                auto& target = targets.front();
                Eigen::VectorXd x = target.ekf_x();
                data["x"] = x[0];
                data["vx"] = x[1];
                data["y"] = x[2];
                data["vy"] = x[3];
                data["z"] = x[4];
                data["vz"] = x[5];
                data["a"] = x[6] * 57.3;  // 角度（度）
                data["w"] = x[7];         // 角速度
                data["r"] = x[8];         // 半径
                data["distance"] = std::sqrt(x[0] * x[0] + x[2] * x[2] + x[4] * x[4]);
            }

            plotter.plot(data);

            // 7. 缩小显示 (sp_vision_25 风格)
            cv::resize(src_img, display_img, cv::Size(), 0.5, 0.5);

            // 发送图像到 Foxglove (限制频率以避免缓冲区溢出)
            // 方案: 每 3 帧发送一次，或每 100ms 发送一次
            static int foxglove_frame_counter = 0;
            static auto last_foxglove_publish_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_foxglove_publish_time).count();
            
            // 条件: 每 3 帧或每 100ms (取决于哪个先到达)
            if (foxglove_frame_counter % 3 == 0 || elapsed_ms >= 100) {
                // 发送缩小的图像以减少带宽 (0.5倍 = 1/4 的数据量)
                foxglove_pub.publishImage(display_img, "video_feed");
                
                // 发送串口数据 (模拟数据或实际数据)
                {
                    std::lock_guard<std::mutex> lock(mock_data_mutex);
                    
                    // 模拟写入数据 (发送给下位机的数据) - 使用正确的字段名
                    uart::Write_Data tx_data;
                    tx_data.data_type = !targets.empty() ? 1 : 0;  // 是否找到目标
                    tx_data.is_shooting = false;
                    tx_data.yaw = mock_data.yaw;
                    tx_data.pitch = mock_data.pitch;
                    tx_data.cord.x = 0;  // 预测坐标x
                    tx_data.cord.y = 0;  // 预测坐标y
                    tx_data.depth = 5000;  // 深度 (mm)
                    
                    // 构建串口数据 JSON (避免头文件依赖问题)
                    nlohmann::json serial_data;
                    
                    // 接收数据 (来自下位机)
                    serial_data["rx"]["my_color"] = static_cast<int>(mock_data.my_color);
                    serial_data["rx"]["run_mode"] = static_cast<int>(mock_data.now_run_mode);
                    serial_data["rx"]["robot_id"] = static_cast<int>(mock_data.my_robot_id);
                    serial_data["rx"]["bullet_velocity"] = mock_data.bullet_velocity;
                    serial_data["rx"]["yaw"] = mock_data.yaw;
                    serial_data["rx"]["pitch"] = mock_data.pitch;
                    serial_data["rx"]["yaw_velocity"] = mock_data.yaw_velocity;
                    serial_data["rx"]["pitch_velocity"] = mock_data.pitch_velocity;
                    
                    // 发送数据 (发送给下位机)
                    serial_data["tx"]["data_type"] = tx_data.data_type;
                    serial_data["tx"]["is_shooting"] = tx_data.is_shooting;
                    serial_data["tx"]["yaw"] = tx_data.yaw;
                    serial_data["tx"]["pitch"] = tx_data.pitch;
                    serial_data["tx"]["cord_x"] = tx_data.cord.x;
                    serial_data["tx"]["cord_y"] = tx_data.cord.y;
                    serial_data["tx"]["depth"] = tx_data.depth;
                    
                    // 发布串口数据
                    foxglove_pub.publishSerialData(serial_data);
                }
                
                last_foxglove_publish_time = now;
                foxglove_frame_counter = 0;
            }
            foxglove_frame_counter++;

            // 在画面上显示云台姿态 (便于观察参数变化)
            cv::putText(display_img,
                        fmt::format("Gimbal: Yaw={:.1f} Pitch={:.1f}",
                                    current_mock_data.yaw, current_mock_data.pitch),
                        cv::Point(10, display_img.rows - 30),
                        cv::FONT_HERSHEY_SIMPLEX,
                        0.6,
                        cv::Scalar(0, 255, 255),  // 黄色
                        2);

            cv::imshow("Minimum Vision", display_img);

            // 按键处理
            int key = cv::waitKey(delay);

            if (key == 'q' || key == 'Q' || key == 27)
            {
                fmt::print("[Info] 用户退出\n");
                // 保存参数到配置文件
                basic_armor_.freeMemory(fmt::format("{}{}", CONFIG_FILE_PATH, "/armor/basic_armor_config_new.xml"));
                fmt::print("[Info] 参数已保存到: {}/armor/basic_armor_config_new.xml\n", CONFIG_FILE_PATH);
                cap_.release();
                cv::destroyAllWindows();
                return 0;
            }

            if (key == 'n' || key == 'N')
            {
                fmt::print("[Info] 跳转到下一个视频\n");
                video_finished = true;
            }

            if (key == ' ')
            {
                fmt::print("[Info] 暂停 (按任意键继续)\n");
                cv::waitKey(0);
            }
        }
        basic_armor_.freeMemory(fmt::format("{}{}", CONFIG_FILE_PATH, "/armor/basic_armor_config_new.xml"));

        cap_.release();
        current_video_idx++;
    }

    cv::destroyAllWindows();
    foxglove_pub.stop();
    fmt::print("[Info] 所有视频播放完毕\n");

    return 0;
}