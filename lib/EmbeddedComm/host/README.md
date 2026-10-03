# EmbeddedComm 主机工具

该目录随库一同复制，无需依赖固件源码。Python 3.10 或更新版本。

| 文件 | 功能 | 可选依赖 |
|---|---|---|
| `packet.py` | 串口变长/固定/混合帧、CRC8、收发封装；包含电机消息助手 | 使用串口时需要 pyserial |
| `serial_gui.py` | 串口 GUI：端口、帧模式、轴数和电机消息调试 | pyserial、PyQt6 |
| `can_packet.py` | SunFFB 电机 CAN 协议编码/解码，支持自定义 ID 和 1..3 轴 | 无 |
| `can_cli.py` | 通用经典 CAN 监视和单帧发送，可选电机协议解码 | python-can、对应 CAN 后端驱动 |

安装依赖由用户项目环境管理：串口 GUI 使用 `pip install pyserial PyQt6`；CAN 使用 `pip install python-can`。编解码模块不需要安装硬件驱动，也不打开物理接口。

从项目根目录启动串口 GUI：

```text
python3 lib/EmbeddedComm/host/serial_gui.py
```

CAN 命令帮助无需安装 python-can：

```text
python3 lib/EmbeddedComm/host/can_cli.py --help
```

SocketCAN 通道已配置为 500kbit/s 时，接收监视与电机解码：

```text
python3 lib/EmbeddedComm/host/can_cli.py --interface socketcan --channel can0 monitor --duration 10 --decode-motor --axes 2
```

监视命令仅接收应用消息；总线控制器在正常模式下可能参与 ACK。工具不会自动启用电机。不同后端的通道命名及驱动配置由适配器决定；SocketCAN 波特率需要在操作系统中配置，CLI 的 bitrate 参数不能替代该配置。

发送一次双轴位置帧（位置均为 32768，序列 2）：

```text
python3 lib/EmbeddedComm/host/can_cli.py --interface socketcan --channel can0 send --id 0x181 --data "12 02 00 80 00 80"
```

`send` 支持 `--extended`；远程帧使用 `--remote --dlc N`，不携带 data。发送成功仅表示交给主机驱动，不表示电机已接收。仅支持经典 CAN，payload 最大 8 字节。

Python 程序使用协议助手时，将本目录加入模块搜索路径：

```python
from can_packet import encode_position, decode
identifier, data = encode_position([32768, 32768], sequence=2)
assert decode(identifier, data) == ('position', 2, [32768, 32768])
```

串口帧引擎支持自定义消息 ID 与 `fixed_lengths`；`packet.py` 中默认布局及类型助手、`can_packet.py` 和串口 GUI 的电机字段是 SunFFB 协议示例。其他项目可以采用自己的消息布局，无需修改 C++ 核心。
