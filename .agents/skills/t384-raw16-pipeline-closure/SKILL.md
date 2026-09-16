---
name: t384-raw16-pipeline-closure
description: 接手和闭环 T384 的 384×288 RAW16 从 DVP/DMA source、固定分块 ring、lwIP/TCP、TinyUSB USB NCM 到浏览器完整帧的真实吞吐、背压、内存和稳定性。用户提到 RAW16 只有 13/19/22 FPS、目标 25/25.5 FPS 或 7 MB/s、source FPS 与 stream FPS 不一致、画面 0 FPS、pipeline 满、NCM/HTTP backpressure、模拟 MINI2/DVP、真实板到货后只换采集接口、持续 10 分钟/24 小时或要求 30 秒 HANDOFF 时必须使用。
---

# T384 RAW16 端到端闭环

## 边界与开工

- 当前唯一主线是 `source → 有界 pipeline → RAW16BE-CHUNK-V1 → lwIP/TCP → TinyUSB NCM → USBHS → 浏览器完整帧`。36 字节 envelope 保持小端，像素 payload 按高字节在前解释；模拟硬件时只替换 source，不建立 HTTP 造数旁路。
- 正式开发工程为 `firmware/`；已验证阶段工程为 `tests/ch32h417 t384 raw16 bench/`，NCM/OV2640保护基线为`tests/ch32h417_t384_ncm/`。不要把备份工程放回`firmware/`。
- 用户负责 MRS 2.5.0 的 V3F增量Build/烧录。只有芯片项、linker、工程资源或目录迁移后才需要一次Clean/缓存重建；不要把Clean变成每轮仪式。代理修改源码、运行主机检查并直接读取设备；不把主机GCC写成目标构建成功。
- 正式与验证 RAW16 工程使用 `CH32H417WEU` 且只构建/下载 V3F；保持 `Erase All=false`、`Clear CodeFlash=false`。`tests/ch32h417_t384_base/` 的 QEU 元数据只是厂商参考，不能作为目标板下载工程。
- 依次完整读取 `AGENTS.md`、`HANDOFF.md` 顶部 30 秒恢复、`docs/runbooks/PROJECT_MEMORY.md` 顶部、`README.md`、`/home/slam/Sipeed/C_context/KNOWN_FAILURES.md`。公共 preflight 没有 T384 项时记录限制，不冒用其他项目名。
- 每轮只改变一个吞吐变量。协议、USB 描述符、buffer ownership、zero-copy 或跨核接口属于高风险改动，先说明兼容性和验证计划。
- 正式与验证工程必须可独立迁移：`.wvproj` linkedFolders用`../...`，`.project`用`PARENT-1-PROJECT_LOC/...`，`.wvsln/.cproject`不得写盘符/绝对工作区路径。`.mrs/`会被IDE自动写入机器本地路径，和`V3F/obj/`一样属于生成缓存；迁移时丢弃并重新生成，不手改其中的workspace/make/dependency文件冒充修复。
- 根目录 `designs/` 禁止创建。设计原型、设计工具和 Skill 生成物都放入 `docs/design/` 的明确子目录；不能用 `.gitignore` 掩盖违规目录。
- 开始修改或提交前先检查工作区，保护用户未提交内容。用户指定 docs 不提交时，只纳入代码/工程配置；Skill 不自动暂存、commit 或 push。

## 固定口径

- 384×288 RAW16：`221184 B/frame`。
- 25 FPS：`5,529,600 B/s`；25.5 FPS：`5,640,192 B/s`。
- 7 MB/s 压力门约 `31.648 FPS`，不是产品 25 FPS 的同义词。
- 页面用于即时观测；严格工具用于持续时间、帧边界、序号和错误验收。
- 当前设备入口为 `http://192.168.18.1/` 和 `http://192.168.18.1/diag`。
- DMA 等效 source 使用 non-synthetic wire flag 以关闭合成像素断言；这不等于真实 MINI2。判断来源以 `/diag source.kind=dma-equivalent-dvp-source-v1` 为准，不能仅凭严格工具的 `real` 标签下结论。

## 闭环工作法

按 `目标 → 状态 → 误差 → 控制动作 → 反馈 → 修正 → 验证 → 沉淀` 推进。

### 建立真实状态

活动流期间读取 `/diag`，记录 source 的 kind/fps/published/drop，pipeline 的 queued/high-water/completed/aborted/no-slot/protocol-error，stream 的 fps/payload/backpressure/error/timeout，以及 NCM RX/TX backpressure/drop。把页面 FPS、设备计数和严格工具分开写；持续结论至少 60 秒。

- 用户确认“连续成像”后，先保留当前代码，复位并只开一个流做活动窗口前后差分；累计 dropped 可能包含无流期间丢帧，不能直接判定当前活动流失败。

### 规格切换护栏

- 正式规格由 `firmware/Common/Raw16/t384_raw16.h` 的单一 `T384_RAW16_PROFILE` 选择：`256u` 为 WN2256（256×192/50 FPS），`384u` 为 WN2384 目标（384×288/30 FPS）。不得散改宽度、高度、DVP 帧率、行字节、pipeline 或网页常量。
- 浏览器必须从 HTTP 头读取实际宽高、帧大小和分块上限；切换 profile 后网页不应因固定 256 常量拒绝合法 384 流。
- 256 档 24×4096 B 可容纳一整帧；384 档当前 12×6144 B 小于一整帧，profile 切换后必须重新做 map/RAM 余量和真实 DVP 缓存评估，不能把“编译通过”写成 384 实机完成。

### 分层定位

- source 低且队列不满：查采集节拍、DMA 完成、CPU 生成、DVP 行/帧时序。
- source 高、stream 0：先查 FRAME_START/END、偏移连续性和半帧 abort，不先怪浏览器。
- pipeline 经常满/no-slot 高而 NCM TX 无 drop：下游消费受限；依次查 TCP COPY/checksum、ACK 节拍、窗口、NCM 聚合和浏览器 reader 反压。
- NCM tx_backpressure/drop 高：再查 NTB 数量/尺寸、flush、USBHS burst/DCD；未出现时不要先扩大 NCM RAM。
- 浏览器处理时间高但设备 stream 足够：优化 parser/render；设备 stream 本身不足时，UI 优化不是根因。

`ncm.rx_busy_drop` 表示 TinyUSB callback 因单槽忙而延后，不自动等同真实丢包。读第三方栈 callback/renew 语义后再修改。

### 优化优先级

按证据只选择一个变量：

1. 去除 source 的 CPU 测试图生成，但保持真实 DMA/pipeline 交付语义。
2. TCP checksum/copy：先验证 `LWIP_CHECKSUM_ON_COPY`；收益不足再做有参考实现对照的融合 copy+checksum。
3. TCP 窗口/segment 数：只有窗口或 ERR_MEM 证据支持时扩大，并用新 map 核对 RAM。
4. zero-copy 最后做。pipeline slot 必须保持到远端 ACK；覆盖部分 ACK、重传、断连和超时回收测试后才实施。
5. NCM NTB、MSS/MTU、USB 描述符涉及主机兼容或大 RAM，无直接证据不改。

永不关闭 TCP/IP checksum，永不通过丢帧、跳过完整帧验证或虚报 source FPS 达标。

### 缓冲与真实 DVP 护栏

- ISR/DMA 路径不阻塞、不动态分配、不长日志；commit 前 DMA 完成，abort 前 DMA 停止。
- 模拟源可在无槽时暂停；真实 DVP 若不可暂停，必须按整帧边界丢弃或使用经 map 证明可容纳的缓冲，禁止发布半帧。
- 真实 MINI2 source adapter 应保持 queue/wire/NCM/browser 不变；若 V5F→V3F 跨核不可避免，先验证共享 RAM、cache、一致性、屏障和通知，再封装进 source 层。
- 扩大 lwIP、TCP、pipeline 或 NCM RAM 后检查 map：`_ebss` 到固定栈至少保留 32 KiB，并确认无 OV2640 Camera 缓冲混入 bench。

## 验证阶梯

1. 运行 `bash tools/check_raw16_bench.sh`。它同时检查正式与tests验证工程的相对路径、源码/JS/smoke和可用map；发现迁移前`.d`时先丢弃生成缓存。
2. 普通源码修改由用户增量Build后直接烧录V3F HEX；只有内存/工程配置变化才额外核对map、目标名和RAM余量。
3. 烧录后先用 `/diag` 证明新身份/行为进入固件，再测 Windows 60 秒。
4. 25 FPS 通过标准：完整帧 `≥25 FPS`、payload `≥5,529,600 B/s`，序号缺口、不完整帧、source drop、pipeline abort/protocol error、HTTP write error/timeout、NCM TX drop 均为 0。
5. 通过后做 10 分钟和断连恢复。24 小时前先修 DHCP T1/T2 续租并覆盖主机休眠恢复；否则禁止声称 24 小时稳定。

## 汇报与交接

每轮列出目标、现状数字、差额、唯一控制变量、静态/MRS/烧录/实测层级和未验证项。用户要求交接时替换 `HANDOFF.md` 顶部 30 秒恢复区，记录最新已烧录身份、活动 `/diag`、已证伪方向、下一单变量和通过标准；稳定偏好与坑写入 `docs/runbooks/PROJECT_MEMORY.md`。

代码审查时不要把正式工程与 `tests/` 验证快照的重复当作待抽取代码；独立快照用于迁移和阶段复现。源码时间晚于 map 只说明 map 不能证明当前源码，纯注释或格式 churn 也会触发门禁，因此只为有价值的实现变化触碰编译源码。

## 测温/标定接手补充

- 当前阶段可直接使用 WN2256 256×192 验证；不要因为正式 SKU 是 T384/T640 就阻塞当前黑体和数据域验证。
- 原厂 USB 控制、Windows UVC、Linux libusb、Linux 定制 V4L2 `/dev/cmdX` 和 VMware/usbipd 是不同层。先验证设备 namespace、节点和 SDK 后端，再判断标定文件是否存在。
- 原厂 SDK 预编译库的 Windows 运行依赖必须包含 `/MD`、`advapi32.lib`、SDK DLL 和 `pthreadVC2.dll`；`LNK4098`通常是非致命警告，`0xC0000135`才表示运行时DLL缺失。
- VMware/usbipd 可成功传控制命令但不保证UVC等时视频；黑体采集脚本必须校验每帧字节数，0字节或不完整帧立即失败，不能生成假manifest。
- `pico_tn160`只迁移标定事务、generation、CRC、calibration_id、双槽和fail-closed结构，不迁移其ADC/NTC/TN160公式。
- 0°C/50°C二点拟合只能标记`experimental`；正式OEM链仍要求数据域确认（Y16→Y14→SNR/NUC→KT/BT→NUC-T）和表身份绑定。实验模型必须限定profile/条件并提供回退。
- 完成黑体采集后记录ROI均值、帧间漂移、Vtemp、FFC、gain、PN/SN/FW和source/stream计数；先检查热稳定和FFC，再拟合，不把启动漂移带入模型。
