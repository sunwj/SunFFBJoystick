# EmbeddedComm

可移植的 C++17 通信库。复制整个 `lib/EmbeddedComm` 目录到另一个 PlatformIO 项目的 `lib/` 即可使用。Arduino IDE 可将此目录放入用户 libraries 目录，并为目标核心启用 C++17；库提供 `library.properties`。其他构建环境将 `src/` 加入头文件搜索路径。

库命名空间为 `EmbeddedComm`，采用头文件模板，无堆分配、无虚函数、无 FreeRTOS 依赖。核心不依赖 Arduino、ESP32、轴数、USB 或电机消息。

| 头文件 | 用途 | 依赖 |
|---|---|---|
| `EmbeddedComm/SerialLink.h` | 固定、变长、混合串口帧，CRC8，限额解析 | C++17 标准库 |
| `EmbeddedComm/CanFrame.h` | 经典 CAN 2.0 帧 | `<stdint.h>` |
| `EmbeddedComm/CanLink.h` | 通用 CAN 帧收发和 ID/DLC 校验 | 调用方 HAL |
| `EmbeddedComm/ArduinoSerialHal.h` | Arduino 串口适配 | Arduino 核心 |
| `EmbeddedComm/Esp32TwaiHal.h` | 非阻塞 TWAI、过滤、bus-off 恢复 | ESP-IDF 4.x TWAI API |

## 串口

```cpp
#include <EmbeddedComm/SerialLink.h>
#include <EmbeddedComm/ArduinoSerialHal.h>

struct SensorLayout
{
    static constexpr uint16_t length(uint8_t id)
    {
        return id == 0x42 ? 3 : 0xFFFF;
    }
};

EmbeddedComm::ArduinoSerialHal hal(Serial1);
EmbeddedComm::SerialLink<EmbeddedComm::ArduinoSerialHal,
                         EmbeddedComm::SerialFraming::Mixed, 64, SensorLayout> link(hal);

void sendSensor()
{
    const uint8_t data[] = {1, 2, 3};
    link.sendRaw(0x42, data, sizeof(data), EmbeddedComm::SerialFrameFormat::Fixed);
}

void pollSensor()
{
    EmbeddedComm::SerialFrameView frame;
    if (link.receiveFrame(frame, 68))
    {
        // 在下一次 receiveFrame 调用前使用或复制 frame.payload。
    }
}
```

串口初始化（引脚、波特率、缓冲容量）由应用负责。`BasicArduinoSerialHal<SerialType>` 可适配其他 Arduino 串口类型，要求具有 `availableForWrite/write/available/read` 接口；ESP32 路径还要求 `read(buffer, capacity)` 是非阻塞批量读取。

默认变长帧：`AA ID LEN PAYLOAD CRC8`。固定帧：`AB ID PAYLOAD CRC8`。Mixed 接收两者，默认发送变长帧。CRC-8/MAXIM-DOW 覆盖 ID、payload，以及变长帧的 LEN。容量为模板参数，范围 1..255。默认 `NoFixedLayout` 不接受任何固定消息；固定模式应提供 `length(id)` 布局，未知 ID 返回 `0xFFFF`，允许合法零长度消息。

自定义 HAL 提供非阻塞 `available()`、`write(data, size)` 和 `readSome(out, capacity)`，也可使用单字节 `read()` 的兼容路径。每次解析最多处理 byteBudget 字节，批读缓存为 32 字节。CRC 查表、单遍复制与校验、一次完整帧 write，均保留原有性能路径。

`sendRaw` 返回 false 表示参数无效或写入未完成。HAL 应在容量不足时整帧拒绝；若发生短写，已经写出的字节无法撤回。CRC/长度错误会重新寻找帧边界，但未使用转义的协议不能保证立即恢复下一帧。`resetReceiver()` 可用于对端复位或半帧超时，超时策略由应用决定。

## CAN

```cpp
#include <EmbeddedComm/CanLink.h>
#include <EmbeddedComm/Esp32TwaiHal.h>

EmbeddedComm::Esp32TwaiHal hal;
EmbeddedComm::CanLink<EmbeddedComm::Esp32TwaiHal> link(hal);

void startBus()
{
    hal.begin(4, 5, 500000); // 默认接收所有 ID；需外部收发器。
}

void sendMessage()
{
    EmbeddedComm::CANFrame frame;
    frame.id = 0x123;
    frame.length = 2;
    frame.data[0] = 0x10;
    frame.data[1] = 0x20;
    link.send(frame);
}

void pollBus()
{
    hal.service(); // RX 所有者负责 bus-off 恢复。

    EmbeddedComm::CANFrame frame;
    if (link.receive(frame))
    {
        // 应用按 ID 解释 payload。
    }
}
```

`CanLink<Hal>` 要求 HAL 提供 `bool send(const CANFrame&)`、`bool receive(CANFrame&)`，无平台依赖。支持标准/扩展 ID 和远程帧，DLC 为 0..8；不支持 CAN FD。receive 消耗一帧；无帧或丢弃非法 ID/DLC 时返回 false，并保留调用方输出不变。应用应使用有界轮询次数，避免噪声占满任务时间。

TWAI `begin(txPin, rxPin, bitrate, receiveId=0xFFFF, singleShot=false)`：默认 0xFFFF 接收全部 ID；指定 0..0x7FF 时过滤单个标准 ID，应用仍需检查帧格式。支持 125/250/500/1000 kbit/s，RX 队列 16，TX 软件队列关闭，收发均零等待，默认硬件重传。只有 `service()` 所有者负责恢复；返回 true 表示已经重新启动，应用可重置协议序列历史。该适配器使用 ESP-IDF 4.x API，其他 MCU、CAN 外设或新版驱动需提供各自 HAL；核心无需修改。

## 并发与移植边界

一个 RX 所有者和一个 TX 所有者可并行，HAL 必须支持对应并发。多个同方向调用者需外部锁。初始化及驱动销毁应由应用在收发任务启动前后管理。帧缓冲容量固定，不产生额外后台任务；调度周期、重试、序列号、心跳、消息类型、字节序与业务单位由应用决定。

当前项目的 `firmware/src/motor_protocol/serial_link.h`、`can_protocol.h` 和 `can_link.h` 是电机协议适配层，使用 `SunFFB::MotorSerialLink` / `MotorCANLink`。原 `FFBSerialLink` / `FFBCANLink` 名称保留为兼容别名。Python 编解码、串口 GUI 和 CAN CLI 位于库的 `host/` 目录。其中电机消息编解码为 SunFFB 协议示例，通用 C++ 核心无需依赖它们。详见 [主机工具说明](host/README.md)。

## 验证

`pio test -e native-comm` 单独以 C++17 编译库，不编译固件源文件、不包含项目轴数配置。已有串口/CAN 回归覆盖协议兼容与驱动行为；S2/S3 环境覆盖硬件适配编译。编译和本机测试不能证明真实 MCU 的 500Hz 时序，仍须进行板上测量。
