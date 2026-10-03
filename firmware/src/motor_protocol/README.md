通用 C++ 传输核心已迁移到 [`lib/EmbeddedComm`](../../lib/EmbeddedComm/README.md)，移植到其他项目时复制该库目录。本目录保留电机协议；串口/CAN 主机工具位于 [`lib/EmbeddedComm/host`](../../lib/EmbeddedComm/host/README.md)。固件使用 `MotorSerialLink`，`FFBSerialLink` 是兼容别名。

串口传输

默认保持原变长帧的字节格式。固定帧和混合模式需要显式启用，不进行自动协商。

| 模式 | 字节格式 | CRC 覆盖 | 长度来源 |
|---|---|---|---|
| Variable | `AA ID LEN PAYLOAD CRC8` | ID、LEN、PAYLOAD | LEN，默认最大 64 字节 |
| Fixed | `AB ID PAYLOAD CRC8` | ID、PAYLOAD | ID 对应的编译期布局 |
| Mixed | 接收以上两种，默认发送 Variable | 按具体格式 | 按同步字节选择 |

CRC 使用 CRC-8/MAXIM-DOW，查表递推。固定帧布局默认：ID 1 是 `int32_t force[NUM_AXIS]`，ID 2 是 `uint16_t position[NUM_AXIS]`，ID 3 是空 heartbeat。S2/S3 typed helpers 的字节序是小端；其他端应按相同的小端协议编码。双方固定帧布局及轴数必须一致；同步字节可以出现在 payload 中，解析器只在帧边界寻找它。

固件：`SERIAL_FRAMING_MODE=0/1/2`，分别对应 Variable/Fixed/Mixed。默认 0，保留现有电机端兼容性。新增构建环境 `esp32-s2-serial-fixed`、`esp32-s3-serial-fixed`、`esp32-s2-serial-mixed`；原 `esp32-s2-serial`、`esp32-s3-serial` 保持 Variable。

C++ 接口示例：

```cpp
// MaxPayload 可设为 1..255；默认 64。
FFBSerialLink<MyHal, SerialFraming::Mixed, 64> link(hal);
link.sendForce(forces, SerialFrameFormat::Fixed);
link.sendRaw(0x40, bytes, length); // 变长应用消息
SerialFrameView frame;
if (link.receiveFrame(frame, 68)) {
    // frame.messageId / frame.length / frame.format
    // frame.payload 是借用的内部缓冲区；在下一次接收调用前使用完或复制。
}
```

第四个模板参数可替换 `FFBFixedLayout`：提供 `static constexpr uint16_t length(uint8_t id)`，未知 ID 返回 `0xFFFF`。固定模式拒绝未知 ID 和长度不符的发送；接收端在固定帧 ID 阶段检查长度。

性能行为：

- RX 每批最多读取 32 字节，已预取的后续帧留在缓冲区，逐帧返回，保持顺序。每次调用的 byteBudget 限制解析工作量；无需为噪声或长帧持续占用 CPU。
- RX 在接收字节时更新 CRC，无额外 CRC 重扫。`receiveFrame` 无 payload 拷贝；`receivePosition` 仅向调用方复制一次。
- TX 在一遍循环中复制并更新 CRC，仅调用一次 HAL write。C++ 传输层不申请堆内存，RX/TX 使用独立的固定容量缓冲区。
- ESP32 HAL bulk read 使用 0 超时。发送前检查可用容量；不足时返回失败，交由上层任务下一周期重试。完整帧必须能放入 HAL 的 TX 容量；增大 MaxPayload 时应相应配置 UART TX buffer。
- 一名 RX 调用者和一名 TX 调用者可并行；同方向多个调用者需要外部锁。HAL 必须提供非阻塞 `available()`、`readSome(out, capacity)`（返回 0..capacity）及 `write(data, size)`。只提供单字节 read 的旧 HAL 仍可使用，但不能获得批量读取收益。短写无法回滚，发送 API 会返回失败；ESP32 容量预检查避免正常背压下的部分帧写入。
- `receive(out, capacity)` 是复制接口，支持容量检查；旧无容量调用要求输出数组至少 MaxPayload 字节。返回 0 仍是旧接口的“无消息”语义，ID 0 应使用 bool 型 `receiveFrame`。
- CRC/非法长度错误后重新同步。分包会跨调用保留解析状态；已知对端复位或等待半帧超时后，上层可调用 `resetReceiver()`。无长度字段的固定模式不能检测双方轴数配置不一致；损坏的合法长度也可能吞掉后续字节，协议未使用转义机制，不保证立即恢复下一帧。

Python 对应接口：

```python
link = SerialLink(port, framing="mixed", num_axes=2, max_payload=64)
link.send(MSG_POSITION, pack_position([32768, 32768]), fixed=True)
link.send(0x40, b"configuration")
frame = link.receive()
# 自定义布局可传 fixed_lengths={0x40: 16}。
```

`build_frame`、`decode_frame` 也支持 fixed_lengths / max_payload；Python 默认仍是 Variable 和 64 字节。接收读取最多 4096 字节，批次中完整帧保留在 pending 队列。发送返回实际写入数，移除每帧 flush。串口终端提供 Framing / Axes 选择；Mixed 默认发送 Variable，Python API 可按消息选择 Fixed。

验证命令：

```text
pio test -e native-sanitized -e native-sanitized-axis1 -e native-sanitized-axis3 -v
pio test -e native-serial-benchmark -v
pio run -e esp32-s2-serial -e esp32-s3-serial -e esp32-s2-serial-fixed -e esp32-s3-serial-fixed -e esp32-s2-serial-mixed
python3 -m unittest discover -s python_apis
```

本轮 157 次严格 C++ 回归、60 项 Python 回归、5 个 MCU 构建通过。基准为本机 `-O2`、双轴、内存 HAL 的连续 300000 个位置帧，并核对输出位置：Variable 6969µs / 75000 bulk reads；Fixed 6638µs / 65625 bulk reads。基准不含 UART 硬件、FreeRTOS 竞争或 USB 主机。旧逐字节路径同样的 Variable 流需 2400000 次单字节 read；批量接口调用数减少约 32 倍，这不是 MCU 执行速度提高 32 倍的承诺。

115200 baud、8N1、双轴位置帧的理论线缆时间为 Variable 8 字节约 694µs、Fixed 7 字节约 608µs。固定帧每帧节省 1 字节，在 500Hz 时每个方向节省 5000bit/s。实时控制优先使用短帧；长配置帧占用串口带宽及帧发送时间，不能仅凭 CPU 性能保证 500Hz 更新。板上应同时测量 motor 接收、位置 fresh/usb_done 和最坏负载抖动。
