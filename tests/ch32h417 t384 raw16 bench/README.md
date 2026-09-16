# T384 384×288 RAW16 正式流水线验证工程

本目录保存已通过 `27.750 FPS / 6.138 MB/s` 60秒严格验收的RAW16阶段工程；
正式开发工程位于`firmware/`。
它不再由 HTTP 临时生成测试字节，而是按产品数据流运行：

```text
采集适配器 -> 有界 RAW16 分块队列 -> 版本化分块协议
           -> lwIP/TCP -> USB NCM -> 浏览器完整帧重组/伪彩
```

当前唯一模拟部分是 `Common/Raw16/t384_frame_source_sim.c`。硬件构建默认打开
`T384_FRAME_SOURCE_DMA_EQUIV=1`：它先生成一个 8 行 RAW16 场景种子，再用
CH32H417 DMA1 内存到内存搬运到每个 6144 B 队列槽，目标有效像素速率为
`7.2 MB/s`。这样只去掉 CPU 逐像素填充，队列、协议、HTTP、NCM、浏览器和
诊断保持不变，用来隔离“源生成”是否是 19.2 FPS 瓶颈。种子会在每个分块重复，
因此该模式只验帧边界和吞吐，不代表真实 MINI2 像素内容；`/diag` 的
`source.kind=dma-equivalent-dvp-source-v1` 明确标记了这一点。主机 smoke test
在 `T384_HOST_SYNTAX_CHECK` 下仍使用 CPU 模拟路径。

真实 MINI2 到板后，用同一 `t384_frame_source.h` API 实现 DVP 适配器，由它把
逐行 DMA 结果提交给 `t384_frame_pipeline.h`；下游队列、NCM、HTTP、浏览器和
诊断不需要改变。

## 内存与所有权

- DVP 参考实现可证实的硬件完成粒度是一行；384 RAW16 一行为 `768 B`。
- 流水线在采集适配器内按 8 行聚合为 `6144 B`，固定 12 个 32 字节对齐
  payload 槽，共 `73728 B`；另有 12×16 B 带外元数据，不污染 DMA payload。
- 所有权顺序固定为：source/DMA 写入 -> pipeline ready -> HTTP 只读 lease ->
  `tcp_write(COPY)` 完成排队 -> release。
- 模拟适配器遇到队列满时暂停当前物理帧，等待消费者释放槽后继续，不把无法
  结束的半帧发布给浏览器；真实 DVP 若不能暂停采集，必须在 source adapter
  内完成整帧丢弃/源端暂存，不能让半帧进入正式队列。队列永不覆盖未消费数据；
  浏览器仍根据帧序号和起止标志丢弃异常帧，不把撕裂数据当成画面。
- 设备只保存这段固定 RAM 队列和累计计数，不把帧或历史写入 Flash/文件。

## 线上格式

`/raw16.stream` 使用 `RAW16LE-CHUNK-V1`：每个分块是 36 字节小端 envelope，
随后是最多 6144 字节纯 RAW16 payload。envelope 包含版本、帧序号、帧内偏移、
长度、尺寸、采集时间、起止/来源标志和 CRC-16/XMODEM。任何 RAW16 像素都不再
被 magic、尺寸或序号污染。

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
- 真实适配器在启动 DMA 前获取 pipeline 槽，确认 8 行 DMA 完成后再提交；推荐
  ISR 只更新 source-local 完成标志，由 `t384_frame_source_task()` 串行操作队列
  元数据。FIFO 溢出或帧长不符时，必须先停 DMA，再 abort 当前帧。
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

本验证工程保持可独立构建：`.wvproj/.project` 的linked folders全部使用相对路径。
`.mrs/`和`V3F/obj/`只是本机生成缓存，迁移后一次性重新生成，不作为验证源码依据。

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
