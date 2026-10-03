CAN 通信

ESP32-S2/S3 使用 Arduino 核心所附 ESP-IDF TWAI 驱动，不增加第三方固件库。默认仍为 UART；CAN 显式启用。纯 C++ CAN codec/link 与硬件 HAL 分离，其他 MCU 可提供非阻塞 `send(CANFrame)` / `receive(CANFrame&)` 适配器。

配置示例：

```text
MOTOR_TRANSPORT=1
ENABLE_MOTOR_OUTPUT=1
USE_CAN_POSITION=1
CAN_BITRATE=500000
CAN_TX_PIN=4
CAN_RX_PIN=5
CAN_FORCE_ID=0x201
CAN_POSITION_ID=0x181
CAN_HEARTBEAT_ID=0x701
CAN_SINGLE_SHOT=0
```

配置均可用 build_flags 的 `-D` 覆盖。bitrate 支持 125k/250k/500k/1M，默认 500k。`USE_CAN_POSITION=0` 时使用本地 ADC，CAN 接收任务仍负责驱动恢复。UART 与 CAN 位置输入互斥；GPIO19/20 保留给原生 USB，GPIO 冲突会在配置可知时编译失败。默认 CAN GPIO 复用原 UART 的 4/5，只初始化所选传输。LCD 默认关闭；启用 LCD 时还需核对外部 TFT 引脚配置。

接线：需要外部 CAN 收发器，TX GPIO 接 TXD、RX GPIO 接 RXD，收发器的逻辑侧应兼容 3.3V GPIO；CAN_H/CAN_L 接总线、设备共地、总线两端各使用 120Ω 终端。S2/S3 内置 TWAI 是控制器，不能把 GPIO 直接接 CAN_H/CAN_L。

构建：

```text
pio run -e esp32-s2-can
pio run -e esp32-s3-can
pio run -e esp32-s2-can-axis1
pio run -e esp32-s3-can-axis3
pio run -e esp32-s2-can-timing
pio run -e esp32-s2-can-adc
```

协议为项目自定义的经典 CAN 2.0 标准 11-bit 数据帧，不是 CAN FD/CANopen。当前固件接收一个可配置的位置 ID；不同设备须使用不冲突的 ID。所有多字节字段小端。

| CAN ID（默认） | 方向 | DLC | 内容 |
|---|---|---|---|
| 0x201 | MCU → 电机控制器 | 2 + 2×轴数 | Header、Sequence、int16 force[轴数] |
| 0x181 | 电机控制器 → MCU | 2 + 2×轴数 | Header、Sequence、uint16 position[轴数] |
| 0x701 | 可选诊断发送 | 3 | Header、Sequence、用户定义 Status |

Header 高 4 位为版本 1，低 4 位为轴数 1..3：0x11、0x12、0x13。Sequence 为 8-bit，每次 HAL 接受发送后递增，255 后回到 0。力为原计算器的 nominal -10000..10000，CAN 中用 int16 表达，不丢精度；位置为 0..65535，32768 为中心。CAN 硬件提供 CRC，不叠加串口 CRC。三轴向量最大 8 字节，整组数据一帧完成，避免分片产生混合轴样本。

双轴例子：

```text
Force(-10000,+10000), Seq=7 : ID=0x201 DLC=6 DATA=12 07 F0 D8 10 27
Position(32768,65535), Seq=8: ID=0x181 DLC=6 DATA=12 08 00 80 FF FF
```

MCU 只接受正确位置 ID、标准数据帧、精确 DLC、匹配的版本/轴数。重复 Sequence 在距上一接受帧不足 100ms 时被过滤；100ms 间隔后允许重新同步，driver 恢复也重置序号历史。Sequence 用于去重，不表示电机生成时间，也不执行一般的乱序排序。发送端应每帧递增，不要一直发送默认序号 0。

C++ codec：`encode_can_force` / `decode_can_force`、`encode_can_position` / `decode_can_position`。力编码拒绝超出 nominal 范围的值；解码失败不修改输出。`FFBCANLink` 提供 `sendForce`、`sendPosition`、`sendHeartbeat`、`pollPosition`，后者区分 Empty/Ignored/Position；仅 Position 返回可发布新的位置。一个 RX 和一个 TX 调用者可并行，同方向多个调用者需要外部锁。CAN codec/link 不在每帧中申请堆内存；TWAI 的驱动/队列资源在初始化时建立。

调度与背压：

- 力计算保持原 500/1000Hz，CAN 力提交周期为 2ms。
- CAN RX 使用零等待，每 1ms 最多处理 8 帧；位置发布沿用现有到达驱动的输入→USB 链路，不额外用 500Hz 定时器重复旧位置。
- 硬件过滤器筛选位置 ID，软件再次验证格式。RX 队列为 16 帧，噪声/高负载不会使接收任务无限运行。
- TX 软件队列长度为 0，`twai_transmit(...,0)` 不等待空间；在忙/恢复时返回失败，下一周期从力计算队列读取最新值再尝试。已经开始的单帧可能仍处于硬件重试中。
- 默认允许硬件重试，避免周期性仲裁冲突持续丢帧；`CAN_SINGLE_SHOT=1` 可改为单次尝试，但丢失仲裁或发送错误也可能丢掉该周期帧。
- bus-off 时停止接受新的 TX，RX 任务发起恢复；恢复完成后重新启动并清除位置序号历史。硬件恢复遵循 TWAI 总线状态，不进行阻塞等待或在力计算任务中重启驱动。
- motor_tx 统计的是驱动接受次数，不是电机收到次数。可选 timing 配置另外输出 TX success/failure、bus-off、recovery、RX overflow 的诊断计数。TWAI alert 可以合并，success/failure/overflow alert 计数不是精确的逐帧完成/丢失总数。

按每帧 160bit 的保守预算，力和位置各 500Hz 共约 160kbit/s，默认 500kbit/s 留有余量。低波特率、其他节点优先级和总线占用会影响更新率；编译和桌面测试不能证明 500Hz。应使用 CAN 分析仪/电机端计数，并结合 `fresh`、`usb_done` 测量实际接收率、延迟和最坏负载抖动。

Python 编解码在 `host/can_packet.py`，不依赖 python-can：

```python
from can_packet import encode_position, decode
identifier, payload = encode_position([32768, 32768], sequence=8)
assert decode(identifier, payload) == ('position', 8, [32768, 32768])
```

实际主机收发可使用 python-can：把 identifier/payload 传给标准帧 `can.Message(arbitration_id=identifier, data=payload, is_extended_id=False)`；接收时将 arbitration_id/data 及 is_extended_id/is_remote_frame 交给 decode。主机适配器/操作系统 CAN 通道须同样配置 500kbit/s。编解码参数支持自定义 ID 和 1..3 轴，heartbeat 可单独解析；固件默认位置过滤器不接收 heartbeat。

验证：

```text
pio test -e native-sanitized -e native-sanitized-axis1 -e native-sanitized-axis3 -v
python3 -m unittest discover -s python_apis
```

本轮 184 次严格 C++ 回归、65 项 Python 回归、8 个 MCU 配置构建通过。C++ 包含黄金字节、力范围、标准 ID/DLC/版本/轴数、重复序号与时间回绕、发送背压、TWAI 初始化失败清理、bus-off 恢复/重启失败和可选单次发送。TWAI 单元测试使用 mock SDK 检查控制逻辑；真实 SDK 则由 S2/S3 构建验证，电气链路、仲裁、ACK 与恢复时序尚未实机验证。
