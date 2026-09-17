# T384 RAW16 正式双核工程

用户已确认B方案，当前唯一目标为设备持续输出完整帧。首版链路：

```text
MINI2 -> V5F DVP/DMA共享暂存块 -> DTCM完整帧
      -> V3F现有分块协议/TCP COPY -> USB NCM -> 浏览器完整帧
```

V3F先启动并初始化共享控制结构、时钟和网络，从`0x30000`唤醒V5F。
V5F使用仓库已有WCH V5F startup和自己的向量、ITCM代码、DTCM数据。
启动先由V5F写DTCM探针、V3F读回确认；未确认时不启用采集。
V3F持续服务USB/HTTP，不进入Petros示例的STOP/WFE等待；共享状态加内存屏障
完成握手和帧所有权交接，不依赖HSEM通知才能消费。

当前双帧链路保持每帧独立的 `FREE -> FILLING -> READY -> READING -> FREE`。
只有完整行数、字节数和物理帧结束均通过才发布READY。V3F发送上一帧时V5F可采集
另一帧；两帧均占用时才从物理帧起点丢弃。producer abort只回收自己的FILLING帧，
不能修改另一帧的READING/READY数据。消费者按序号（含uint32回绕）读取。
原厂读表借用原来的连续DTCM帧，但同时保留两个bank，协议/RPC取消逻辑不变。
两核IPC版本2，必须使用新两核合并镜像，不能混用旧单帧核。

用户当前验收要求：完整帧≥25FPS且持续流/重连稳定。输入约30FPS，整帧跳号明确
报告；跳号不是半帧，也不把source FPS当stream FPS。严格零丢帧bench仍可用于
零丢帧要求，稳定性工具并不声称达成零丢帧。

## 双帧内存账本

| 用途 | 地址 | 容量 |
|---|---|---|
| V5F代码 | 0x200A0000 | 32KiB，真实highcode须小于此值 |
| 帧1分段A（ITCM） | 0x200A8000 | 96KiB |
| V5F loader | 0x200C0200 | 256B（前512B保留） |
| 帧0（连续DTCM） | 0x200C0300 | 216KiB |
| V5F数据/堆 | 0x200F6300–0x200FAFFF | 19.25KiB，堆至少8KiB |
| 帧1分段B（DTCM） | 0x200FB000 | 18KiB |
| V5F栈 | 0x200FF800 | 2KiB |
| V3F代码 | 0x20100000 | 150KiB |
| 帧1分段C（共享） | 0x20125800 | 42KiB |
| V3F loader | 0x20130000 | 256B |
| V3F数据/堆 | 0x20130100–0x2016CFFF | 243.75KiB，堆至少32KiB |
| 帧1分段D（共享） | 0x2016D000 | 60KiB |
| DVP DMA双块 | 0x2017C000 | 12KiB |
| 两核IPC | 0x2017F000 | 1KiB，当前944B |
| 保护预留 | 0x2017F400 | 1KiB |
| V3F栈 | 0x2017F800 | 2KiB |

帧1合计96+18+42+60=216KiB，所有物理边界按6144B块对齐。总共432KiB帧容量，
不新增片外RAM，不将V5F指令缓存计入可分配空间。DMA仍只写共享暂存，V5F复制
到帧区；启动时V5F在连续帧及四个分段写不同探针，V3F全部读回才允许采集。
256 profile的软件回归使用96KiB帧；帧1只使用ITCM前96KiB，其余分段仍按384预算保留。

真实新目标map：V3F堆余35176B，V5F16556B，IPC944B。两个目标的size工具会
分别列出重叠NOLOAD共享预留，不能将两核bss简单相加当物理占用。每次改动都须
重查map/HEX入口、运行代码容量、四段地址/大小、堆边界及Merge.bin一致性。

Flash仍为V3F184KiB、标定槽0x2E000/0x2F000、V5F起点0x30000/128KiB。
合并BIN空洞填FF，不能保证下载保留已保存标定数据；禁止Erase All/Clear CodeFlash。

## 构建与实测

正式MRS入口 `T384-RAW16-BENCH.wvsln`。已在Windows/MRS2.5.0使用WCH xPack
GCC12.2.0实际构建两核；源码迭代不反复Clean。MRS首次生成两核makefile后，可以
在Windows项目目录运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_dualcore.ps1`，
脚本调用已安装工具链构建、合并并检查，不下载/擦除。V3F/V5F独立bin分别在其obj，
实际两核镜像为 `V5F/obj/Merge.bin`。

主机回归 `bash tools/check_raw16_bench.sh`，两核门禁 `python3 tools/check_dualcore_artifacts.py`。
设备[诊断](http://192.168.17.1/diag)与[成像页](http://192.168.17.1/)沿用原地址；
新双帧身份 `mini2-dvp-v5f-double-frame-v2`，frame_banks=2，booted/initialized/dtcm_access均1。
HTTP连接关闭隔离和静态窗口补发已在单帧实板通过60秒及3次重连：约10FPS、恢复82–88ms。
当前双帧版已主机/目标构建验证，≥25FPS的实板效果尚待下载后测试。

原生Windows运行 `py -3 tools/t384_stream_stability.py --duration 60 --reconnects 3`，
通过后 `py -3 tools/t384_stream_stability.py --duration 600 --reconnects 10`。
默认要求连续窗口完整帧≥25FPS，帧间隔≤2秒、同步diag成功、CRC/半帧/重复逆序为0，
关闭流后根页/diag恢复≤2秒。结果保存 `out/stability/latest.json`；只能按真实结果
报告稳定性，不替代手机、测温、运行时栈水位或24小时验证。

## 单核迁移前记录（历史，不能用于当前下载）


本目录是T384唯一正式开发固件工程；已验证阶段副本保存在
`tests/ch32h417 t384 raw16 bench/`，NCM保护基线保存在`tests/ch32h417_t384_ncm/`。
它不再由 HTTP 临时生成测试字节，而是按产品数据流运行：

```text
采集适配器 -> 有界 RAW16 分块队列 -> 版本化分块协议
           -> lwIP/TCP -> USB NCM -> 浏览器完整帧重组/伪彩
```

当前正式 V3F 构建通过 `Common/Raw16/t384_raw16.h` 中唯一的
`T384_RAW16_PROFILE` 宏选择规格，默认值已于2026-09-16切为 `384u`，实际模组PN=WN2384、FW=00.00.07.01，完整成像尚未通过。已识别的 `Camera WN2256` 在 `256u` 档使用
`Common/Raw16/t384_frame_source_mini2.c` 的 MINI2 DVP bring-up 适配器：DVP
按256×192、50Hz、每行512字节接收并统计帧/行/FIFO事件。DVP先写固定双行sink，
ISR把完成行复制到8行/4096字节pipeline槽，完整帧经`/raw16.stream`发布；队列或
FIFO异常时丢弃当前帧。`/diag`的`source.kind`为
`mini2-dvp-y16-picture-v7`，并暴露`dvp.*`计数、当前模式及控制诊断。将该宏切换为 `384u` 即针对
WN2384/T384 自动变为384×288、探测器60Hz/DVP输出30FPS和768字节/行；RAW16、pipeline、HTTP头和
浏览器均从同一规格派生。

v6的384固定长度DMA接收已上板：每帧36个8行/6144B完成事件，55帧均288行/221184B、ROI valid1，但队列满全丢弃后源停止，本地重启71→209仍无新帧，不能把行数改善写成成像闭环。接收方式依据WCH RM V1.6第29章：JPEG接收位令COL_NUM成为块长，不编码JPEG、不改变Y16或私有帧流；硬件BUF_TOG选完成块，VSYNC结束校验。256逐行路径保留。

v7仅384在500ms无事件的恢复路径增加实时0x86/0x85回读；有效回执且模式与最后确认域一致才重发现有易失0x46启流命令，至少4秒才允许再次写入。应用期间只读查询，不重置应用窗口；后续输出1/1/目标FPS及模式回读才开启DMA，ACK丢失亦不得代替实际确认。正常连续输入不触发，读表期间禁止触发，无0x44/0x45/0x49或校准写入；诊断新增module_probes/module_rearms/restart_ifr。故障探测一次最坏约1.5秒UART等待，可能短暂影响HTTP响应，未上板验证。11:51:04 v6 map余量35160B不替代v7新map。

2026-09-17初始化修复将探测器帧率与数字输出帧率分离：查询已为原生帧率时跳过0x44，旧诊断键`mini2.control_detector30_status=4`表示未发送，而不是成功ACK。通信失败有界重试；有效旧数字状态在2秒窗口内每50ms重查，开流必须回读确认数字DVP enabled/format/fps和数据模式；迟到的无正文ACK不能当成查询成功。诊断新增`mini2.detector_target_fps`/`mini2.dvp_target_fps`，384分别60/30，256均50。v5控制回读已实机通过，不等于完整成像通过，也不承诺384全量RAW16 60FPS吞吐或正式测温。

历史v3真实复验仍503：DVP设置收到成功ACK但数字输出回读全0，TPD尚未发送。v4依据本地原厂SDK实际callback，将0x86查询从旧4字节改为3字节（status/format/fps），启动与确认统一专用构造函数；诊断`mini2.digital_query_bytes=3`证明新配置。新增SDK设置/查询对照测试，不能以假UART与自身实现一致替代原厂契约。

切换示例：在 `Common/Raw16/t384_raw16.h` 将
`#define T384_RAW16_PROFILE 384u` 改为 `256u` 可回到256档，改回 `384u` 可重新选择384档；每次都需重新Build/烧录。不要再分别修改
宽度、高度或网页常量。256 档是当前 WN2256 实测档。384 档目前仅完成主机语法和
协议/主机 smoke 适配，真实 WN2384 的 DVP、RAM 余量和持续吞吐仍需单独验证。

主机完整帧工具及黑体采集按HTTP头适配256/384；384的温度模型保持不可用，直到取得该模组自己的有效标定数据。原厂只读表工具默认校验当前模组的PN/SN/FW、事务与CRC；WN2256已知SHA首验改为显式选项。现有桌面Scope支持384完整帧及实验二点采集，但保存/回读/应用仍需实机验收；原厂内置两点动作尚未接入本固件。

启动时通过最终板 UART0（CH32 `USART4`，PF4=TX、PF3=RX，经U7）按资料顺序发送
“关闭数字输出”“关闭模拟输出”“开启当前规格 DVP 帧率”，再用易失 `0x45` 请求
TPD/Y16 并以 `0x85` 回读确认；每条命令均等待通用回执并记录校验/状态。若 TPD
设置或确认失败，则先请求 Picture/UYVY，再重申 DVP 输出并回读确认；两条路径均失败
才保持流门控。全程不发送 `0x49` 保存命令，因此断电后不会永久修改机芯配置。

主机传输回归仍可显式定义 `T384_FRAME_SOURCE_SIMULATOR=1`，启用
`Common/Raw16/t384_frame_source_sim.c` 的 DMA 等效或 CPU 模拟路径；它先生成
一个 8 行 RAW16 场景种子，再用 CH32H417 DMA1 内存到内存搬运到每个分辨率对应的
队列槽。该模式只验证帧边界和吞吐，不代表真实 MINI2 像素内容；`/diag` 的
`source.kind=dma-equivalent-dvp-source-v1` 明确标记了这一点。

当前已接入 WN2256 真实 MINI2 DVP，用同一 `t384_frame_source.h` API 将
逐行 DMA 结果提交给 `t384_frame_pipeline.h`；下游队列、NCM、HTTP、浏览器和
诊断不需要改变。

WN2256 DVP 行完成路径在 TPD/Y16 模式下同时被动统计每个完整帧中心 `(120,88)` 起的 16×16
ROI。该统计只保留两种字节序解释的和、平方和、样本数及极值，不保存像素或帧，也不发送
机芯命令；其完整性判定与 pipeline 发布独立。`/diag` 的 `roi.*` 输出最新完整帧
序号、有效标志、该帧是否发布以及 BE16（兼容字段 `mean_raw_x100`）和 LE16 的
原始 count 均值/总体标准差（`×100`）及极值。这些值不是摄氏温度；Picture/UYVY
回退时 ROI 明确无效，避免把色度/亮度字节误当温度数据。

## 内存与所有权

- 256沿用逐行DVP接收，一行为`512 B`；384使用固定长度接收，一次完成`8×768=6144 B`，`dvp.row_events`记录完成事件而非物理行数，需结合`dvp.dma_block_rows`解释。
- 256 档流水线在采集适配器内按8行聚合为`4096 B`，固定24个32字节对齐
  payload槽，共`98304 B`，可完整容纳一帧；384 档使用10个`6144 B`槽，加两个
  同尺寸DMA暂存块，合计`73728 B`，不保存整帧。相较旧384队列12槽加1536B双行sink，预算未增加；实际总BSS及32KiB栈前余量需新map核对。
- 256 档仅当pipeline在帧开始时完全空闲才接纳该帧，否则从首行起整帧丢弃。
- 所有权顺序固定为：source/DMA 写入 -> pipeline ready -> HTTP 只读 lease ->
  `tcp_write(COPY)` 完成排队 -> release。
- 模拟适配器遇到队列满时暂停当前物理帧，等待消费者释放槽后继续，不把无法
  结束的半帧发布给浏览器；真实 DVP 若不能暂停采集，必须在 source adapter
  内完成整帧丢弃/源端暂存，不能让半帧进入正式队列。队列永不覆盖未消费数据；
  浏览器仍根据帧序号和起止标志丢弃异常帧，不把撕裂数据当成画面。
- 设备只保存这段固定 RAM 队列和累计计数，不把帧或历史写入 Flash/文件。

## 线上格式

`/raw16.stream` 使用 `T384-FRAME-CHUNK-V1`：每个分块是 36 字节小端 envelope，
随后是256档最多4096字节、384档最多6144字节的 payload。默认 TPD 模式为高字节在前的 Y16；若 TPD 设置或
`0x85` 回读失败，则安全回退 Picture/UYVY。envelope 的 flags 和 pixel-format 字段均标记
实际模式，HTTP 头同步给出模式、像素格式和温度模型。envelope还包含版本、帧序号、帧内偏移、
长度、尺寸、采集时间、起止/来源标志和 CRC-16/XMODEM。任何像素都不再
被 magic、尺寸或序号污染。

启动只发送易失的 `0x45` 模式切换和 `0x46` DVP输出命令，不发送会保存数字/模拟
出图格式的 `0x49`。网页只在 Y16 模式显示实验线性温度；Picture 回退仅显示伪彩。

该协议是为 T384/T640 的有界分块流水线建立的 V1 接口，旧版“裸帧且前 8 像素
是元数据”的页面和脚本不兼容；本工程的内嵌页面与验收工具已同步升级。

## 真实 MINI2 替换边界

保持不变：

- `t384_frame_pipeline.*`
- `t384_raw16_wire.*`
- `http_status.c`、NCM/lwIP/TinyUSB
- 浏览器分块重组、伪彩、FPS、手动本机记录

替换/新增：

- 用 `t384_frame_source_mini2.c` 实现 `t384_frame_source.h`。
- 真实适配器在启动 DMA 前获取 pipeline 槽，确认 8 行 DMA 完成后再提交；当前
  WN2256 bring-up 在 DVP ISR 中完成单行复制和有界分块提交，后续高吞吐适配器如
  移到 `t384_frame_source_task()` 仍须保持同一所有权顺序。FIFO 溢出或帧长不符时，
  必须先停 DMA，再 abort 当前帧。
- 依据 MINI2 实测冻结字节序、PCLK 采样沿、H/V 极性、消隐/信息行和电平。

当前工程证明的是 V3F 同核“数据进入 RAM 之后”的正式链路。现有资料未证明
V5F DMA 可直接写入 V3F 数据 RAM；若最终硬件强制 V5F 采集，仍须先验证跨核
可见内存、屏障/cache 和通知机制，再在 source 适配器内部增加 IPC。不能把这一
硬件前提写成已经完成。

## 历史单核MRS工程（不适用于当前正式工程）

- 解决方案：`T384-RAW16-BENCH.wvsln`
- 只构建/烧录：`V3F/T384-RAW16-BENCH_V3F.wvproj`
- 固件产物：`V3F/obj/T384-RAW16-BENCH_V3F.hex`

当前工程名和下载目标均为 T384，MRS 芯片项为 `CH32H417WEU`，对应目标器件
`CH32H417WEU6`；`Erase All` 与 `Clear CodeFlash` 均关闭，只构建/下载 V3F。
构建和烧录由用户在 MRS 完成，每次工程元数据变化后必须重新 Clean + Build。

`.wvproj/.project` 的linked folders全部使用相对路径。`.mrs/`和`V3F/obj/`包含
MRS生成的机器本地路径，不属于可迁移源码；移动工程后只清理一次旧缓存，普通源码或
头文件修改直接增量Build。

## 验证

静态/协议/纯像素/队列所有权检查：

```bash
bash tools/check_raw16_bench.sh
```

2026-09-17网络迁移版本烧录并重新获取DHCP后访问 `http://192.168.17.1/`；旧固件仍为 `.18.1`。迁移未上板验证。页面显示真实 source FPS、浏览器
完整帧 FPS、序号缺口、丢弃帧、处理耗时和队列水位；进入页面不会自动记录。

严格 30 秒闭环：

```bash
python3 tools/t384_raw16_bench.py --duration 30 --min-mb-s 7
```

工具逐分块校验 envelope CRC、帧序号和偏移，并以浏览器同等规则只统计完整帧
有效负载；检测到 synthetic source 时还校验全部 RAW16 像素，换成真实 source 后
自动切换为帧完整性校验，也可用 `--expect-source` 强制口径。通过条件是完整帧
持续不少于 `7.000 MB/s`、无序号缺口、无不完整帧。工具只复用一个期望帧和
一个分块缓冲，不写磁盘历史。
