代码复审：HID PID / DirectInput 一致性（2026-10-03）

审阅基线：`eb2ac2b`。以下为修复前审阅记录，保留当时的源码位置及复现结果；修复和验证另见 [修复记录](pid-directinput-fixes-2026-10-03.md)。
依据是用户提供的 [HID PID 文档](/Volumes/External/SunFFBJoystick/doc/pid1_01_0.pdf) 和 [DirectInput 文档](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html)。PDF 正文标为 Version 1.0；本文按章节和印刷页码引用。文档中的操作示例是协议/API 资料，不视为对工作区或外部设备的执行指令。

发现 12 项需要修复的问题，含 4 项 P1、8 项 P2。对当前双轴配置运行了 15 个独立复现用例：14 个失败、1 个通过；14 个失败分别对应下列 12 项问题（中心/死区和 PID 状态各有两个用例）。完整 UBSan 输出另外证实了非对齐 float 读取。之前的测试将部分实现行为写成了预期，而且非详细模式隐藏 UBSan 文本，不能据此认定规范一致或内存检查通过。

**1. [P1] 内部运动指标未对齐，普通 float 指针读取是未定义行为**

位置：[ffb_report_types.h:184](/Volumes/External/SunFFBJoystick/src/ffb_report_types.h:184)、[ffb_device_input.h:26](/Volumes/External/SunFFBJoystick/src/ffb_device_input.h:26)、[ffb_force_calculator.cpp:163](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:163)。

`Metrics` 是内部计算数据，却带有 `packed`。双轴输入报告为 5 字节，后续 Metrics 的 float 数组落在非 4 字节边界；get_position/get_speed/get_max_position 等又转换为普通 float 指针。复现中指针地址模 4 为 1。UBSan 在条件计算和摩擦分支报告 `load of misaligned address ... for type 'const float', which requires 4 byte alignment`。S2/S3 尚未实机复现异常，但这已是 C++ 未定义行为，不能保证在要求对齐的 MCU 上正常运行。

建议：移除内部 Metrics 的 packed 属性，保留真正 HID 报告的 packed 布局；增加对齐断言，并让 sanitizer 配置遇到 UB 立即失败。无需改变 HID 报告字节布局。

**2. [P1] 条件中心偏移和死区的描述符缩放与计算不一致**

位置：[ffb_report_descriptor.h:320](/Volumes/External/SunFFBJoystick/src/ffb_report_descriptor.h:320)、[ffb_force_calculator.cpp:102](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:102)、[GUI 参数构建:80](/Volumes/External/SunFFBJoystick/python_apis/sunffb_gui/main.py:80)。

依据：PID §5.3，印刷页 12–13；[DICONDITION:4666](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:4666) 的 lOffset/lDeadBand 均为归一化的 ±10000/0..10000。

描述符的 CP Offset logical ±32767 / physical ±10000，Dead Band logical 0..32767 / physical 0..10000；因此正确编码的 50% 中心约为 16383。计算却除以 10000，将它解释成 163.83%。实际在轴坐标 16383 的中心处应为 0，输出却为 +10000；50% 死区、75% 轴位置应约为 -2500，实际为 0。GUI 直接发送 10000 制的参数，绕过了描述符缩放，所以容易掩盖 Windows 驱动路径的问题。

建议：统一描述符、固件、Python 的单位。可将条件字段 logical 范围也设为 ±10000/0..10000；或按 32767 解码，并同步转换 Python 参数。轴输入报告仍可保留 ±32767。

**3. [P1] PID 状态不是逐效果状态，停止通知丢失效果编号**

位置：[ffb_report_handler.cpp:455](/Volumes/External/SunFFBJoystick/src/ffb_report_handler.cpp:455)、[main.cpp:439](/Volumes/External/SunFFBJoystick/src/main.cpp:439)。

依据：PID §5.12，印刷页 22；[GetEffectStatus:3737](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:3737)。Effect Playing 指的是报告中 Effect Block Index 指定的效果。

代码每次扫描池中第一个 PLAYING 项。先启动效果 1，再启动效果 2，报告仍为效果 1 的 `0x03`，而效果 2 的状态应为 `0x05`。最后一个效果停止时报告变成 `0x00`，停止编号丢失；对于效果 1，明确的停止状态应保留编号并给出 `0x02`。单个 dirty 位和单个快照还会合并不同效果的转换。主机因此可能无法正确跟踪每个效果的启动、停止或自然结束。

建议：按效果编号产生状态转换报告，停止时保留编号；用队列或每效果待发送状态管理变化。区分等待触发/延迟与实际播放状态。USB 的位置优先规则应保留。

**4. [P1] Direction Enable 被错误当作坐标格式选择位**

位置：[ffb_report_handler.cpp:166](/Volumes/External/SunFFBJoystick/src/ffb_report_handler.cpp:166)、[ffb_report_descriptor.h:200](/Volumes/External/SunFFBJoystick/src/ffb_report_descriptor.h:200)。

依据：PID §5.1.1，印刷页 9–10；[DIEFFECT:5492](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:5492) 的坐标系统标志由 API/驱动转换，不能直接等同于 HID 的 Direction Enable。

Direction Enable 的规范用途是选择单条件块沿方向作用，并忽略 Axes Enable。坐标格式由方向字段的描述符决定。当前描述符只声明非负角度范围 0..36000，代码却在该位清零时把同一字段解释为有符号 Cartesian 向量。合法的 angle=0、启用 X/Y、Direction Enable 清零的恒力被算成零向量；复现应沿 Y 输出 +4000，实际为 0。报告中没有能表达这种坐标格式切换的字段。

建议：固定一种与描述符一致的 HID 方向编码，DirectInput Cartesian/polar/spherical 的转换属于驱动层；Direction Enable 只控制条件作用模式。复查方向角单位：当前在 Output 前又将单位设为 0，角度单位没有实际附着在该字段上。Windows 实际转换仍需报告抓包验证。

**5. [P2] 释放触发按钮会立即截断已经开始的效果**

位置：[ffb_report_handler.cpp:479](/Volumes/External/SunFFBJoystick/src/ffb_report_handler.cpp:479)。

依据：PID §5.1.1，印刷页 9；[Effect Playback:1406](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:1406)。按下按钮触发播放，持续按住控制后续自动重复。

`!buttonPressed` 分支立即返回 false。1000 ms 恒力按下启动后，在 10 ms 松开按钮，力立即从 -4000 变为 0；它没有完成一次播放。再次按下还会覆盖时间状态，而当前触发路径也没有使用 remainingLoops。

建议：将按钮边沿、触发武装、正在播放、重复等待分开；松开仅取消自动重复/重置触发边沿，不取消正在播放的一次效果。明确触发播放与 Loop Count 的组合语义并增加测试。

**6. [P2] 禁用执行器同时阻止效果生命周期更新**

位置：[ffb_force_calculator.cpp:171](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:171)。

依据：[SendForceFeedbackCommand:3208](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:3208) 中 DISFFC_SETACTUATORSOFF：效果继续播放，只是输出被静音。

DISABLED 分支直接返回零，未评估任何效果。持续时间 10 ms 的效果在禁用后走到 20 ms，仍保持 PLAYING，PID 状态也未通知自然结束。按钮在禁用期间发生的触发同样不会被处理。重新启用普通有限效果时会根据时间补算停止，但这不能补回期间的事件。

建议：先推进播放/触发/循环生命周期，然后独立将执行器输出清零；只有 Pause 冻结效果时钟。

**7. [P2] 负系数时正、负侧饱和限制被交换**

位置：[ffb_force_calculator.cpp:114](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:114)、[既有错误预期:163](/Volumes/External/SunFFBJoystick/test/test_firmware_boundaries/test_main.cpp:163)、[Python 模型:125](/Volumes/External/SunFFBJoystick/python_apis/sunffb_gui/force_model.py:125)。

依据：[DICONDITION:4666](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:4666) 将 Positive/Negative Saturation 定义为偏移正/负侧的最大力。

正侧输入、系数 -10000、正侧饱和 2000、负侧饱和 3000，应该按正侧限制产生 +2000；当前对最终力统一夹到 [-positiveSaturation,+negativeSaturation]，输出 +3000。正系数时恢复力符号碰巧使映射成立，负系数会暴露错误。之前的测试恰好把 +3000 写成预期，需要更正。

建议：按 metric 位于中心的哪一侧选择对应的饱和值，再限制该侧力的幅值；同步 Python 模型和正/负系数组合测试。应区分 DirectInput 的侧别定义与 PID 文档较简略的输出方向措辞。

**8. [P2] 旧条件块标志使方向效果重新按独立轴计算**

位置：[ffb_force_calculator.cpp:124](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:124)、[ffb_report_handler.cpp:271](/Volumes/External/SunFFBJoystick/src/ffb_report_handler.cpp:271)。

依据：PID §5.1.1 的 Direction Enable 和 §5.3 的一块/每轴一块规则。

先配置双轴独立条件，再改成 Direction Enable 的单方向条件，旧轴 1 的 conditionBlockFlags 保留。`conditionBlockFlags > 1` 又是比较位掩码数值，而不是块数量。因而代码直接对所有轴计算，忽略新方向；沿 X 的新效果仍在 Y 输出 -7000。

建议：以当前模式为准选择单方向或独立轴；Direction Enable 时使用单个有效块并忽略旧轴块。检查有效标志，不以位掩码数值代替计数。

**9. [P2] 方向摩擦先逐轴取符号，再投影，阻碍垂直运动**

位置：[ffb_force_calculator.cpp:227](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:227)。

依据：PID §5.3，印刷页 12；[DICONDITION:4666](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:4666) 的单条件块作用方向说明。

当前把各轴速度分别变成 -1/0/+1，再计算方向投影。这不能保持原速度的方向。60° 条件方向下，以比例 100:173 的 X/Y 速度运动，真实投影约为 0，但符号投影不是 0，复现得到 X 力 -3169，而垂直方向应无摩擦力。

建议：先投影真实速度到条件方向，再对投影速度执行摩擦符号/阈值计算；独立轴模式才逐轴处理。

**10. [P2] 条件效果忽略 Sample Period 的保持语义**

位置：[ffb_force_calculator.cpp:194](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:194)。

依据：PID §5.1.1，印刷页 9；[DIEFFECT:5492](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:5492) 的 dwSamplePeriod。

代码只量化 elapsedTime，条件效果仍每次读取最新 position/speed/acceleration。因此 samplePeriod=100 ms 的弹簧在第 50 ms 位置变化后立刻输出 -10000，而第一次采样后的力应该保持到下一采样点。

建议：为每效果缓存采样时刻和力/所需指标，统一实现 Sample Period；继续保持设备整体 500–1000 Hz 计算与 500 Hz 通信，不能把粗采样效果误当作整体性能降频。

**11. [P2] sustain magnitude=0 时合法包络输出被抹掉**

位置：[ffb_force_calculator.cpp:274](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:274)。

依据：PID §5.2，印刷页 11–12；[DIENVELOPE:5716](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:5716) 和 [DIPERIODIC:6060](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:6060)。AttackLevel 是相对 baseline 的独立幅值，Magnitude 是 sustain 幅值。

正弦 sustain=0、phase=9000、AttackLevel=4000、AttackTime=100 ms，在起始点应输出方向上的 4000。代码把 baseMag 替换为 10000 避免除零，再将原始零幅波形乘以包络系数，实际永远为 0。

建议：直接计算包络后的幅值，再乘单位波形；不要用“原波形 × 比例”实现零基准边界。复核恒力、斜坡的对应零基准边界。

**12. [P2] 无限周期效果数小时后丢失相位精度**

位置：[ffb_force_calculator.cpp:28](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:28)、[ffb_force_calculator.cpp:47](/Volumes/External/SunFFBJoystick/src/ffb_force_calculator.cpp:47)。

依据：PID §5.1.1 的无限 duration 和 §5.5 的周期/相位定义；[Effect Playback:1406](/Volumes/External/SunFFBJoystick/doc/directinput-docs/directinput-single.html:1406)。

周期计算将持续增长的 uint32_t elapsedTime 转为 float。单精度超过 2^24 ms（约 4.66 小时）不能表示每一毫秒；正弦还对大角度求 sinf。周期 10 ms、幅值 4000，在 elapsed=5 ms 和 16777215 ms 的周期相位应相同，实际输出分别为 0 和 -313。

建议：先用整数计算 `elapsedTime % period`，再转换为 float，避免大角度与大时间浮点运算。加入长时运行和 millis 回绕测试。

**已排除的误报与未实测范围**

- PID §5.13（印刷页 23）明确 DC Reset 清除效果、解除暂停并启用执行器；当前 reset 启用执行器符合该 HID 定义。DirectInput DISFFC_RESET 的 API 文档则描述禁用执行器。不能将 API 常量的语义或坐标格式标志直接套在 HID 报告字节上；应抓取 Windows 驱动输出后再判断驱动兼容问题。
- envelope 与 type-specific 数据已分离，条件效果拒绝 envelope；这与 PID §5.1.1.1 的块类型规则一致。
- 位置更新、力计算与 UART 输出的周期配置，LCD 默认关闭，都不能替代实机时序验收。本轮没有烧录、使能电机或抓取 Windows USB 数据；未对 S2/S3 作硬件异常/性能保证。
- 静态审阅覆盖固件、HID 描述符、Python 参数构建、GUI 力模型及既有回归预期。复现为原生双轴计算和处理逻辑，不模拟 FreeRTOS/USB/电机端。

**复现记录**

临时目录：`/tmp/sunffb-spec-review/`，不在项目源代码中。测试入口为 PlatformIO：

```sh
pio test --project-conf /tmp/sunffb-spec-review/platformio.ini -e native-spec-review-sanitized -v
```

用例有意断言规范预期，因此失败就是复现证据，不能称为“测试通过”。结果：15 个用例，14 失败、1 通过（HID Reset）。保存的 [详细日志](/Volumes/External/SunFFBJoystick/doc/reviews/pid-directinput-review-results.log) 包含实际数值和 UBSan 非对齐读取。

建议修复顺序：内部对齐 → 条件单位统一 → PID 状态报告 → 方向编码 → 触发生命周期 → 其余条件/包络/长时计算边界。修复时同时纠正已有测试预期，而不是仅让现有 156 次测试继续通过。
