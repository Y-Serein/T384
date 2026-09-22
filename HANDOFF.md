# T384/T640 当前交接（2026-09-22）

## 30 秒恢复

**已开始 640 专用架构迁移：V5F 将同时负责 MINI2/DVP、TCP/lwIP、TinyUSB NCM、HTTP；V3F 只保留启动、跨核握手、状态/RPC。当前源码尚未由 MRS 编译、尚未烧录，旧板上约 19.6 FPS 的证据仍属于旧 V3F-network 镜像，不能当作新架构成绩。**

- 正式工程 `firmware/T384-RAW16-BENCH.wvsln`，当前网络数据面配置为 V5F、默认 profile 640；[预览](http://192.168.17.1/)和[诊断](http://192.168.17.1/diag)地址保持不变。V2 帧协议、NCM 描述符、IP 和浏览器接口未改；根页面在 V5F 版改为 gzip Flash 响应以满足 128 KiB 代码窗口。
- 640 v2 每帧线上 331776 B，保留原 8-bit 亮度和块内 U/V；网页还原 UYVY，TCP/IP 校验保留。理论 30/60 FPS 需 9.95/19.91 MB/s 像素载荷，另有协议开销。先前 UYVY 成功窗口约 8.53 FPS、5.59 MB/s；v2 约 20 FPS、6.6 MB/s，但窗口断连。
- 旧镜像最后可复核现场记录（`docs/logs.txt`，2026-09-20）：15.036 s 内 295 完整帧，19.619 FPS，6.509 MB/s；这些数字只用于迁移前基线。
- 最新 `out/stability/640-throughput-latest.json`（2026-09-20 09:47:53 +08 开始）仍记录 12.425 s 后 WinError 10054、`stable=false`、断连后 `/diag` 超时；序号缺口 494，半帧/逆序 0。该文件和上面 17:58–18:03 记录来自旧板上镜像，不能证明当前源码或新产物已上板。
- 新架构关键内存：640 packed ring 64 槽（165888 B）；V5F 网络 heap 32 KiB；NCM/USB 缓冲放共享 SRAM；HTTP/diag 状态放 V5F DTCM；网页 gzip 响应放 V5F Flash-only 段。V5F `TCP_WND=4*MSS` 是为 4 个 RX pbuf 的 lwIP sanity 约束，图像发送方向不变。
- `T384_NETWORK_ON_V5F` 被明确限制为 640 架构实验；256/384 的旧 V3F-network 源码路径未改，但切换 profile 前需恢复对应旧工程/linker 配置。
- 2026-09-22 首份日志的 `stream.connects=0` 根因是 V5F NOLOAD 网络 heap 中的模块状态未清零，已加入 `t384_module_files_init()`；随后新日志已证明流能建立（`connects=1`、`frames=6`），但浏览器断开。进一步发现 64 槽迁移把 ring 容量误报成 packed 逻辑帧大小（165888 vs 331776），已修正 wire/HTTP `Frame-Bytes` 并加入 5 秒无进展清理，尚未再次 Build/烧录验证。
- 用户负责 Windows/MRS/下载/上板测试；本轮代理未运行目标编译、未烧录、未 commit/push。工作区仍有用户既有未提交改动；不 reset/覆盖。

## 已尝试及结果

1. 640 SRAM 无损打包 v6 保留；本轮把现有 2592 B/块直接作为 HTTP v2 载荷，网页和主机抓流工具同步识别，实测稳定窗口约 19.619 FPS，仍未达到 30 FPS。
2. 先前只加 NCM TX 诊断的改动曾伴随上板回归，确切原因未证实；该 TinyUSB 诊断代码已撤回，本轮不再动 TinyUSB、USB 描述符、IP 或 TX 时序。
3. 抓流脚本现会在异常时保存 `partial_window` 和尝试断连后诊断；最新断连后的诊断请求超时，保留了 12.425 s 的完整帧统计。
4. USBHS `/diag` 的失败尝试仍不作为证据；当前源码未新增 USBHS 硬件字段，但保留了 TinyUSB NCM 的只读 `xmit_ntb_*` 统计。现有 V3F bin/Merge 含这些字符串，V5F map/HEX 仍旧，必须先双核重建。

## 下一步

1. 在 MRS 打开 `firmware/T384-RAW16-BENCH.wvsln`，先 Build V3F、再 Build V5F；核对新 map 中 V5F `highcode<=128 KiB`、`.t384_net_ncm/.t384_net_heap/.t384_net_http` 均未越界，Merge 同时包含两核。**禁止 Erase All / Clear CodeFlash**。
2. 下载后先打开[诊断页](http://192.168.17.1/diag)，确认 `network.data_plane=v5f`；再开[预览页](http://192.168.17.1/)，确认页面能加载、图像和 diag 同时可用。
3. 只测一个 20 秒窗口：记录完整帧 FPS、`pipeline.acquire_no_slot`、`ncm.xmit_ntb_*`、`tcp.sndbuf`、USBHS 复位/错误；新架构若不枚举或无图，先回退工程配置到提交基线，不改协议。
4. 30 FPS 仍需真实板验证；手机、其他 PC、长时稳定性和正式测温未验证。

## 关键相对路径

- `firmware/Common/App/http_status.c`：V5F HTTP v2 发送/诊断；`t384_ncm.c`、`lwipopts.h`、`tusb_config.h`：V5F NCM/TCP 配置；`device_console_http_gz.inc`：V5F gzip 根页面。
- `firmware/Common/Ld/V5F/Link_v5f_net.ld`：V5F 网络数据面内存分区；`t384_v5f_net_memory.{h,c}`：网络堆/缓冲段属性。
- `firmware/Common/Raw16/t384_packed_picture.h`、`t384_raw16_wire.{c,h}`、`t384_frame_pipeline.h`：打包与帧流；`web/raw16_bench_console.html` 和生成的 `firmware/Common/App/device_console_html.inc`：网页解码。
- `tools/t384_stream_stability.py`、`tools/t384_raw16_bench.py`、`out/stability/640-throughput-latest.json`、`docs/logs.txt`：抓流工具及最新证据。
- `docs/design/usb_net_raw16_throughput_optimization_20260918.md` 第 11–12 节：外部 BL618 思路和更正，不能当 H417 实测。

## 验证状态与未决问题

本轮只做源码/工程/内存布局修改；已做 JSON/XML 读回、gzip 资产完整性核对和 `git diff --check`，未运行 WCH 目标编译、未烧录、未上板。新 map/HEX/Merge、V5F 代码窗口、网络 SRAM 余量、USBHS 枚举、完整帧 FPS 和重连均未验证。

---

# 历史交接（2026-09-18，以下不是当前状态）

## 30 秒恢复

**当前暂停：640 已出图，用户反馈约 10 FPS；TCP 发送缓存扩大后仍约 10 FPS，先保留当前源码，不继续提速。384 标定保存问题用户确认“可以了”；正式 OEM 测温尚未闭环。持续稳定性尚未验收。**

- 正式工程：`firmware/T384-RAW16-BENCH.wvsln`，双核 V5F 采集 / V3F 网络；当前默认 `T384_RAW16_PROFILE=640u`，IPC v6。
- 实机身份：TIFSC640 / FW `01.00.01.03`，原生 640×512 / 60 FPS，Picture 模式 0，输入 YUYV（查询格式 2）；向网页输出既有 UYVY 格式。640 当前仅成像，不提供正式温度。
- 设备地址：[成像页](http://192.168.17.1/) / [诊断页](http://192.168.17.1/diag)。旧文档/skill 中的 18.1 和“仅 V3F”是历史信息，不能照搬。
- 用户负责编译、烧录、上板测试；本轮代理没有运行编译/测试、没有烧录。默认先读 `docs/logs.txt`，简短中文，额度有限但不允许乱改。
- 下次先读本节、`docs/runbooks/PROJECT_MEMORY.md` 最新章节、AGENTS.md 和最新日志；恢复方向由用户决定，别自动继续吞吐优化。

## 当前状态

1. 384：标定写入修复包括独立擦除页判断、Flash 物理地址别名和实验模型校验/应用路径。用户确认可用；重启/断电保存、独立黑体精度及正式 KT/BT/NUC-T 链不能据此标记完成。
2. 640：v6 用 SRAM 保存无损打包数据，128 槽覆盖完整帧；仅在活动 HTTP 消费租约中恢复 UYVY。ITCM 只放元数据，跨核数据使用对齐字访问。没有继续发送不必要的机芯配置命令。
3. 最后提速尝试：`lwipopts.h` 仅 640 的 `TCP_SND_BUF` 从 23360 改为 46720 字节；既有 96 KiB 堆 / 64 队列不变。新增 `http.tcp_send_buffer_bytes`、`stream.sendbuf_stalls`、`stream.write_mem_stalls`。用户最新反馈仍 10 FPS；尚无包含新增字段的活动流日志，不能认定窗口已被精确排除，也不能把该尝试写成提速成功。
4. 当前 `docs/logs.txt` 是 v6 停流后的快照：采集 60 FPS、512 行/655360 字节、坏帧/溢出/中止/协议错误/打包拒绝/NCM TX drop 都为 0；HTTP 完整帧累计 163，背压 9000，`stream.active=0`，瞬时 FPS/BPS 为 0。累计 published 含空闲排队释放，不是网页 FPS。
5. 用户最终产品要求网页设置可选择 384/640。当前仍是编译期选择，网页设备切换尚未实现；不悄悄扩大本轮范围。

## 已尝试及结果

- 原默认尺寸限制导致 640 RAW16 编译错误：已按 profile 整理尺寸和相关前后端路径，用户后来上板出图。
- 640 早期控制命令超时/无图：收敛为 6 个只读身份/模式/格式查询，当前全部有效，无超时；不凭分辨率猜 PN 或照搬 384 控制配置。
- v4：40×5120=204800 字节队列小于 655360 字节整帧，TCP 背压下持续半帧中止，HTTP 字节增长但完整帧为 0。
- v5：把像素放入 ITCM 且空闲消费也反复展开后，Windows 出现 USB 无法识别；撤回该轮修改恢复连接。确切根因未证明，不能归因于 USB 描述符或单独断定 ITCM 故障。
- v6：打包像素仅用 SRAM、ITCM 只放元数据、按读租约延迟展开，用户确认有图约 10 FPS。每块只有 U/V 恒定才接受无损打包；变化色度必须拒绝并计数。
- TCP 发送缓存翻倍：用户仍约 10 FPS，本轮停止继续优化；IP、USB 身份、描述符和流格式没有因此改变。

## 下一步（用户选择后再执行）

1. 若继续 640 提速：播放期间另开诊断页，保存两份有时间间隔的活动快照，先核对 `http.tcp_send_buffer_bytes=46720`、两类背压、完整帧/BPS 差值，再选择一个变量；不能再盲目扩大 RAM。
2. 区分发送窗口/内存队列、COPY/校验、NCM 聚合和浏览器处理成本；保留校验与完整帧验收，不把 60 FPS 源帧率当成果。
3. 若做网页设备选择：先明确连接/选择方式及单固件资源边界，再最小实现运行时选择；当前编译期 profile 不是完成态。
4. 若回到温度：先切回对应 384 profile 并核对 PN/FW/数据域和实验模型绑定，再验证保存/重启/温度；640 Picture 的 8-bit 亮度不能套 384 Y16 测温。

## 关键相对路径

- `firmware/Common/Raw16/t384_raw16.h`：编译期 profile。
- `t384_frame_source_mini2.c`、`t384_frame_pipeline_full.c`、`t384_frame_pipeline.h`、`t384_packed_picture.h`、`t384_dualcore.h` 均在 `firmware/Common/Raw16/`：640 采集、打包、队列、IPC/内存布局。
- `firmware/Common/App/http_status.c` / `lwipopts.h` / `t384_product_config.h`：消费租约、诊断、发送缓存、真实 IP。
- `firmware/Common/Ld/V5F/Link_v5f.ld`、`tools/check_dualcore_artifacts.py`：分区与产物边界检查。
- `firmware/Common/Raw16/t384_calibration_storage.{c,h}`：384 保存；逻辑槽地址 0x50000/0x52000。
- `tests/mini2_640_sram_frame_smoke.c`、`tests/mini2_stream_init_smoke.c`、`tools/check_dualcore_firmware.sh`：已准备的专项，代理本轮未运行。
- `docs/runbooks/PROJECT_MEMORY.md`、`docs/runbooks/skills/t384-session-closure/SKILL.md`：长期经验与复用流程。

## 验证状态

实际运行：读取源码/日志及 `git diff --check`，最近代码修改的空白检查通过。编译/测试由用户完成，用户反馈 v6 出图、最后仍 10 FPS；没有代理目标编译、专项测试或精确持续吞吐验收证据。本次交接仅修改文档/skill，不改固件。新布局需用户目标 map 核对：FRAME 220320、ITCM 2048、DTCM 18144、CODE 41472、DATA 56960 字节，打包数据 331776 字节，逻辑容量 655360 字节；不能沿用旧产物放行。所有当前改动未由本轮提交/发布，工作区存在大量既有未提交内容，禁止覆盖。

## 未决问题

640 约 10 FPS 的确切瓶颈仍未知，发送缓存翻倍未看到收益；25 FPS UYVY 需至少 16.384 MB/s，不能承诺靠调参数达到。v5 枚举异常根因未证明；色度恒定是当前打包适用条件。正式测温、网页设备选择、重启/断电标定持久化、多平台及长时间稳定性均未完整验证。

---

# 历史交接（以下不是当前状态；旧产物、地址和待办不得直接执行）

# T384 当前交接（2026-09-18）

## 30秒恢复：29帧链路不改；黑体采集/实验拟合/独立验证入口已构建，待上板

### 当前状态

- 用户最新反馈要求简化：只采0°C、50°C，默认距离0.01m/发射率0.98，不要求独立点/误差输入，后续用户自行验证。Windows一行入口不变`py -3 "C:\Serein_Y\Sipeed\T384\tools\calibrate_t384_blackbody.py"`，每点30完整帧；先5秒HTTP可达性预检，失败保留preflight.json且不要求操作黑体。现有六张原表和两档参数无需重读；本轮仅主机脚本，不重建固件或要求因脚本修改重新烧录。
- 最新现场失败`out/radiometry/blackbody/20260918T064826.583926Z/low/module-before/cal-state.json`：首个POST等待20秒超时，没有start/status/事务号或帧文件，不能归因为机芯gain命令拒绝或黑体采集过慢。后续Windows直连curl退出28；只读网络脚本退出2，当前out/reconnect/windows-network-latest.json无地址/路由/邻居、HTTP超时。当前连接未就绪，但不能据之后状态反推报错瞬间唯一根因。
- 固件新增显式只读`cal-state`事务：gain/Vtemp原始值/自动FFC配置/快门状态，各保存LE uint16共8B；查询首尾gain及PN/SN/FW核对。SDK四种查询命令与CRC已通过实际SDK纯内存回调逐字节验证；原表/参数/文件事务协议及正常29帧采集、DVP/DMA/USB/队列策略未修改。查询期间沿用暂停/scratch/ACK释放；拒绝可释放，损坏/超时等不确定状态严格锁定。
- 采集核验现有八份bin/JSON的身份、长度、CRC/SHA；自动读取采集前后状态并绑定证据，实际同档gain/开快门/FFC配置一致才接受；禁止覆盖已有点。保存完整Y16BE帧及SHA、几何/ROI、前后诊断与原始状态事务，不够帧、合成源、逆序/重复序号、状态不匹配均无成功manifest。
- 分析重新读取原始文件并复算ROI，支持负温/小数，核对采集条件；默认只两点拟合、验证留给用户。独立点可显式补充，超过显式误差阈值返回非零且保留报告；默认无独立点不标记验证通过。模型明确`experimental-blackbody-2point-v1`，不自动应用网页、不写MCU槽、不写MINI2；边界状态不是逐帧gain/FFC/Vtemp/epoch，正式OEM算法和应用仍未闭环。
- 最新开发产物`firmware/V5F/obj/Merge.bin`218920B，SHA256=f451c6ee5a224f1c94f4efab09c303e28f3ebee01e82ec9b6d261f30fbe5521c；IPC944B，堆余V3F39232B/V5F16556B不变。新增状态事务未上板验证，当前WN2384是否支持gain getter须实测；不以人工标签绕过拒绝。Merge空洞不能保证旧标定槽保留。

### 已尝试及结果

- `bash tools/check_module_files.sh`退出0：32事务场景，含状态查询成功/拒绝后继续/CRC/长度/超时锁定/gain或身份变化；SDK四种实际命令构造对照，原表/参数、存储/HTTP/主机及新的碎片384采集/失败门禁/引导式独立验证回归通过。日志`/tmp/t384-calibration-entry-host.log`。
- `python3 -B tests/blackbody_pair_smoke.py`退出0：负温、独立点、失败误差及原始文件/状态/ROI篡改、短帧、身份/增益/条件不匹配拒绝。真实现有八份归档证据的独立完整性核验通过。
- 既有`tools/build_dualcore.ps1`WCH两核开发编译/合并及`python3 -B tools/check_dualcore_artifacts.py`退出0；日志`/tmp/t384-calibration-closure-target.log`。未烧录、擦除或访问实际Flash槽。
- Windows原生`py -3 ...calibrate_t384_blackbody.py --prepare-only`退出0，核验真实归档并输出`out/radiometry/blackbody/windows-entry-preflight/plan.json`；此目录仅为离线预检，0/50/25°C、0.5m、0.98、2°C为测试输入，不是现场测量或冻结精度要求。
- 简化后`bash tools/check_module_files.sh`和`python3 -B tests/blackbody_pair_smoke.py`退出0：新增仅两次确认、无独立点未验证、不可达时无黑体提示/无成功报告，以及丢失首个POST响应仅只读观察、不盲目重试或取消未知事务。JSON请求超时改5秒，大表下载保留20秒；记录start-http/poll/download等失败阶段。Windows原生无参数--prepare-only退出0，真实归档计划`out/radiometry/blackbody/20260918T065323.195740Z/plan.json`为0/50、0.01m、0.98、无独立点/误差阈值。日志/tmp/t384-two-point-host.log。
- 完整`bash tools/check_raw16_bench.sh`首次退出1：旧capture夹具复用了已存在的临时目录并缺新增状态证据。已将采集端测试统一到包含真实wire解析及状态证据的新专项，保留384尺寸/中心ROI断言，不放松生产校验；全套重跑退出0，含256/384、网页/NCM恢复、实际ISR/ASan/UBSan/RPC/布局及最新两核产物门禁。日志`/tmp/t384-calibration-entry-full.log`。

### 下一步

1. 用户恢复USB设备连接，先确认[诊断页](http://192.168.17.1/diag)可达，再关闭[成像页](http://192.168.17.1/)和全部抓流客户端，运行现有一行入口。当前固件身份未取得，不据HTTP连接超时要求再烧录；状态查询若仍失败，按保存的阶段/恢复状态继续定位。
2. 固定0.01m/发射率0.98、辐射面覆盖中心ROI，等0°C、50°C黑体稳定后各确认一次；完成后report.json给出实验拟合，默认无独立点，不能检查为验证通过。不同gain分开采集，本入口不自动切换机芯增益；后续由用户验证。
3. 取得当前PN/FW的3601项索引/分段/限幅、16384项查表/单位、距离表后缀及DVP SNR/NUC域权威规则；建立同帧gain/FFC/Vtemp/epoch来源后接入OEM运行时adapter。旧SDK1201/8192规则不套用。
4. 继续高低增益、升降温、机芯温漂、独立多温度点及全画面验收；实验三点ROI通过不能证明完整温区/全画面/OEM精度。新固件29帧恢复及多平台长时也待实测。

### 关键相对路径

- `tools/calibrate_t384_blackbody.py`、`capture_radiometry_calibration.py`、`calibration_capture_support.py`、`analyze_blackbody_pair.py`、`read_mini2_module_files.py`。
- `firmware/Common/Raw16/t384_module_files.c`、`t384_mini2_protocol.{c,h}`；`tests/calibration_capture_smoke.py`、`blackbody_pair_smoke.py`、`module_files_smoke.c`、`mini2_file_sdk_vectors.py`。
- `docs/reference/MINI2_READONLY_FILE_PROTOCOL.md`、`firmware/README.md`。

### 验证状态

本轮主机专项、语法检查及Windows默认离线入口通过；前轮完整回归、两核WCH开发构建/产物检查通过，本轮没有重建或修改固件。新增实时状态固件未获实机成功证据，当前设备HTTP连接未就绪；真实黑体采集/精度、逐帧状态、OEM应用、Flash保存/重启/断电及多平台未验证。未提交/发布/烧录、未写MINI2/实际标定槽。

### 未决问题

当前WN2384的gain getter支持及状态事务恢复待实测；Vtemp只记录原始值、不猜温度单位。边界FFC配置/快门不能证明采集中无FFC事件。正式3601/16384算法/距离表/数据域/同帧状态仍缺，当前入口仅提供真实采集与实验验证，不宣称正式标定完成。旧2KiB无符号整数实验槽包无自动应用，双Flash共页擦除被拒绝，Merge下载保留尚未证明。

---

## 历史：标定保存保护已构建

### 当前状态

- 用户要求抓住标定主要矛盾、先完成固件。六张原表及两档参数已经取得；当前无需重复读取。当前WN2384/FW00.00.07.01，原表目录`out/radiometry/mini2-uart/20260918T053852.431973Z/`，参数目录`out/radiometry/mini2-uart/20260918T055634.948076Z/`。high=-14790/15219/6990，low=-12288/14400/10000，KT/BT3601项、NUC-T16384项；对应高低档文件相同不证明实际gain。
- 本轮补固件保存事务：上传header/payload CRC、固定字符串边界、每字节覆盖、generation回绕、选择另一物理槽、payload/header物理读回后最后写magic；非整字payload按Flash字对齐补FF，CRC仍只算实际长度。故障前有效槽不擦除。WCH双Flash模式的8KiB擦除会使现有两槽共页，实际检测后拒绝擦除；未移动布局/改Flash模式。
- HTTP上传/提交拒绝活动流及机芯读表，严格正文长度/路由/大小写Content-Length/重复长度/Transfer-Encoding。响应复用各客户端request直到ACK，支持小TCP窗口及背压，不增加全帧/大表缓存。manifest补identity/header_crc32及应用状态；保存成功明确applied=false/oem_radiometry_ready=false，未接入新温度模型。
- `tools/calibration_storage_client.py`只读export、本地check、显式restore后全量及身份字段读回核对。未向实际设备写槽/重标定/烧录；原厂表不是2KiB槽包，v1旧整数无符号两点结构不能用于-20..150°C正式标定。
- 最新双核开发产物`firmware/V5F/obj/Merge.bin`218476B，SHA256=f835296e8282f90b9d5934a04276aa832c04640b72c24a64e0e50d060a056426；IPC944B，V3F堆余39232B（复用响应缓冲比前版多4088B），V5F16556B。采集/DVP/DMA/USB/帧流格式及29帧队列策略未修改；新保存保护未上板验证。Merge的FF空洞仍可能覆盖已有标定槽。

### 已尝试及结果

- `bash tools/check_module_files.sh`退出0，新增保存故障/完整性、最大包HTTP/碎片正文/非法头/准确路由、流中提交拒绝、背压及客户端缓冲独立、备份/恢复内容绑定回归；原25场景、SDK纯内存命令和两档参数/原表主机回归继续通过。日志`/tmp/t384-calibration-closure-host.log`。
- `tools/build_dualcore.ps1`使用已装WCH GCC两核开发编译/合并/map/HEX通过；增加双Flash保护后已再次构建。日志`/tmp/t384-calibration-closure-target.log`。`python3 -B tools/check_dualcore_artifacts.py`退出0。
- `bash tools/check_raw16_bench.sh`退出0，含完整主机/256及384/网页恢复/实际ISR/双核RPC/布局/目标产物门禁，日志`/tmp/t384-calibration-closure-full.log`。
- 存储专项ASan/UBSan初次被LeakSanitizer的ptrace环境限制中止；同一二进制`ASAN_OPTIONS=detect_leaks=0 /tmp/t384-calibration-storage-asan`退出0。测试无动态分配；未把初次环境失败写成全部消毒器通过。

### 下一步

1. 原表/参数无需重读；不主动写机芯，不把数据齐全当OEM算法已匹配。
2. 确认当前PN/FW的3601项索引/分段/限幅、16384项查表/单位、距离表后缀/格式、DVP SNR/NUC域及同帧gain/FFC/Vtemp来源；旧SDK0..1200索引和NUC>>1规则不套用。
3. 有权威规则后接入固件状态adapter和主机/浏览器应用adapter，匹配身份/增益/epoch，验证负温及独立黑体点；状态未就绪继续禁止绝对温度输出。
4. 新固件保存设施的实际Flash模式、写入/读回/重启/断电验收另行由用户执行；存在有效槽时下载前先导出备份，禁止Erase All/Clear CodeFlash也不能保证Merge下载保留。
5. 黑体条件/环境温区/误差指标冻结后，分别验证高低增益、升降温、机芯温漂及全画面，再宣布完整标定。保持当前29帧策略，不扩展吞吐优化。

### 关键相对路径

- `firmware/Common/Raw16/t384_calibration_storage.{c,h}`；`firmware/Common/App/http_status.c`。
- `tests/t384_calibration_storage_smoke.c`、`calibration_http_smoke.c`、`calibration_storage_host_smoke.py`；`tools/calibration_storage_client.py`、`check_module_files.sh`。
- `firmware/README.md`。

### 验证状态

主机专项、完整回归、WCH两核开发构建与产物门禁通过。最新固件未上板验证，Flash写入/重启/断电安全、实际擦除模式、OEM帧状态/运行时应用、真实黑体精度和多平台仍未验证。未烧录、未写MINI2/实际标定槽、未commit/push。

### 未决问题

当前3601/16384算法版本、距离表后缀、DVP数据域和同帧状态仍是正式测温主要阻塞；2KiB旧实验小包无自动应用，不能承载完整OEM表或负温正式模型。现有两槽在双Flash模式下不能独立擦除，固件拒绝写入但完整支持尚需单独确认布局/模式；Merge下载保留、环境温区/误差尚待实测或冻结。

---

## 历史：WN2384六张原表及两档参数已读，正式测温未闭环

### 当前状态

- 用户明确当前稳定29帧、链路先不改；当前聚焦T384标定。实机WN2384/FW00.00.07.01已通过NCM/UART取得高低档KT/BT/NUC-T，证据`out/radiometry/mini2-uart/20260918T053852.431973Z/`。KT/BT各3601项/7202B，NUC-T各16384项/32768B；对应高低档完全相同，不代表恒等KT/BT或视频gain已确认。长度/CRC32/SHA及身份、事务释放独立复核通过。
- distance两档本地error=-9/path为空，没有发送文件打开命令，事务均无cleanup_failed/采集暂停。当前PN无可信后缀，不猜F1。`docs/data/read_report.txt`仍是旧WN2256报告，不代表最新384结果。
- 用户同意补参数/距离表证据/数据域及状态绑定。已补`tpd-high/tpd-low`只读参数查询，原SDK`adv_tpd_parameters_get`命令01 26 8A，返回LE int16 Ktemp、int16 Btemp、uint16 Address_CA。最新`out/radiometry/mini2-uart/20260918T055634.948076Z/`两档成功：high=-14790/15219/6990，low=-12288/14400/10000；独立bin/CRC/SHA/JSON/身份复核通过，均DONE、dvp_paused=false、cleanup_failed=false。当前PN/FW实机支持已证明，不泛化SDK支持名单。不打开/关闭文件，open/close_status=255；29帧采集/队列/帧流策略未改。
- 旧SDK算法经纯内存调用确认索引限0..1200，旧NUC-T查表按NUC>>1使用8192项；不能套入当前3601/16384项表。新参数成功也不能直接启用正式温度。未改变温度模型、未写MINI2/Flash标定槽、未烧录/提交/发布。
- WCH两核开发构建/map/HEX/Merge通过：`firmware/V5F/obj/Merge.bin`218476B，SHA256=c382a5b4244bc508046818a1e28db356714ac178c9e792afeb9228ee32ea92e2。IPC944B，两核堆余35144B/16556B不变。参数入口已实机成功，读后真实29帧持续恢复待验证；合并BIN的FF空洞仍不能保证已保存Flash标定槽保留。

### 已尝试及结果

- 读取docs/logs.txt及新六份bin/JSON，独立完整性/有符号BT及高低档比较通过。KT范围13276..35989、BT -1946..0、NUC-T无递减；不是恒等/零填充KT/BT。
- `bash tools/check_module_files.sh`退出0：25场景含参数high/low、拒绝后再查询、CRC/长度/超时锁定、身份变化、半发取消/READY取消及下载释放；SDK纯内存回调逐字节命令对照、有符号LE解析和主机CLI/清理门禁通过。日志`/tmp/t384-tpd-parameters-host.log`；初次新取消场景发现原始error_command未记录，补取消首错误记录后通过。
- `bash tools/check_dualcore_firmware.sh`退出0：双规格真实ISR/数据、压力/读租约/溢出恢复、ASan/UBSan、实际RPC/布局通过；日志`/tmp/t384-tpd-parameters-dualcore.log`。`tools/build_dualcore.ps1`两核WCH开发编译/合并/map门禁通过，日志`/tmp/t384-tpd-parameters-target.log`。Windows直接启动因binfmt缺失失败，/init沙箱socket失败，获沙箱外许可后构建成功；不是设备故障。
- 完整`bash tools/check_raw16_bench.sh`主机部分包括最后双核/布局通过，最终退出1：用户MRS随后生成Merge218480B比HEX范围多4个尾部FF，严格长度门禁拒绝。已用既有merge_dualcore_hex.py按HEX重建218476B，断言全部原有效字节完全相同、差异仅四个尾部FF；python3 -B tools/check_dualcore_artifacts.py最终退出0。不是源码/协议失败，未重新烧录，不要求用户因此重烧。
- 参数首次HTTP400/-1：用户确认只下载V3F.hex，未更新V5F；随后的超时未保存有效start/status，用户表示可能仍下载错误，不能判定真实死锁根因。最终正确下载后两档成功，以最新证据覆盖前次状态；不重复之前失败路径。

### 下一步

1. 六份原表和两档参数已齐，当前无需重复烧录/读表/查参数；新参数JSON已核对，保留全部主机原始证据。
2. 用户在[成像页](http://192.168.17.1/)确认读后画面恢复；状态解除已证实，不以此替代持续29帧恢复实测。
3. 代理/原厂继续确认索引版本；旧SDK的0..4095再减Address_CA算法对当前6990/10000地址会恒夹到索引0，不允许启用。
4. 向原厂确认3601项索引/分段/限幅、16384项NUC-T查表及单位、距离表准确后缀/格式、DVP TPD的SNR/NUC域、gain/FFC/Vtemp状态获取与同步。不用旧函数/旧WN2256参数补齐。
5. 取得规则后做当前384自己的黑体响应及温漂验证，再决定正式计算/标定保存。目标历史为-20..150°C，环境温区和允许误差尚未冻结；两点吻合不代表全量程精度。

### 关键相对路径

- `firmware/Common/Raw16/t384_module_files.c`、`t384_mini2_protocol.{c,h}`；`tools/read_mini2_module_files.py`。
- `tests/module_files_smoke.c`、`mini2_file_sdk_vectors.py`、`module_files_host_smoke.py`；`tools/check_module_files.sh`。
- `docs/reference/MINI2_READONLY_FILE_PROTOCOL.md`、`firmware/README.md`；当前数据目录见顶部。

### 验证状态

参数专项主机/双核回归及WCH两核开发构建通过；完整回归主机部分通过，Merge尾部FF长度门禁曾失败，按HEX重新合并后最终产物门禁通过。用户已上板，两档参数查询及身份/完整性/释放实机成功；读后持续29帧、正式测温/黑体精度和多平台未验证。代理无烧录/重标定/写槽/commit/push。

### 未决问题

WN2384的3601/16384算法版本、距离表后缀、DVP SNR/NUC数据域、同帧gain/FFC/Vtemp/epoch、环境温区/允许误差、Flash标定槽的下载保留。参数只读查询已在当前PN/FW通过，前次超时不据此认定已定位死锁；旧吞吐/USB问题暂不扩展。

---

## 历史：v4用户反馈降到20FPS；已撤回并恢复v3约29FPS策略

### 当前状态

- 用户反馈上轮v4反而只有约20FPS，明确要求撤回。已撤回该轮跨帧接收，恢复正常帧起点必须旧队列/读租约排空的v3策略；删除recovering字段及仅对应跨帧行为的两个新增测试。不是将源改回30，384模块仍请求60FPS、144KiB块队列不变。
- 完整撤回该轮身份修改：V5F恢复block-ring-60-v3，V3F remote代理恢复原double-frame-v2字符串；diag源名字不能单独证明采集版本，用匹配两核Merge及pipeline.streaming/24槽/147456B判断。IPC仍3/944B，原字节序、ROI、Picture、USB重连保护、网页和用户已有改动保留。
- 已生成回退开发产物firmware/V5F/obj/Merge.bin218180B，两核编译/map/HEX/合并检查通过，堆余V3F35144B/V5F16556B。未提交/发布，代理未烧录；回退后未上板验证，不能宣称实测恢复29FPS。
- 之前v3原生Windows两轮约29.5–29.8完整FPS为回退参考。v4的20FPS是用户反馈，未采活动计数，不能断言其唯一原因；上轮仅凭隔帧现象和逻辑夹具不足以证明放宽接帧能改善实际吞吐。

### 已尝试及结果

- bash tools/check_dualcore_firmware.sh退出0：256/384严格语法、实际ISR完整字节、101帧、读租约/溢出abort/恢复、回绕/scratch、384 ASan/UBSan、RPC和host ld布局通过，日志/tmp/t384-ring60-rollback-host.log。
- tools/build_dualcore.ps1两核WCH编译/合并及目标门禁通过；Merge218180B、IPC944B和堆边界不变，日志/tmp/t384-reconnect-target.log。无设备操作。
- 本轮不进行新吞吐优化或设备测速；仅回退上轮修改，没有执行破坏性Git回滚。

### 下一步

1. 用户按原MRS流程下载回退版匹配两核Merge.bin，禁止Erase All/Clear CodeFlash，不混用旧核；代理不操作设备。
2. 在[成像页](http://192.168.17.1/)确认流帧率是否回到约29，必要时用[诊断页](http://192.168.17.1/diag)核对源60和块队列配置；实际恢复以用户板上结果为准。
3. 若回退后仍低，先核对两核下载身份及是否只有一条活动流；不继续放宽接帧。后续60FPS优化需单独证明TCP/USB持续13.27MB/s能力。

### 关键相对路径

- firmware/Common/Raw16/t384_frame_pipeline_full.c、t384_dualcore.h、t384_frame_source_mini2.c、t384_frame_source_remote.c、tests/mini2_dvp_capture_smoke.c：本次定向撤回。
- firmware/README.md；firmware/V5F/obj/Merge.bin（Windows C:\Serein_Y\Sipeed\T384\firmware\V5F\obj\Merge.bin）。

### 验证状态

回退源码的双核专项主机回归、WCH目标编译及map/HEX/Merge门禁通过；回退后未上板验证，29FPS恢复尚待用户下载确认。未继续设备读取/测速/烧录，未commit/push。

### 未决问题

60完整FPS未达成，v4现场20FPS根因未由活动计数验证，USB瞬断恢复/FFC/真实剩余温漂和长时仍未决。本轮按用户要求停止跨帧优化，仅恢复此前策略。

---

## 历史：v4跨帧接收尝试，用户实机约20FPS后已撤回

### 当前状态

- 用户已上板块流版反馈源60.0/流29.0。Windows两轮10秒原生单流分别298完整帧/29.799FPS/6.591MB/s、295帧/29.497FPS/6.524MB/s，目标13.27104MB/s均失败；序号缺口301/303，不完整帧和序列异常均0。不是浏览器渲染造成的测速上限，也不是实际60验收通过。
- 活动诊断2秒：source.frames12482→12605（+123）、published11824→11885（+61）、dropped658→720（+62），pipeline.acquire_no_slot659→721（+62）、abort0、峰值22/24，NCM TX drop/背压均0，TCP backpressure7637→8734。模块回读60；指向v3在正常帧起点要求旧队列/租约为空而隔帧拒收。不能据NCM无背压推断13.27MB/s持续能力已验证。
- v4正常时队列有空槽即可接下一帧，允许前帧末块COPY租约跨下一START，保留slot所有权；只有真正abort后才等前缀和读租约排空恢复，避免慢消费不断发布损坏前缀。满队列仍从帧起点跳过，中途满仍abort不发END，不放宽物理结束或浏览器完整性验证。
- 修正V3F remote代理源名字一直固定double-frame-v2的诊断漏洞；两核新384身份mini2-dvp-v5f-block-ring-60-v4。IPC仍3/944B，新recovering字段使用原union剩余4B，数据区144KiB/DMA12KiB、五段地址/探针、25650FPS、Y16BE、Picture、USB2及重连保护不变。
- 新两核WCH目标编译/合并/map/HEX通过，Merge.bin218204B，堆余V3F35144B/V5F16556B。未烧录/提交/发布，未上板验证v4能否持续60完整FPS。若仍不足，下一层查队列中途满和TCP COPY/窗口/ACK节拍，不猜改USB描述符或关闭checksum。

### 已尝试及结果

- WSL只读HTTP先因沙箱socket禁止失败，获授权后5秒超时；Windows只读脚本读取成功，不能拿WSL不可达当设备断网。第一诊断窗口stream.active=0，后续调整先启动测速再采样，得到活动计数。
- 当前设备v3的两个Windows10秒单流测速均退出1未达60目标；结果已滤掉不相关标识保存在out/stability/ring60-boundary-before.json，原生输出/tmp/t384-ring60-speed.log。
- bash tools/check_dualcore_firmware.sh退出0，新测试101帧跨边界保留旧END、无丢帧且全部字节正确；旧END被租约持有期间新帧开始/前三块写入不覆盖旧数据，两帧均完整；原溢出/abort后排空恢复、ASan/UBSan/256/RPC/布局仍通过。
- 临时副本恢复旧排空条件，新增跨边界夹具在published_frames==frame+1断言失败（SIGABRT），生产文件未回滚；证明新回归覆盖了旧保护过严。随后v4源码已用MRS现有工具链构建和门禁通过，不操作硬件。
- 完整回归bash tools/check_raw16_bench.sh退出0，包含双规格严格语法、模块/文件/HTTP/标定、网页/NCM恢复、新跨帧边界/ASan/UBSan/RPC/布局及新目标产物门禁；日志/tmp/t384-ring60-boundary-full.log，双核专项/tmp/t384-ring60-boundary-host.log，目标/tmp/t384-reconnect-target.log。

### 下一步

1. 完整回归已通过，用户按原MRS流程下载匹配两核v4 Merge.bin，禁止Erase All/Clear CodeFlash，不混用旧V5F；代理不操作设备。
2. 在[诊断页](http://192.168.17.1/diag)核对v4身份、模块DVP回读60、pipeline.streaming1/24槽/147456B及双核启动访问检查。名字由V3F返回，不能单独证明V5F也已更新，须使用匹配Merge。
3. 原生Windows单流先60秒核对完整FPS、CRC/偏移、缺口/半帧、pipeline abort/no-slot及TCP/NCM背压；若接近60再10分钟和USB瞬断恢复。source60或短时峰值不能代替60完整FPS持续验收。

### 关键相对路径

- firmware/Common/Raw16/t384_frame_pipeline_full.c、t384_dualcore.h：正常跨帧/abort恢复条件；t384_frame_source_remote.c、t384_frame_source_mini2.c：v4身份。
- tests/mini2_dvp_capture_smoke.c；out/stability/ring60-boundary-before.json；firmware/README.md。
- firmware/V5F/obj/Merge.bin，Windows C:\Serein_Y\Sipeed\T384\firmware\V5F\obj\Merge.bin。

### 验证状态

设备v3源60/模块回读60已只读确认，主机实际约29.5–29.8完整FPS；v4双核专项/负控、WCH目标构建及全套回归退出0，未上板验证。真实60完整FPS、USB瞬断恢复、手机/长时/正式测温仍未完成。

### 未决问题

允许跨帧后TCP持续吞吐是否足够13.27MB/s及是否引起中途溢出需实测；若源60而abort/背压增长，按采集/队列/发送/USB/浏览器分层，不再次通过强制隔帧掩盖不足。USB旧故障现场唯一根因、FFC和真实剩余温漂仍未决。

---

## 历史：384 v3请求60FPS的双核块流已构建；真实60完整FPS待上板

### 当前状态

- 用户要求实现384的60帧，承接行缓冲讨论。384配置探测器/DVP均60，现有0x46易失启流命令仍必须由0x86回读确认，不以ACK认定60FPS；未修改采样沿、电平、GPIO、USB描述符或线上帧格式。
- 384双核改为24槽×6144B/8行，共144KiB块队列，DMA双块12KiB。非末块提前交给V3F；末块等完整物理帧结束/288行校验通过才发END。旧队列未空从帧起点跳过新帧，中途满则abort不发END，已发送前缀由浏览器在下一START丢弃，持有读租约的数据不被覆盖。
- 384源身份mini2-dvp-v5f-block-ring-60-v3；diag应pipeline.streaming=1、slot_count=24、capacity_bytes=147456、dualcore.frame_banks=0、三项双核启动/访问检查均1，模块数字输出回读60。source FPS、发布帧数与主机完整FPS须分别记录。
- 两核IPC版本3，布局944B不变，必须更新匹配的V3F/V5F合并镜像；256仍旧双整帧路径/默认50FPS。384的LE接收→Y16BE融合复制、Picture原样、ROI及下链发送保护保留。
- 物理地址和五段访问探针保留；旧第二帧ITCM区只放384B元数据，另外三段各32B探针。数组减少的空间没有自动转给堆，链接窗口和栈边界未移动；V3F堆余35144B、V5F16556B。仍USB2 NCM，本轮不含USB3，384×288 RAW16 60FPS纯像素需求13.27104MB/s。
- WCH GCC12.2.0两核开发构建及map/HEX/Merge检查通过，Merge.bin218180B，未烧录/未发布/未提交。未上板验证，不能称实际60完整FPS已达到。此前USB瞬断网络不可达仍未实机闭环，用户没有调试串口。

### 已尝试及结果

- bash tools/check_dualcore_firmware.sh退出0：真实ISR首块提前/物理END延迟、101帧字节完整性、慢消费/持租约溢出后恢复、帧序号和队列计数回绕、scratch互斥、短帧/FIFO/停源恢复、256旧链路、384 ASan/UBSan、RPC和host ld布局通过。
- node tests/device_console_reconnect_smoke.cjs退出0：真实网页内联parser接收旧帧前缀、分片header/payload、下一START丢前缀、完整END才显示且Y16BE值正确；原缓存返回/重试/过期fetch/网络恢复测试保留并通过。
- tools/build_dualcore.ps1完成两核目标编译/合并；python3 tools/check_dualcore_artifacts.py退出0，IPC、固定地址、实际数组大小、堆/代码/HEX入口及Merge一致性通过。无设备操作。
- bash tools/check_raw16_bench.sh全套退出0（包含双规格、模块控制/读表/HTTP/标定、网页/NCM恢复和双核门禁）；随后新增parser前缀测试独立执行通过。日志/tmp/t384-ring60-host.log、/tmp/t384-ring60-full.log、/tmp/t384-reconnect-target.log。

### 下一步

1. 由用户按现有MRS流程下载匹配两核Merge.bin，不混旧核、不Erase All/Clear CodeFlash；合并BIN的FF空洞不能保证保存Flash标定槽。
2. 新版启动后在[诊断页](http://192.168.17.1/diag)核对块流身份/容量、双核启动和模块DVP回读60；ACK或源名字不能代替实际回读。模块不支持/未接受60时保留失败证据，不强行放行未知数据域。
3. 在[成像页](http://192.168.17.1/)先看画面/中心连续性，再用原生Windows单条流验证完整FPS、字节/CRC、半帧、序号缺口、队列水位/abort和TCP/NCM背压，先60秒再10分钟；实际60目标不得沿用工具默认25FPS门槛。避免并发另开第二条流。
4. USB瞬断仍按固定Windows只读脚本保留时间线，分别确认网卡/IP/HTTP/流恢复；断电恢复不算重连通过，不要求用户当前必须接串口。

### 关键相对路径

- firmware/Common/App/t384_product_config.h、firmware/Common/Raw16/t384_frame_pipeline_full.c、t384_dualcore.{h,c}、t384_frame_source_mini2.c：60配置、SPSC块队列、IPC/物理END。
- tests/mini2_dvp_capture_smoke.c、tests/device_console_reconnect_smoke.cjs、tools/check_dualcore_layout.py、tools/check_dualcore_artifacts.py：实际路径/回绕/布局与产物门禁。
- firmware/README.md；开发固件firmware/V5F/obj/Merge.bin（Windows C:\Serein_Y\Sipeed\T384\firmware\V5F\obj\Merge.bin）。

### 验证状态

主机真实路径、ASan/UBSan、完整回归及WCH两核开发构建通过；未上板验证。实际模块60输出、13.27MB/s持续吞吐、主机60完整FPS、真实瞬断恢复、长时/手机/其他PC和正式测温尚未验证。

### 未决问题

60FPS若未达到，须分模块回读/源输入、队列溢出或帧边界拒收、TCP复制/背压、USB实际吞吐及浏览器处理层定位；源60不能代替完整60。USB故障现场唯一根因、FFC次数、真实剩余温漂和标定槽下载保留仍未决。

---

## 历史：USB重连修复版仍网络不可达；下链发送隔离/观测新版待上板

### 当前状态

- 最新USB反馈：用户已下载上轮重连修复版，网卡重新出现但诊断断开、刷新根页不可达，随后断电重启。Windows故障时Up/480Mbps、DHCP IP192.168.17.2/24和设备路由存在，HTTP5秒超时；后续只读HTTP成功是重启后的新启动窗口，不属于自动重连验收。当前不能继续只归因旧流连接或浏览器。
- 新补充保护：NCM发送在link-down直接ERR_USE，清理旧TCP的RST不进入新USB会话，亦不递归tud_task。新增夹具模拟HTTP reset期间发旧RST，修改前失败、修改后零USB调用通过；原夹具只模拟reset计数、覆盖不足。该代码风险已证明，现场唯一根因尚未证明，不再猜改端点/USB时序。
- 新观测：diag的usb.recovery_guard=1与ncm.mounts/umounts/suspends/resumes；USB事件变化后V3F原115200串口3条USB recover日志（每2秒最多1条，稳定期不持续打印，主循环非ISR）。新两核WCH构建/map/HEX/Merge与bash tools/check_raw16_bench.sh全套退出0，Merge218632B，V3F堆余35144B/V5F16556B，新增32B静态RAM。该版未烧录/未上板验证；日志/tmp/t384-usb-recovery-host.log和/tmp/t384-reconnect-target.log。
- Windows只读脚本tools/windows_user_action.ps1已执行：首次HTTP超时退出2、断电重启后成功退出0，结果out/reconnect/windows-network-latest.json及windows-network-after-powercycle.json。不改系统网络/设备状态，不把WSL失败当Windows DHCP证据。
- 用户明确没有接V3F调试串口，不能以串口日志作为当前必需前提。固定Windows只读脚本新增-DurationSeconds 60有限采集，保存windows-network-monitor.json完整时间线；无网络更改/复位/持续服务，遇到HTTP失败记录并退出2。新版先由用户按现有流程下载，再观察USB故障，代理未操作设备。
- Windows有限采集已原生运行8秒退出0、4条HTTP成功快照及JSON数组检查通过；这是断电重启后稳定网络的工具验证，用户当前设备尚未下载本轮usb.recovery_guard新版，不能写成故障恢复通过。
- 最新反馈：用户说字节序修复“好像可以了”。实机已出现dvp.y16_input=LE，ROI主均值约31119、反向解释约36806，原始prefix仍低字节在前，支持匹配采集/网络修复已运行；活动diag曾显示stream.fps_x1000=29000。属于用户短时改善及只读实机确认，尚不等于严格持续/正式测温验收。
- 用户补充：启动FFC时一般尚未连上，不影响使用，只需检查是否bug，暂不追问声音/冻结周期、不改机芯设置。当前固件只读class0x02/index0x81自动FFC开关，回读开启，没有手动FFC、自动FFC阈值/间隔或快门开合setter；V5F初始化只执行一次。
- 初轮USB修复背景：用户已确认物理USB入口且网卡会重新出现。原网页已有断流退避和5秒无帧重试。额外网页漏洞为pagehide停止后仍标已连接、缺少缓存返回pageshow恢复，以及旧fetch/read结果可覆盖新连接。已修复状态清除、缓存/可见恢复、online提前重试、reader取消/释放和过期连接隔离；此后用户已上板反馈整个网络仍不可达，以上实现不等于现场通过。
- USB恢复缺口：当前DCD发BUS_RESET而无UNPLUGGED，TinyUSB复位不调用卸载回调；旧HTTP/RX清理原来只在卸载，重新挂载可残留旧流。真实回调夹具修改前复现残留、修改后清理通过；挂载下链→清理会话/RX/filter→上链，保留正常suspend/resume会话，不改描述符/协议/USB电平。网页及NCM主机回归通过，本轮重连未上板验证。只读diag含沙箱外两次均5秒超时，不能确定目前设备是否已恢复枚举或供电。
- 本轮后续两个只读快照source.frames=2061→3586，restarts/probes/rearms=12/6/1、control_attempts/tx_bytes=53/1219均未增长，bad_frames/FIFO均0。最早活动快照686帧时同样12/6/1及53/1219，说明采集约97秒内没有后续重复MCU命令，但窗口在用户报告的启动FFC之后，不能排除首次初始化或首次rearm的间接影响。
- 自动FFC原厂SDK明确按Vtemp阈值、最小/最大时间间隔触发；本机阈值和间隔未读，不能把协议示例当实际默认值，也不能断言384固定需做几次。边界风险：>=500ms采集停顿会走恢复，非pending状态即使数字enabled/format/fps匹配仍可重发0x46；尚未证明FFC会停DVP或启流会触发FFC，不据此直接改恢复或关闭自动FFC。证据out/radiometry/wn2384-ffc-observation/。
- 用户已恢复工作：最新优化固件稳定约29FPS，本轮聚焦384温漂和中心Y16周期性几万→一万，256无同样现象；先前“暂停主动读取”已解除。29FPS为用户实测反馈，仍不冒称代理严格持续验收通过。
- 设备PN=WN2384/FW=00.00.07.01，双帧v2、http.checksum_aligned_reads=1，两核已启动；启动读取的Vtemp/module_temp/auto-FFC不是当前逐帧状态，不能用来断言FFC引起漂移。
- 根因证据：同一原始DMA/流数据低字节在前，却由网页/抓流工具按Y16BE解析。12次诊断BE中心均值9325–15467，LE为31523.95–31547.94，空间标准差BE约1834–2316、LE约7.16–9.05；坏帧/FIFO/source drop均0。温漂被错误字节序放大约256倍，低字节回绕会造成巨大跳变和图像灰度环绕。
- 整帧验证：30个221184B完整帧，BE中心均值29531–30108，LE为31346.88–31349.13；全图相邻像素平均差BE3178.90、LE9.24。对同一帧离线解码得到灰度结构连贯的LE图，BE图有明显灰度环绕。此证据只确认接收内存中的字节序，不确认未测的物理DVP总线字节时序。
- 最小修复：384 confirmed TPD在V5F现有DMA→pipeline复制中融合两像素字节对换，保持Y16BE线上契约；256与Picture复制不变。不修改DMA原始bank或增加帧RAM。ROI主统计与wire一致，dvp.first_row_prefix仍保留原始接收字节，diag新增dvp.y16_input=LE（384）/BE（256）。
- 新两核WCH目标构建、map/HEX/Merge检查已通过；Merge.bin218632B，IPC944B、V3F堆余35176B/V5F16556B不变。V5F反汇编确认每两个像素一次lw/sw，无逐字节整帧循环。用户已运行并反馈初步改善，尚无修复后严格完整帧/长时报告，不能写成温漂完全解决或29FPS已严格验收。
- 未commit/push/发布/烧录，已有web/raw16_bench_console.html及嵌入include的布局改动保留。384没有有效测温模型，不套WN2256实验公式，也不调整温度常数。

### 已尝试及结果

- curl --max-time 5 --silent --show-error http://192.168.17.1/diag：只读成功，源约30FPS、有效384 TPD、旧BE/LE中心统计显著不同。
- 12秒诊断采样及30帧只读抓取完成，保存在out/radiometry/wn2384-endian-before/；不是黑体采集，没有参考温度/独立gain/FFC快照。WSL探针每帧计算统计较慢，18个序号缺口、source drop新增19、stream backpressure新增545；坏帧/FIFO/abort/write error/timeout均0。不能当作29FPS持续验收或归因新的源损坏。
- bash tools/check_dualcore_firmware.sh：双规格实际ISR/完整字节/低字节回绕/Picture原样/规范ROI/原始prefix、101帧双核交错、慢消费读租约、短帧/FIFO/恢复、ASan/UBSan/RPC/内存布局通过，LeakSanitizer关闭。
- bash tools/check_raw16_bench.sh：全部主机回归通过；执行末尾因构建期间新增注释导致旧map时间门禁退出1。随后tools/build_dualcore.ps1刷新两核，python3 tools/check_dualcore_artifacts.py独立新map/HEX/Merge门禁通过。没有把首次脚本退出1写成整体退出0。
- Windows直接执行powershell.exe在沙箱内Exec format error，经/init调用又因vsock受限失败；获授权后通过/init执行现有tools/build_dualcore.ps1完成WCH GCC12.2.0两核构建，无下载/擦除。日志/tmp/t384-y16-endian-target.log，主机全套日志/tmp/t384-y16-endian-check.log。
- 重连：node tests/device_console_reconnect_smoke.cjs及真实NCM mount/umount/suspend/resume回调夹具通过；修改前分别复现停止后保留已连接状态、无umount的重挂载残留旧会话。现有tools/build_dualcore.ps1两核构建及python3 tools/check_dualcore_artifacts.py通过，Merge.bin218632B，堆预算不变；V3F反汇编确认挂载先下链并调用t384_http_status_reset再上链。日志/tmp/t384-reconnect-target.log；本轮USB/网页修改未上板验证。
- 本轮首次全套主机脚本执行中修改脚本，触发执行偏移语法错误退出2；bash -n通过后固定脚本重新完整执行，日志/tmp/t384-reconnect-host.log，不把首次运行报错当代码验证通过。
- 重跑bash tools/check_raw16_bench.sh最终退出0，包含新增网页/NCM恢复夹具、双规格严格语法、模块HTTP/文件/标定、双核真实ISR及新map/HEX/Merge门禁；本轮主机检查完整通过，无真实USB瞬断/重新枚举验收。

### 下一步

1. 当前新版为带usb.recovery_guard=1的下链发送隔离/故障观测Merge.bin，旧重连版实机仍网络失败；不反复下载同一个旧版。新两核构建及完整主机回归通过，可安排新版上板，代理不烧录/擦除。
2. 新版上板后若USB瞬断再次导致根页/diag不可达，先保留V3F串口USB recover日志、运行Windows只读脚本保存IP/邻居/HTTP状态，避免立刻断电丢故障现场；确认IRQ/xfer、mount/suspend、RX/TX停在哪层。启动FFC暂保留设置。
3. 用户在http://192.168.17.1/保持固定场景，检查灰度环绕/中心大跳变是否消失及29FPS是否保留；代理再进行单流完整帧/ROI漂移验证，不并发占用第二条流。
4. 字节序修复后如仍有明显真实漂移，再单变量采集预热时间、FFC事件、gain/Vtemp和固定ROI，区分热稳定、自动FFC和响应域。启动时查询值不能代替当前状态，不关自动FFC、不重新标定或修改公式掩盖数据问题。

### 关键相对路径

- firmware/Common/App/t384_product_config.h：profile绑定接收字节序；firmware/Common/Raw16/t384_frame_source_mini2.c：融合复制/ROI适配。
- firmware/Common/Raw16/t384_raw16_roi.{h,c}、tests/mini2_dvp_capture_smoke.c：规范统计及真实ISR端到端回归。
- firmware/Common/App/http_status.c：dvp.y16_input诊断标记；firmware/README.md：输入/输出数据域说明。
- out/radiometry/wn2384-endian-before/{analysis.json,summary.json,diag-series.json,first-frame.bin,comparison.png,diag-before.txt,diag-after.txt}：只读实机证据，非黑体标定。
- tools/build_dualcore.ps1、tools/check_dualcore_artifacts.py、firmware/V5F/obj/Merge.bin：新两核开发产物；Windows路径C:\Serein_Y\Sipeed\T384\firmware\V5F\obj\Merge.bin。

### 验证状态

字节序实机证据、规范化后离线成像、双规格主机回归及两核WCH构建/门禁通过；用户已运行并反馈改善，活动diag短时29FPS。后续FFC只读窗口无新增重启/启流/控制命令，启动FFC实际次数和触发原因仍未验证；30帧诊断不是吞吐验收。初轮网页/NCM重连修复已用户下载、实机仍诊断/根页不可达；断电重启后的成功HTTP不是重连通过。本轮下链发送隔离夹具、WCH两核构建/map/HEX/Merge通过，完整主机回归退出0，隔离/观测新版未上板验证。手机/其他PC系统、正式测温精度、FFC/Vtemp逐帧绑定、10分钟/24小时和标定槽下载保留未验证。FFC调查不修改参数；后续构建只针对网页/USB网络恢复。

### 未决问题

修复后成像/中心连续性与29FPS、真实剩余温漂/FFC相关性、384自身OEM标定/数据域、长时和重连稳定性。不能把所有慢漂移都归因字节序，也不能把byte swap当温漂补偿算法。

---

## 历史：v6已收满但随后停顿；v7模组侧恢复待Build/上板

### 30 秒恢复（历史背景）

### 当前状态

- 用户反馈0/0 FPS、发布0、故障55，明确已整机USB断电重连。只读诊断确认实机v6/8行6144B/10槽，55帧均288行/221184B、ROI valid1，但无空槽55次、全部abort；源计数后续仍55，接收器重启71→209、CR0=67/CR1=1，FIFO/NCM drop/backpressure0。3秒单流收到HTTP头但无完整帧。v6改善块/行计数，未恢复持续成像；不能把55次无槽直接归因活动HTTP吞吐，可能属于启动无消费者窗口。
- 当前诊断数字1/1/30、TPD1是启动采样，停顿后未重新查模组，不能证明此时仍启流；本地DVP重启已实机证实不足。停止继续猜采样沿、USB身份或温度公式。
- v7仅384增加故障态恢复：停止本地DMA后有界读0x86三字节与0x85模式；有效回执且模式与最后确认域一致才重发现有易失0x46启流，至少4秒写入间隔。pending阶段只读等待应用，后续真实输出/模式确认才启DMA，不以ACK放行，丢ACK亦用回读判定；模式错/通信失败不写更多命令。正常持续输入及读表期间不触发，256保持原本地恢复路径。
- 无探测器/模式/保存/标定写入，USB/网络/私有帧格式及73728B采集数据预算不改。新增dvp.module_probes/module_rearms/restart_ifr，故障期间旧数字/模式字段实时刷新。一次故障探测最坏约1.5秒UART等待，可短暂影响HTTP响应；正常持续输入无此等待。
- 11:51:04 v6 map：slot_data0xF000、metadata0xA0、DMA0x3000/32B对齐，_ebss=0x20176EA8，栈前余量35160B。晚于v6源码但早于v7，不能放行v7下载；v7未目标编译、未烧录/上板，真实384出图及标定未完成。

### 已尝试及结果

- 两次GET17.1诊断成功并差分证明源仍55/重启持续增加；`python3 tools/t384_raw16_bench.py --url http://192.168.17.1/raw16.stream --duration 3 --warmup 0 --timeout 3 --expect-source real`退出2超时，无完整帧，没有新控制写入设备。
- `bash tools/check_raw16_bench.sh`双规格严格语法/各21初始化场景及全部既有UI/网络/SDK/采集/标定模拟/代理回归通过，新增384活状态恢复7场景通过，最终旧map门禁退出1。独立UART夹具亦通过，包括丢ACK、错误模式/格式/FPS、pending不重复写与写入退避；无持久化/探测器/模式设置。
- 随后扩展的最新采集夹具执行产品任务+UART+ISR+真实队列，384 ASan/UBSan通过：停顿→重发→后续确认→真实完整帧逐字节消费，以及超时/读表互斥、错模式fail-closed；LeakSanitizer沿用环境限制关闭，未验证泄漏。新增夹具软件行为不证明实机源停顿成因。
- 诊断按当前字段上界估算5940B含NUL<5952B；Shell语法与其它相关diff检查通过，ISR既有尾空白保留。未自动生成目标固件、烧录、commit/push/发布。

### 下一步

1. 用户MRS增量Build正式V3F的v7，代理核对新map/HEX时间、v7身份、DMA/队列大小/对齐、栈前余量≥32768B；不能下载11:51:04 v6旧HEX。
2. 新产物放行后用户只下载V3F，禁Erase All/Clear CodeFlash；无需继续排查“是否仅MCU复位”，用户已确认整机断电。当前HTTP入口[成像页](http://192.168.17.1/)及[诊断页](http://192.168.17.1/diag)可读，图像未恢复。
3. 单流验证v7模块探测/启流计数、故障期间实时数字/模式、restart_ifr，以及实际连续288行/221184B完整帧；若实时1/1/30及TPD1确认仍无DVP事件，需实际PCLK/H/V输入及IRQ证据，不继续无证据改寄存器。
4. 连续出图后核对字节序和该384自己的数据域/标定；v6 LE中心标准差8.97count、BE2296.54count是字节序候选证据，不单凭平坦场景静默改线上格式，更不套用WN2256公式。标定保存/回读/应用另行实机验收。

### 关键路径与未决

- `firmware/Common/Raw16/t384_frame_source_mini2.c`：fault-only状态确认/启流；`firmware/Common/App/t384_product_config.h`：500ms本地恢复、4秒模块写入退避；`tests/mini2_stream_init_smoke.c`与`tests/mini2_dvp_capture_smoke.c`：实际控制/采集回归；`tools/check_raw16_bench.sh`：严格检查/map门禁。
- 未决：55帧后模组真实输出状态、源停顿物理成因、v7持续出图、新RAM与运行时栈、字节序/成像质量、手机浏览器与正式标定。下面v5/v6内容均为历史，不作为当前下载或完成依据。

## 历史：v5控制已通过，384完整帧0；v6采集修复待Build/上板

### 当前状态

- 实机v5（PN=WN2384，FW=00.00.07.01）已确认数字输出enabled1/format1/fps30、TPD模式1、stream_ready1，UART timeout/bad0；探测器60 Hz和DVP30 FPS分离配置不再阻塞启流。
- 真实采集仍失败：source.frames54/published0/drop54，最后279行/214272B而非288行/221184B；12槽满、acquire_no_slot54、pipeline完成0，FIFO/NCM drop为0，源计数随后停止。3秒只读单流收到HTTP头但无分块/完整帧。逐行IRQ遗漏/中断抢占消费者是待实测解释，不作为已证明硬件根因。
- 源码v6只384启用WCH RM V1.6第29章固定长度DMA接收：每8行/6144B完成一次，按硬件BUF_TOG选完成块，用VSYNC结束验证全帧，不进行JPEG编码、不改变Y16/私有帧流。10×6144B队列加2×6144B DMA暂存，总数据预算73728B；256逐行路径保留，单一全局规格选择仍有效。
- 500ms无DVP事件仅重启MCU接收器，记录dvp.restarts/restart_cr0/restart_cr1；先停止DMA再终止当前帧，读表期间不重启，不新增模组命令。超长帧先abort再复制，短帧/FIFO/无空槽仍严格拒绝，不靠发布半帧恢复画面。
- v6未目标编译、未烧录、未上板验证；当前map/HEX旧，不能放行。末次curl只读访问17.1在沙箱内及授权沙箱外均5秒超时，不能当设备仍可访问。无浏览器可执行文件，不声称看过实际画面/截图；真实标定保存/回读/应用及温度精度未完成。

### 已尝试及结果

- `bash tools/check_raw16_bench.sh`：256/384严格语法、各21初始化场景、真实ISR/队列/ROI/envelope采集夹具、既有UI/网络/SDK协议/黑体采集/标定模拟/只读代理回归均通过，最终旧map时间门禁退出1。首轮诊断格式实参错位被-Werror捕获，修正后完整重跑通过软件部分。
- 采集夹具执行实际产品ISR，仅mock MMIO，覆盖3帧完整字节、消费停顿边界、短帧后bank重同步、超长帧复制前拒绝、FIFO拒绝、DVP停顿重启后完整下一帧及读表保护，两规格通过。
- 384 AddressSanitizer/UBSan采集测试通过；首次LeakSanitizer因ptrace环境不支持退出1，设置detect_leaks=0后通过，不宣称泄漏检查通过。map阶段独立双规格尺寸/32B对齐/32KiB边界夹具通过，不替代目标map；诊断按实际字段上界估算5855B含NUL<5952B。
- Shell语法及其它相关文件diff检查通过；ISR文件diff检查仍报用户既有尾空白，未清理或覆盖，保留其它未提交改动。恢复逻辑额外保护主任务/ISR时间竞争，避免无符号时间差误触发本地重启；对应采集夹具通过。未commit/push/发布，不生成或下载生产固件。

### 下一步

1. 用户MRS增量Build `firmware/T384-RAW16-BENCH.wvsln` 的V3F，不需Clean；代理检查新map/HEX时间及source.kind=v6、slot_data0xF000/metadata0xA0/dvp_row_sink0x3000且32B对齐、栈前余量≥32768B。
2. 新产物放行后由用户只下载V3F，禁Erase All/Clear CodeFlash。新固件预计入口[成像页](http://192.168.17.1/)及[诊断页](http://192.168.17.1/diag)，当前末次请求超时。
3. 代理复验v6/8行6144B/10槽、数字1/1/30及TPD1；单流验证真实完整帧288行/221184B、持续源/发布/流计数和错误。重启计数持续增加则仍未恢复，需实际PCLK/H/V及停顿前CR0/CR1证据，不宣布解决。
4. 完整成像通过后再用该384自己的数据闭环标定，不套用WN2256公式/表；原厂测温型号、数据域及保存/回读/应用仍需验证。

### 关键路径与未决

- `firmware/Common/Raw16/t384_raw16.h`：唯一全局选择；`firmware/Common/App/t384_product_config.h`：DMA块/帧率；`firmware/Common/Raw16/t384_frame_source_mini2.c`：真实采集/恢复；`tests/mini2_dvp_capture_smoke.c`：新增实际ISR回归；`tools/check_raw16_bench.sh`：双规格及map门禁。
- 未决：真实384新路径完整出图、物理时序/电平和源停顿原因、新目标RAM余量、持续吞吐、浏览器与手机兼容性、真实384测温/标定。下面v2-v5内容均为历史，不作为当前下载放行依据。

## 历史：v4已烧录仍0流，v5启动确认窗口待验证

### 当前状态

- 本轮最新真实设备已为mini2-dvp-y16-picture-v4，digital_query_bytes=3；重复读取源计数仍124、数字输出0/0/0，TPD命令未发、stream_ready=0。v4长度修正不足以恢复出图，60 FPS为旧采样值而非当前连续源帧。UART timeout/bad=0、NCM TX drop=0，没有吞吐瓶颈证据。
- 新发现代码确认缺口：三个有效旧状态回读连续执行，没有任何应用等待。v5仅对数字输出确认增加2秒窗口/50ms轮询，通信连续失败3次停止，严格enabled/format/fps及TPD模式护栏保留；这是软件恢复策略，不证明模组实际延迟或2秒原厂保证。新增独立500ms延迟状态和永久关闭测试。SDK示例区分成像型adv_digital_video_output_set与测温型basic_preview_start，但设备PN=WN2384不等同确认WN2384T，未换命令族或发送4D/8D。
- 最新真实复验：用户已烧录v3，17.1可访问；UART timeout/bad均0，DVP设置ACK成功、native60切换已跳过，但连续0x86回读enabled/format/fps均0，流程提前退出，TPD命令尚未发送，流仍503。此前“主机通过”没有解决真实384出图；不能继续把v3当成可用成像固件。
- 查本地原厂SDK真实回调找到确定报文差异：adv_digital_video_output_get要求0x86长度3，旧表/代码请求4。v4已统一启动与确认阶段为3字节，专用构造函数和digital_query_bytes诊断防止再次分叉；保留严格输出/模式回读，不绕过护栏。新增tests/mini2_video_sdk_vectors.py对照SDK设置/查询（含0x86、0x85）的实际报文/CRC，假UART也独立要求3，不再复制原有4字节假设。软件修复未上板验证，不能提前认定查询长度修复足以恢复完整出图。
- 本轮直接读到source.frames=124且随后复查仍124，而NCM计数增加；source.fps_x1000=60000是旧采样值，不能写成持续有效60 FPS。网络曾短暂拒绝连接后恢复，当前仍v3。真实流、数据域、384标定未闭环。
- 历史v2诊断：PN=WN2384、FW=00.00.07.01，探测器60 Hz；每帧288行/221184B、FIFO溢出0，但模式未知、全部丢弃、流503。首个超时为探测器切30，之后模式设置无回执；不能认定30不支持或切帧率必然重启。
- 上一轮v3分离探测器60/DVP30，原生帧率匹配时不发0x44，256仍50/50；增加有界等待和输出/模式回读。本轮v3已证实UART超时消失，但新增回读门槛显露旧0x86长度定义问题，不能再提示v3能出图。
- 旧键mini2.control_detector30_status保留，值4=未发送；没有改变USB/电平/DMA队列/私有帧流/温度公式，没有新增产品静态缓冲。历史10:24:56 v3 MRS产物晚于上一轮输入，map余量33592B、HEX校验均通过，用户已烧录但成像复验失败。当前v4源码晚于该产物，必须重新Build；384测温能力、数据域与标定仍未验证。
- 前一轮按照 `docs/design/SIPEED_THERMAL_USB_NCM_HTTP_PROTOCOL_V1.md` 迁移网络并完善UI；用户明确确认保留现有私有帧流。默认改为192.168.17.1/24、DHCP .17.2–.20、Router/DNS为设备、本地ir.sipeed.com解析和常见Captive Portal路径302到字面IP；设备上游gateway仍0，不做NAT。USB描述符/VID/PID未改，正式USB身份未冻结；ROM MAC无效时CHIPID回退不能证明多设备唯一性。
- 正式网页 `web/raw16_bench_console.html` 增加触摸/鼠标ROI、键盘可用中心区域/清除、全图H/L坐标叠加、5×5中心平均、ROI平均、四种色板选择和PNG保存；原始完整帧的浏览器副本防止下一帧分块污染交互统计。设备页检测实际能力，明确禁用网页升级；384无模型仅标记Y16极值，Picture禁用测温/原始统计。
- 网段持久化/恢复及安全OTA后端尚未接入，统一120B metadata/frame API仍未迁移，不宣称完整协议合规。新增GET network、device明确configurable=false/ota.supported=false；默认采集/读表/Windows入口改17，旧固件18访问需区别。用户纠正参考目录为 `~/dev/pico_tn160`，当前 `/home/yserein/dev/pico_tn160` 及 `/home/slam/dev/pico_tn160` 均不存在，尚未完成TN160源码对照。
- 用户确认384已到货，要求接上后能出图和标定，并保留单一全局宏切回256。此前采购目标为WN2384T，实际诊断PN为WN2384；测温/内置标定支持仍需具体FW与数据证据，不把分辨率等同具体测温型号。
- 正式 `firmware/Common/Raw16/t384_raw16.h` 默认为 `384u`：384×288、每行768B、探测器60 Hz/DVP输出30 FPS配置、6144B/chunk、12槽。改为 `256u` 并重新Build/烧录可回到256×192/50FPS；这是编译选择，不能在已烧录固件中热切换。全量384 RAW16 60 FPS需要13.27MB/s，尚未验证，不通过跳帧宣称60 FPS。
- 当前只完成软件准备。384真实DVP、完整出图、原厂表、黑体标定、保存/回读/应用均未上板验证；384温度模型保持不可用，不能套用WN2256公式/表。
- 用户已MRS Build：2026-09-16 22:16:53的新map确认384，但栈前余量30008B，小于32768B门槛，尚未放行烧录。随后收紧HTTP标定缓冲释放3840B、移除启动标定读取的2KiB局部数组，并修复标定API路径比较及manifest同缓冲编码；这些更新需再次增量Build。
- 2026-09-17 09:02:27 map已刷新，`_ebss=0x201773c8`，栈前余量33848B，标定缓冲0x800/0x900，先前RAM修复已生效；但早于本轮网络/UI源码，不能作为最新下载放行依据。
- 现有相邻桌面Scope解析与实验二点采集支持384尺寸；本轮只读检查，没有修改或构建该工程。原厂SDK文档支持WN2384T内置两点，当前T384固件未接入该动作。实验模型与原厂标定必须分开报告。

### 已尝试及结果

- 当前v5：独立256/384各21初始化场景通过，新增500ms状态延迟成功/永久关闭有界拒绝。完整bash tools/check_raw16_bench.sh双规格严格语法及既有全部smoke通过，最后因11:10:43 v4 map早于新配置退出1；Shell语法/本轮配置脚本和文档差异检查通过。v5未MRS构建/上板，延迟确认是否解释实际故障未知；保留所有用户已有改动。
- 当前v4：`bash tools/check_module_files.sh`退出0，新增原厂SDK视频setter/getter实际报文对照通过（0x86长度3）。最终 `bash tools/check_raw16_bench.sh` 双规格严格语法、各19初始化场景/短文本保护/原厂SDK契约及既有全部smoke通过，最后因旧v3 map时间门禁退出1。首轮全工程语法发现http_status.c缺协议常量头文件，补齐后重跑通过；Shell语法及本轮其他文件diff检查通过，用户已有ISR尾空白保留。现有10:24:56 HEX仍v3，代理未生成/烧录v4。
- 原厂串口V0.4 sheet2行70/71/92/93及SDK枚举确认384支持30/60 Hz；离线SDK回调核对0x44与0x46的30/60参数/CRC均匹配，无硬件I/O。新增真实初始化代码+假UART/时钟19场景双规格回归，并保护短文本身份查询。首次主机汇编遇到vendor fence.i，仅测试夹具stub中断禁用后通过；诊断容量粗估每字符串65B断言超限，按实际字段上界复核5773B<5952B，无须扩RAM。
- 最终复跑 `bash tools/check_raw16_bench.sh` 双规格全部主机语法/初始化19场景/文本身份保护/既有smoke通过，最后旧map时间门禁退出1；随即检测到10:24:56新产物，单独执行原脚本map阶段（含全部输入mtime）退出0，profile384/12槽/73728B/metadata192B/NCM0xD010，_ebss=0x201774c8，栈前余量0x8338=33592B。ELF字符串和HEX校验/新身份检查通过。`bash -n`通过；差异检查仍发现用户原有ISR尾空白，未清理或覆盖该格式改动。修复上板未运行。
- 最新 `bash tools/check_raw16_bench.sh` 双规格主机严格语法、UI/DNS及全部既有smoke通过，最后map过期退出1。`bash tools/check_module_files.sh`退出0；独立DHCP真实OFFER/ACK函数选项字节、DNS A/AAAA/EDNS/NXDOMAIN/边界和UI 256/384原始数据/ROI/触摸事件/模型保护检查通过。初次完整检查运行期间脚本有修改，出现语法错误；固定后 `bash -n` 与完整重跑不再复现。独立完整帧Python首次漏传脚本参数报IndexError，补齐正确参数后通过。
- Playwright运行包可读取，但Chromium/WebKit可执行文件不存在，未运行真实浏览器/截图；不自动安装。UI测试为VM DOM/canvas夹具，不替代Safari/Chrome真机。未上板/烧录/Flash写入。
- 修复两个确定遗漏：黑体采集在384尺寸下被旧WN2256实验温度头拒绝；批量/Windows读表入口强制旧WN2256 PN/FW/SHA。主机bench与采集现在按HTTP头支持256/384；读表默认保留当前身份/CRC保护，旧SHA首验改为显式选项。
- `tools/check_raw16_bench.sh` 补双规格目标源码主机语法及pattern/pipeline检查，map尺寸按当前全局规格派生，32KiB栈前余量门槛保留。
- 256/384 Y16/Picture协商与完整帧、384拒绝旧公式、读表普通/显式旧SHA及首验失败停止回归通过；map两规格尺寸与32KiB边界夹具检查通过，夹具不能证明目标RAM。
- 本轮 `bash tools/check_raw16_bench.sh` 双规格主机语法及全部smoke通过，整体退出1：map早于最新http_status.c，需要再次MRS增量Build。新增384黑体采集集成夹具通过：保存221184B帧，中心ROI=(184,136)。Windows PowerShell只解析操作脚本语法通过，未执行设备操作。
- 新map `_ebss=0x201782c8`、固定栈起点 `0x2017f800`，栈前余量30008B，32KiB门禁实查退出1。最新缓冲修改预计增至33848B，估算不能替代重新链接的map；不能降低门槛。
- `bash tools/check_module_files.sh`退出0；新增真实HTTP处理函数配合假TCP/RAM Flash的384最大payload上传、commit、manifest、启动回读检查通过。修复原API路径长度多比较1字符造成404、manifest输入输出缓冲重叠；不改变API路径/格式，未实写硬件Flash。
- 两次只读请求 `http://192.168.18.1/diag` 均连接超时（curl退出28），没有获得新模组身份或实时帧。`docs/logs.txt`当前是相邻Scope Qt构建错误，不是384上板日志。

### 下一步

1. 用户MRS增量Build正式V3F的v5（只改源码，无需Clean）；代理核对新map/HEX新鲜度及32KiB余量。当前11:10:43产物属于已上板失败的v4，不得当作v5重新下载。代理不烧录或写标定。
2. 新产物通过后用户只下载V3F，禁Erase All/Clear CodeFlash，再对USB供电模组断电重连。入口http://192.168.17.1/当前v4可读；不迁移网段或改USB身份。
3. 代理复查source.kind=v5、digital_query_bytes=3、digital enabled1/format1/fps30、TPD设置与模式1、stream_ready=1。再检查实际持续源计数、288行/221184B完整图像及pipeline水位；若等待窗口后仍回读0，延迟生效假设未获支持，需真实UART响应/具体型号启流协议证据，不继续猜延时或绕过护栏。
4. 标定使用新384自己的数据：核实原厂表/gain/Vtemp/FFC与数据域，完成黑体采集、模型/表身份绑定、保存/回读/应用及独立温度点。现有Scope实验0/50°C流程不能宣称OEM或-20～150°C精度；内置标定命令与写入语义需具体PN/FW证据。
5. 挂载真实TN160参考工程、完成浏览器ROI/极值/图片验收。网段持久化与网页OTA需先冻结Flash分区/恢复方式/bootloader与固件包校验方案，不直接添加任意BIN写入接口。

### 关键路径与未决

- 全局选择：`firmware/Common/Raw16/t384_raw16.h`；DVP/帧率/温度保护：`firmware/Common/App/t384_product_config.h`；ring：`firmware/Common/Raw16/t384_frame_pipeline.h`。
- `tools/t384_raw16_bench.py`、`tools/capture_radiometry_calibration.py`、`tools/read_mini2_module_files.py`、`tools/windows_user_action.ps1`、`tools/check_raw16_bench.sh`；相邻Scope `src/plugins/t384/` 当前仅只读。
- 未决：本轮初始化修复的真实反馈、接线电平/时序、12槽持续消费能力、WN2384/FW00.00.07.01测温与内置两点支持、真实标定/Flash回读/应用、-20～150°C环境温区/允许误差。最新map余量通过；有旧v2真实DVP诊断，没有384完整出图/修复上板/发布结论。

## 历史：2026-09-15暂停，等WN2384T到货

### 当前状态

- **用户明确暂停：等购买的WN2384T到货。** 不再重复WN2256读表，不再安排当前持续流测试；用户要改善测温，不是证明成像能用。
- 目标温区 **-20～150°C**；环境温区和允许误差未确认。
- 原厂主机两点重标定输入包括原始KT/BT、NUC-T、Vtemp等及环境修正参数；模组内置两点接口是另一机制，输入点编号和黑体温度。
- AC020 2.4.5内置两点接口明确列 **WN2384T、SE51280T、TC2-C**，未列当前WN2256；不能泛化为所有256不支持或所有384支持。WN2384T具体FW/UART与精度未上板验证。
- 当前WN2256 / FW 00.00.08.03已取高低NUC-T各32768B/16384项；SHA均718d61a69cced663015d509973e75af07920453461b582cbbd367ca98210e4de。KT/BT四项C7 status=1零长度，distance无可信后缀本地-9，未发送打开。
- 成像/传输/录像不依赖KT/BT；实验公式T=(Y16-38659.97)/118.28，50°C实机约48°C。正式OEM测温未完成，仅两个点不能保证全温区/温漂精度。

### 已尝试及结果

- USB探针KT/BT SDK-902，UART同一已知路径C7拒绝；换通道未补齐数据，不能认定物理无表。
- 只读代理C7→87→86→46，512B分块、CRC/前后身份校验，复用RAW16 ring，不新增32KiB表RAM，DVP暂停直到下载ACK/取消/超时释放，不写MINI2。
- 修复C7明确拒绝误追加CLOSE与本地路径拒绝误锁定两处根因。最新八事务均退出无HTTP409/cleanup_failed，两档NUC-T成功；连续DVP帧恢复仍未独立验收。
- 原厂主机两点算法仍需原表；模组内置两点WN2384T有文档支持。自主模型可替代原表，但需自行验证非线性/温漂/增益补偿。
- 10分钟持续流入口偏离用户本轮数据/精度重点，保留可选工具，停止将其作为下一步。

### 下一步（到货后）

1. 用户提供WN2384T实际PN/SN/FW，确认环境温区、允许误差与黑体能力。
2. 代理先只读核对身份、原表可读性、gain/Vtemp/FFC及数据域；不迁移WN2256表和分辨率配置。
3. 核对该固件内置两点指令、UART回执及取消/清除/保存语义，形成具体方案；实际重标定改变校准状态，需要用户明确授权。
4. 按原厂P1低温→P2高温执行获授权标定，使用未参与拟合点及不同机芯温度、FFC/增益条件验收-20～150°C。
5. 数据与精度证据统一更新docs/data/read_report.txt，保持实验/OEM与文档支持/实测支持分离。

### 关键相对路径

- docs/data/read_report.txt：唯一数据汇总；docs/runbooks/PROJECT_MEMORY.md：经验与下次提示词。
- .agents/skills/t384-radiometry-calibration-closure/SKILL.md：复用接手流程；docs/design/SELF_RADIOMETRY_ARCHITECTURE.md：自主架构候选。
- out/radiometry/mini2-uart/20260915T094510.291591Z/：最新两档NUC-T bin与全部事务JSON。
- firmware/Common/Raw16/t384_module_files.{c,h}、tools/read_mini2_module_files.py、tools/check_module_files.sh：只读代理和检查。
- docs/data/SDK/Win_Linux/AC020_win&&linux_SDK_2.4.5/AC020_win&&linux_SDK/libir_SDK_release/include/libircmd_temp.h:269：内置两点；同目录libirtemp.h:1011与libir_sample/sample/multi_point_calibration/src/sample.cpp:470：主机算法输入。

### 验证状态

- bash tools/check_module_files.sh：16场景、原厂SDK回调字节对照、HTTP ACK/断开/背压、主机完整性/清理检查通过；实机bin独立sha256sum匹配原厂。
- bash tools/check_raw16_bench.sh：JavaScript/主机语法及全部smoke通过，最终退出1，内嵌页面更新后map过期需MRS增量Build；未运行WCH目标构建。
- WN2256只读事务恢复和NUC-T下载已实机验证；页面诊断修复、Windows新增ValidateStream分支与10分钟持续运行未验收。
- **WN2384T未到货，全部功能与精度未上板验证。** 未调用重标定、未写MINI2二次标定区；未commit/push/发布。

### 未决问题

WN2384T实际FW与原表/内置标定支持、UART与保存语义、SNR/NUC数据域、Vtemp索引/同帧gain/FFC/epoch、环境温区/允许误差、光学条件与全量程独立精度、多平台与真实DVP恢复。

## 以下为历史过程：以顶部暂停状态为准

## 历史：MINI2 原厂表只读固件代理

### 当前状态

本轮用户决定先完善当前可运行项目，暂缓重复读取KT/BT拒绝路径。
成像/传输/录像及既有实验公式无KT/BT运行依赖，OEM正式测温仍保留缺表保护。
修正页面诊断响应正文超时与定时器清理、模型不可用提示；同步内嵌页面。
Windows固定脚本新增-ValidateStream（当前WN2256 256x192实源，默认600秒）并保存out/stability结果。
运行边界见docs/runbooks/CURRENT_IMAGE_ONLY_OPERATION.md；本轮Windows分支与实机持续验收未运行。
完整主机JavaScript/语法/smoke通过；check_raw16_bench最终退出1，因内嵌页面更新后map过期，需MRS增量Build。

当前最新20260915T094510.291591Z：八事务全部完成，无锁定；NUC-T两档各32768B，
sha256sum均匹配原厂。KT/BT两档均实际C7 status=1拒绝，distance两档本地-9未发送C7。
两处错误恢复已实机验证可继续事务；真实DVP连续帧恢复仍未验证。
下一步聚焦原厂PN/FW受支持表接口/路径与距离后缀映射，不能靠重复同一命令补齐。
下方历史过程保留用于回溯，以本段和docs/data/read_report.txt当前结论为准。

最新批量20260915T094316.821967Z：C7拒绝恢复已实机通过，KT/BT-high各自拒绝后继续；
distance-high本地缺后缀-9误触发cleanup_failed，低增益仍HTTP409未尝试。
进一步修复未发送C7时不锁定，补距离拒绝后nuct-low完整读取回归；
16场景、SDK/HTTP/主机检查通过，最新NUC-T SHA一致；此进一步修复待MRS构建/烧录。

最新实机：两次UART nuct-high SHA与原厂完全一致，下载后暂停状态解除。
kt-high C7 status=1后旧固件追加CLOSE status=1导致cleanup_failed，后续表未实际读取。
已修复CRC有效空正文C7明确拒绝的退出路径，保留不确定错误保护；修复版待构建/烧录。
最终所需数据清单与证据统一汇总到用户指定 `docs/data/read_report.txt`，保留历史USB报告。

按 `docs/logs.txt` 和用户本轮确认，已实现八枚举只读代理。用户额外确认：复用RAW16 ring，保持CH32 DVP暂停直到下载完成/取消/超时后恢复；不新增32KiB表RAM、不写MINI2。尚未MRS构建或烧录，正式OEM测温链仍未完成。

### 已尝试及结果

- 原厂SDK反汇编补齐日志遗漏的87信息查询；文件ID由主机分配。纯内存SDK callbacks与固件四类命令逐字节对照通过；未访问硬件。
- 状态机14场景、HTTP ACK/断开/背压3场景、主机长度/身份/CRC/SHA/manifest检查通过；全套主机检查通过后停在旧MRS map门禁。公共preflight无T384选项（invalid choice），没有冒用其他项目。
- UART CRC为真实传输校验；整文件CRC32仅本地计算，没有假称原厂CRC。距离PN无后缀时本地-9，禁止猜F1；这不是SDK-902。
- 测试夹具曾有超时队列游标错误及WCH头文件路径缺失，已修正；非固件/上板失败。保留了工作区原有成品标定/显示/协议改动，未commit/push。

### 下一步（用户/代理）

1. 用户：MRS只增量Build V3F，检查新map/RAM/栈和链接模块，再按原流程烧录（禁止Erase All/Clear CodeFlash）。
2. 用户：关闭所有RAW16客户端，执行固定 `tools/windows_user_action.ps1`，默认nuct-high首验。已知WN2256必须32768B/16384项、SHA `718d61a69cced663015d509973e75af07920453461b582cbbd367ca98210e4de`。
3. 代理：分析输出JSON和新map；首验成功后再分析KT/BT真实打开状态。距离表先取得原厂后缀证据。
4. 用户/代理：验证取消、断网、串口异常、重复读取句柄清理及恢复后的完整RAW16流；关闭失败锁定后禁止盲目循环重试。
5. 桌面Scope页面不在本轮修改内；相邻项目实际位于 `/home/yserein/Project/yserein-scope`，后续接入应遵守其Provider生命周期规则。

### 关键相对路径

`firmware/Common/Raw16/t384_module_files.{h,c}`、`t384_module_files_http.{h,c}`、`firmware/Common/App/http_status.c`、`tools/read_mini2_module_files.py`、`tools/check_module_files.sh`、`docs/runbooks/MINI2_READONLY_TABLES.md`、`docs/reference/MINI2_READONLY_FILE_PROTOCOL.md`。

### 验证状态

主机检查通过；旧map不能代表新固件。未上板验证，未验证Windows或其他目标平台，没有新HEX交付。

### 未决问题

MINI2实际UART文件互操作、SN是否可完整读取、距离PN后缀、原厂表是否暴露、真实DVP恢复、新增小缓冲后的RAM/栈余量；SNR/NUC数据域、Vtemp索引、gain/FFC/epoch和正式精度仍未闭环。

## 本轮新增：成品标定事务首版（进行中）

- 新增 `firmware/Common/Raw16/t384_calibration_storage.{h,c}`：双槽、generation、CRC32、identity/profile/gain 校验；默认槽地址 `0x2E000/0x2F000`，待真实 Flash 上板确认。
- `firmware/Common/App/http_status.c` 新增 `/api/v1/calibration/v1/manifest`、`/data`、`/commit`、`/abort`；PUT 暂存完整包，commit 写备用槽并切换 active generation，旧 `/raw16.stream` 与 `/diag` 不变。
- `yserein-scope/src/plugins/t384/` 已接入上传、提交、撤销、回读和实验实时显示；模型固定为 `t384-empirical-2point-v1`，仍明确 `experimental/non-OEM`。
- 已验证：固件 HTTP/存储 GCC 语法、双槽 storage smoke、Git diff 检查通过；`bash tools/check_raw16_bench.sh` 在旧 map 时间门禁处停止，需用户 MRS 增量 Build；Qt6 构建未运行（环境缺少 Qt6）。
- 未验证：CH32 真 Flash 写入/断电恢复、Windows 上传回读、T384/T640 实机、绝对温度精度；本轮未烧录、未提交、未 push。

## 此前 30 秒恢复（辐射标定背景）

- **当前产品链**：MINI2/WN2256 8-bit DVP → V3F 有界分块 pipeline → lwIP/TCP → TinyUSB NCM → 本地 Web。真实验证件仍为 `256×192 WN2256 / FW 00.00.08.03 / 50 FPS`，不代表 T384/T640 正式完成。
- **当前网页模型**：256u 使用 `experimental-blackbody-2point-v1`，公式 `T=(Y16-38659.97)/118.28`；依据 Windows NCM 高增益 0°C/50°C、1 cm、发射率 0.98 的各30帧采集。用户实测 50°C 黑体页面约 48°C、手掌约二十多°C；模型只证明趋势可用，仍不是 OEM 辐射测温。
- **黑体证据**：`C:\Serein_Y\Sipeed\T384\out\radiometry\wn2256\0C-high` 与 `50C-high` 各30帧、每帧98304B；0°C ROI均值38659.967、帧间std4.91；50°C均值44574.201、std2.58；分析结果 `out/radiometry/wn2256/blackbody-pair.json`/`blackbody-pair-rerun.json`。
- **已读取原厂数据**：VMware 原厂 USB 探针报告 `out/radiometry/wn2256_probe_vm/read_report.txt`；PN=`WN2256`；高增益 Ktemp/Btemp/Address_CA=`-14790/15219/6990`；低增益=`-12288/14400/10000`；高低增益 NUC-T均16384项/32768B，SHA-256=`718d61a69cced663015d509973e75af07920453461b582cbbd367ca98210e4de`。
- **仍未完成**：WN2256 KT/BT与距离表读取返回 SDK `-902`；SNR/NUC 数据域、gain 语义和 Vtemp→索引未确认；正式 `t384_radiometry` 运行链未启用；未向 MINI2 写二次标定区。当前不能宣称“正式测温已完成”或“只差黑体标定”。
- **工具已完成**：`tools/capture_radiometry_calibration.py`、`tools/analyze_blackbody_pair.py`、`tools/windows_capture_t384_blackbody.ps1`、`tools/validate_radiometry_tables.py`、`tools/read_wn2256_calibration.sh`；原厂探针和工具已同步到VMware `~/dev/T384`。
- **验证**：`bash tools/check_raw16_bench.sh`、radiometry/manifest/blackbody smoke 均通过；VMware 内原厂探针工具链曾编译并成功读出 PN/Ktemp/Btemp/Address_CA/NUC-T。实验模型的板上显示已由用户验证为“50→约48°C、手掌二十多°C”。最新固件是否已烧录以用户板上 `/diag` 为准；本轮未替用户构建、烧录或写表。
- **本轮状态**：阶段性暂停扩展功能，先保留当前实验模型和数据，下一次从误差归因开始。不要立即把二点模型写入 MINI2，也不要继续猜 KT/BT 路径或 PID。
- **用户下一步**：如果继续标定，只需按标定手册在稳定条件下重复受控黑体采集/记录环境信息，并由用户负责 Windows、MRS、烧录和黑体操作；不需要再采集本轮已经完成的 0°C/50°C 高增益样本。
- **代理下一步**：先计算 48°C 偏差、ROI/FFC/距离/发射率影响，确认新的单变量假设后再提出代码行为变更；正式包必须绑定 PN/SN/FW、gain、FFC、epoch、表长度、SHA-256 和数据域声明。
- **关键路径**：[工程](firmware/T384-RAW16-BENCH.wvsln)、[辐射核心](firmware/Common/Raw16/t384_radiometry.c)、[实验模型配置](firmware/Common/App/t384_product_config.h)、[黑体分析](tools/analyze_blackbody_pair.py)。
- **本轮新增沉淀**：[辐射标定闭环 Skill](.agents/skills/t384-radiometry-calibration-closure/SKILL.md)、[长期记忆](docs/runbooks/PROJECT_MEMORY.md)。

## 未决与风险

- 当前“能连续成像”已确认，但活动窗口严格无丢帧、10 分钟/24 小时稳定、断连恢复、DHCP 续租和多平台验证未完成。
- DVP BE/LE、采样沿、同步极性和 TPD 数据阶段必须以实机/逻辑分析确认；本地 USB 资料的 little-endian 不能直接替代 DVP 证据。
- WN2256 是非测温验证件；当前经验温度不能升级为正式辐射结果。正式表来源、SNR/NUC 数据域、Vtemp→index、gain/FFC 绑定未闭环。
- 384u 只完成主机编译/smoke，尚未 WN2384/T384 实机；不能把切宏等同于正式产品完成。

---

## 历史交接（截至 2026-09-02，仅供追溯）

- **当前结论**：正式 `firmware/` 工程的 DMA 等效 source 已完成 Windows 60 秒严格验收：`1665 帧 / 27.750 FPS / 6.138 MB/s`，超过 `25.5 FPS / 5,640,192 B/s` 目标；序号缺口、逆序、不完整帧均为 0。这个结果证明现有 RAW16 下游链路有能力跑过 25.5 FPS，不证明真实 MINI2 DVP 已完成。
- **突然提升的有效改动**：`-O2` 只把约 `22.9` 提到 `23.9 FPS`；lwIP 通用 checksum-on-copy 反降到约 `23.7 FPS`，已回退；项目自有的融合 copy+checksum 才把结果提高到约 `27.7 FPS`。不要回退 `firmware/Common/App/arch/cc.h` 的 `t384_lwip_chksum_copy()`。
- **工程边界**：`firmware/` 只放唯一正式源码/MRS 工程；已验证 RAW16 快照在 `tests/ch32h417 t384 raw16 bench/`，NCM/OV2640 快照在 `tests/ch32h417_t384_ncm/`。快照必须保持可独立迁移，不能抽成依赖 `firmware/` 的共享目录。
- **构建规则**：正式入口为 `firmware/T384-RAW16-BENCH.wvsln`，芯片项 `CH32H417WEU`，只构建/下载 V3F，`Erase All=false`、`Clear CodeFlash=false`。普通源码修改使用 Incremental Build；只有芯片/linker/工程资源/路径迁移或缓存失效时才 Clean。
- **迁移规则**：MRS tracked metadata 只用相对 linked folders；`.mrs/` 和 `V3F/obj/` 是本机生成缓存。根目录 `designs/` 禁止创建，设计资料只放 `docs/design/`。
- **Git 状态**：最近提交是 `ded420f`（`feat: establish portable T384 firmware baseline`）。当前未暂存修改有 `tools/t384_raw16_bench.py` 和本项目 Skill；`HANDOFF.md`、`docs/` 及若干 README 当前仍是未跟踪文档，不能在未获授权时纳入代码提交。
- **本 session 唯一代码改动**：`tools/t384_raw16_bench.py` 在显式 `--expect-source real` 时不再生成无用的 221,184 B 期望帧，并缓存 synthetic 校验的 `memoryview`；不改固件、协议或 CLI 输出。`bash tools/check_raw16_bench.sh` 和 real/synthetic 定向测试均通过。
- **板上身份**：本 session 没有重新 MRS 构建或烧录。板上仍是已通过 27.750 FPS 的融合 copy+checksum 版本。
- **当前首要未闭环**：RAW16 流断开后 HTTP 端口曾超过 25 秒短时拒绝；尚未完成 10 次断连恢复、10 分钟持续测试、DHCP T1/T2/休眠恢复、真实 MINI2 DVP、T640 和正式测温。
- **下一控制动作**：保持吞吐数据面不变，先只读复现 RAW16 客户端关闭后的 HTTP 恢复问题；定位 TCP PCB/客户端释放/TIME_WAIT 后，只改一个变量。恢复通过后再做 10 分钟 `≥25.5 FPS` 持续验收。

## 下次开工顺序

1. 依次读 `AGENTS.md`、本文件、`docs/runbooks/PROJECT_MEMORY.md` 顶部、`README.md`、`/home/slam/Sipeed/C_context/KNOWN_FAILURES.md` 和 `.agents/skills/t384-raw16-pipeline-closure/SKILL.md`。公共 preflight 没有 T384 项，记录限制，不冒用其他项目。
2. 运行 `git status --short`，先识别当前未暂存工具/Skill改动和未跟踪文档；不要覆盖、自动暂存或把 docs 混入代码提交。
3. 运行 `bash tools/check_raw16_bench.sh` 建立静态基线。它检查相对 MRS 路径、主机 GCC、分块协议、pipeline、checksum-copy 和可用 map，但不等于新 MRS 构建或上板。
4. 若继续稳定性闭环，先复现断流恢复，不改当前融合 checksum-copy、USB 描述符、ring 所有权或 NCM 缓冲；10 次开关流后根页和 `/diag` 应在 2 秒内恢复且 connect/disconnect 配平。
5. 再做 Windows 10 分钟严格测试：完整帧 `≥25.5 FPS / ≥5,640,192 B/s`，序号缺口、不完整帧、source drop、pipeline abort/protocol error、HTTP write error/timeout、NCM TX drop 均为 0。
6. 真实 MINI2 到板后只替换 source adapter，先测 DVP/I²C 1.8 V、电平切换、PCLK/HSYNC/VSYNC、采样沿、RAW16 字节序、信息行与整帧边界；未实测前不得称产品固件完成。

## 关键路径

- 正式工程：`firmware/T384-RAW16-BENCH.wvsln`
- 已验证 RAW16 快照：`tests/ch32h417 t384 raw16 bench/T384-RAW16-BENCH.wvsln`
- 融合 checksum-copy：`firmware/Common/App/arch/cc.h`
- source 边界：`firmware/Common/Raw16/t384_frame_source.h`
- pipeline：`firmware/Common/Raw16/t384_frame_pipeline.c`
- HTTP RAW16：`firmware/Common/App/http_status.c`
- 主机严格工具：`tools/t384_raw16_bench.py`
- 静态闭环：`tools/check_raw16_bench.sh`
- 项目记忆：`docs/runbooks/PROJECT_MEMORY.md`
- 项目 Skill：`.agents/skills/t384-raw16-pipeline-closure/SKILL.md`
- 设备入口：[页面](http://192.168.18.1/)；[诊断](http://192.168.18.1/diag)

## 验证状态与风险

- 已验证：DMA 等效 source 的 Windows 60 秒完整帧吞吐 `27.750 FPS / 6.138 MB/s`；错误计数为 0。
- 已验证：本 session 的主机工具内存优化通过 `bash tools/check_raw16_bench.sh` 与定向 Python 测试。
- 未验证：本 session 没有 MRS 构建、没有新 HEX、没有烧录；工具改动不需要烧录。
- 未上板验证：真实 MINI2 DVP、电气/时序/字节序、T640、KT/BT/NUC-T 测温、多平台兼容、10 分钟/24 小时稳定性。
- 诊断命名坑：DMA 等效源当前 wire flag 为 non-synthetic，严格工具可能显示为 `real/frame-integrity-check`；判断真实来源必须看 `/diag source.kind`，不要把该标签当成真实 MINI2 证据。修改这段用户可见语义前需先取得用户确认。

## 未决问题

- RAW16 客户端断开后，HTTP 连接资源为何短时不能在 2 秒内恢复。
- DHCPREQUEST 是否正确支持只带 `ciaddr` 的标准 T1/T2 续租；修复前不能声明 24 小时稳定。
- 真实 MINI2 是 V3F 同核 DVP，还是需要 V5F→V3F 跨核共享数据面；后者必须先验证共享 RAM/cache/屏障/通知。
- 正式 USB VID/PID、目标主机系统矩阵和测温标定数据仍未冻结。
