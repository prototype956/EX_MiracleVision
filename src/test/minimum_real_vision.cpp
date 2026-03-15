/**
 * @file minimum_real_vision.cpp
 * @brief 最小实机测试系统（串口通信 + 虚拟/真实发弹指令测试）
 * 
 * 用于验证重构后的视觉系统与下位机（RM-OmniDrive等）的串口通信（rm_protocol.hpp）是否正常工作。
 * 可以单独运行以测试串口上下行收发。
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include "hal/serial/uart_serial.hpp"
#include "hal/serial/rm_protocol.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace mv::hal;
using namespace mv::protocol;

int main() {
    mv::Logger::Instance().Init("logs");
    MV_LOG_INFO("min_real", "开始最小实机通信测试");

    auto& cfg = mv::ConfigManager::Instance();
    // 假设 vision.yaml 中有串口配置
    try {
        cfg.Load(CONFIG_FILE_PATH "/vision.yaml");
    } catch(...) {
        MV_LOG_WARN("min_real", "未找到 vision.yaml，强制使用默认配置或串口将无法打开");
    }

    auto serial = std::make_unique<UartSerial>();
    YAML::Node serial_cfg;
    serial_cfg["device"] = "/dev/ttyUSB0";
    serial_cfg["baudrate"] = 115200; // 根据实际情况可调为921600

    if (!serial->Open(serial_cfg)) {
        MV_LOG_ERROR("min_real", "串口打开失败，请检查全向轮底盘连接");
        return -1;
    }
    MV_LOG_INFO("min_real", "串口打开成功");

    // 循环测试收发
    uint8_t seq = 0;
    while (true) {
        // --- 1. 构造协议下行帧发给电控 ---
        DownFrame mock_cmd;
        mock_cmd.seq = seq++;
        mock_cmd.detected = 1;
        mock_cmd.shoot = 0;
        mock_cmd.yaw = 1000;   // mock 0.1 rad
        mock_cmd.pitch = -500; // mock -0.05 rad
        mock_cmd.distance = 200; // 2.00m
        
        // 按照 rm_protocol.hpp 进行打包: 15字节
        uint8_t tx_buf[DOWN_FRAME_LEN] = {0};
        tx_buf[0] = FRAME_HEAD_0;
        tx_buf[1] = DOWN_HEAD_1;
        tx_buf[2] = DOWN_PAYLOAD_LEN;
        tx_buf[3] = mock_cmd.seq;
        tx_buf[4] = mock_cmd.detected;
        tx_buf[5] = mock_cmd.shoot;
        
        // Little Endian copy
        *reinterpret_cast<int16_t*>(&tx_buf[6]) = mock_cmd.yaw;
        *reinterpret_cast<int16_t*>(&tx_buf[8]) = mock_cmd.pitch;
        *reinterpret_cast<uint16_t*>(&tx_buf[10]) = mock_cmd.distance;
        
        // 计算并填入 CRC16-CCITT
        uint16_t crc = Crc16Ccitt(tx_buf, DOWN_CRC_LEN);
        *reinterpret_cast<uint16_t*>(&tx_buf[12]) = crc;
        tx_buf[14] = FRAME_TAIL;

        serial->Send(tx_buf, DOWN_FRAME_LEN);
        MV_LOG_INFO("min_real", "发送 DownFrame Seq: {}", mock_cmd.seq);

        // --- 2. 尝试从电控读取上行帧 ---
        uint8_t rx_buf[256];
        std::size_t received = 0;
        if (serial->Recv(rx_buf, sizeof(rx_buf), received) && received > 0) {
            MV_LOG_INFO("min_real", "收到下位机数据 {} 字节", received);
            // 简单打印电控传来的部分头字节供调试观察
            for(size_t i=0; i<std::min(received, (size_t)10); ++i) {
                printf("%02X ", rx_buf[i]);
            }
            printf("\n");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10Hz
    }

    return 0;
}
