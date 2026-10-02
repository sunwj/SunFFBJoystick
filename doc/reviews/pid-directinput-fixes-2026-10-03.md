HID PID / DirectInput 修复记录（2026-10-03）

依据用户提供的 PID 与 DirectInput 文档，落实 [修复前审阅的 12 项问题](pid-directinput-review-2026-10-03.md)。修复前 UBSan 和失败结果保留在原记录中。

| 审阅项 | 最终修改 |
|---|---|
| 1 内部 float 对齐 | 移除内部 Metrics 的 packed，增加自然对齐断言。HID wire structs 保持 packed。ASan/UBSan 加入 `-fno-sanitize-recover=all`。 |
| 2 条件参数单位 | 描述符 CP Offset logical ±10000，Dead Band logical 0..10000，与固件及 Python 的 nominal 值一致。输入轴仍为 ±32767。 |
| 3 PID 状态 | 每个效果保存最新待发送状态和 revision；停止保留效果编号。USB 接受报告后才确认相应 revision；失败重试，各效果互不覆盖。延迟和等待触发不冒充实际播放。 |
| 4 方向编码 | 按描述符固定解码角度；Direction Enable 用于条件模式。Direction Output 保留角度单位，输出后再清除单位。 |
| 5 触发释放 | 已触发的播放持续到结束；释放仅取消后续保持触发重复。支持有限/无限循环、延迟及重复等待。 |
| 6 执行器关闭 | 关闭执行器时继续推进触发、循环和结束状态，最终输出为零。Pause 独立冻结计时，包括执行器同时关闭的情况。 |
| 7 负系数饱和 | 根据指标位于中心哪一侧选择该侧 saturation，正负系数共用正确的幅值限制；Python 同步。 |
| 8 旧条件块 | Direction Enable 只使用 block 0 投影；独立轴仅使用已配置且启用的对应条件块。 |
| 9 方向摩擦 | 先投影速度，再按 2% 阈值取符号；独立轴分别应用各轴速度尺度。 |
| 10 条件采样周期 | 缓存各效果的力样本，条件效果也保持输出到下一个采样边界；参数更新、启动和循环重新采样。缓存位于效果块，无动态分配。 |
| 11 零持续幅值包络 | 使用绝对 attack/fade 幅值，零 sustain 也能产生有效包络；周期偏置不随包络缩放。Python 同步。 |
| 12 长时间相位 | 时间先进行整数取模，再转换为浮点计算周期相位，避免数小时后的相位精度丢失。 |

验证：新增 `test/test_spec_compliance`，覆盖原规范复现、每效果状态重试、触发循环、延迟状态、缓存失效、静音/暂停组合、零持续幅值 fade、不同轴数方向和 Stop All。

- 三个轴数配置的严格 C++ 测试：130 次全部通过，详细输出无 ASan/UBSan 错误。见 [C++ 日志](pid-directinput-fixes-native-results.log)。
- Python：52 项全部通过。见 [Python 日志](pid-directinput-fixes-python-results.log)。
- S2/S3 共 11 个配置构建通过：1/2/3 轴、时序诊断、500Hz 和 LCD 开关配置。见 [构建日志](pid-directinput-fixes-build-results.log)。现有 USB VID/PID/Product 宏重定义及 TFT 库警告仍存在，未出现编译错误。
- Python 常量与固件：`generate_constants.py --check` 通过。

PID Device Reset 保持规范要求：清除暂停、启用执行器、释放效果。这是 HID wire 命令，与 DirectInput API Reset 的表述属于不同层。

每效果 pending 状态保存最新值，因此同一效果在端点忙碌期间可合并为最终状态；不同效果的最终停止不会相互覆盖。位置报告保留优先发送策略，PID 状态使用空余 USB 时隙。

LCD 默认关闭和原 500/1000Hz 调度配置保持。桌面测试和编译不能证明板上更新率、USB 枚举或 Windows DirectInput 端到端兼容；这些需 S2/S3 硬件、主机与电机控制器联合测量。

描述符已更新，刷入后重新连接 USB。直接发送 HID 的客户端需使用 nominal 条件参数和角度方向；DirectInput 的 API 坐标由主机驱动转换。
