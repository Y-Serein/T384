# T384 RAW16 正式流水线工程

本目录是T384唯一正式开发固件工程；已验证阶段副本保存在
`tests/ch32h417 t384 raw16 bench/`，NCM保护基线保存在`tests/ch32h417_t384_ncm/`。
它不再由 HTTP 临时生成测试字节，而是按产品数据流运行：

```text
采集适配器 -> 有界 RAW16 分块队列 -> 版本化分块协议
           -> lwIP/TCP -> USB NCM -> 浏览器完整帧重组/伪彩
```

当前正式 V3F 构建通过 `Common/Raw16/t384_raw16.h` 中唯一的
`T384_RAW16_PROFILE` 宏选择规格，默认值 `256u` 针对已识别的 `Camera WN2256` 使用
`Common/Raw16/t384_frame_source_mini2.c` 的 MINI2 DVP bring-up 适配器：DVP
按256×192、50Hz、每行512字节接收并统计帧/行/FIFO事件。DVP先写固定双行sink，
ISR把完成行复制到8行/4096字节pipeline槽，完整帧经`/raw16.stream`发布；队列或
FIFO异常时丢弃当前帧。`/diag`的`source.kind`为
`mini2-dvp-y16-picture-v2`，并暴露`dvp.*`计数、当前模式及控制诊断。将该宏切换为 `384u` 即针对
WN2384/T384 自动变为384×288、30Hz和768字节/行；RAW16、pipeline、HTTP头和
浏览器均从同一规格派生。

切换示例：在 `Common/Raw16/t384_raw16.h` 将
`#define T384_RAW16_PROFILE 256u` 改为 `384u` 后重新 Build；不要再分别修改
宽度、高度或网页常量。256 档是当前 WN2256 实测档。384 档目前仅完成编译和
协议/主机 smoke 适配，真实 WN2384 的 DVP、RAM 余量和持续吞吐仍需单独验证。

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

- DVP参考实现可证实的硬件完成粒度是一行；当前256 RAW16一行为`512 B`。
- 256 档流水线在采集适配器内按8行聚合为`4096 B`，固定24个32字节对齐
  payload槽，共`98304 B`，可完整容纳一帧；384 档沿用12个`6144 B`槽，容量
  为`73728 B`，不能仅凭编译结果宣称已具备完整帧缓存。
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
随后是当前最多4096字节的 payload。默认 TPD 模式为高字节在前的 Y16；若 TPD 设置或
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

## MRS 工程

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

烧录后浏览器直接访问 `http://192.168.18.1/`。页面显示真实 source FPS、浏览器
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
