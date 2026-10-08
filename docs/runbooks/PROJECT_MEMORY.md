# T384 项目记忆

## 2026-10-08：640 两点标定保存与显示已闭环，其他验证延期

本节覆盖下方旧阶段描述。用户最终确认“确实可以了，我已经验证过了，其他验证晚点跑”；下次不得从旧 Picture/20 FPS、等到货或原厂缺表分支重新开始。

用户随后要求提交本轮版本：源码、HANDOFF、本节和项目skill同批归档，原始采集数据与固定临时操作脚本仍保留在本地，不自动push。

### 用户偏好与分工（反复纠正后的明确结论）

- 用户要工程上尽量能用、尽快闭环，不接受把理论完美、所有附加验证当作眼前任务的前提。先解决指定问题；标定任务不自动扩成吞吐、零丢帧、多平台或OEM研究。
- **从编译、构建、产物检查到烧录、现场验证都是用户工作。代理不再说“构建后先给我确认新 map，再允许烧录”。** 源码中的边界断言、静态内存分析仍要负责；不要把这条偏好解释成可以猜地址或取消完整帧校验。
- “不能的话差在哪里，是你没改完就改完来”：要检查采集、拟合、保存、回读、运行时加载、网页显示整个用户流程，别交付采集脚本后才让用户发现不能保存/显示。已获具体授权的工作继续完成，不重复询问。
- 先看当前源码判断已实现能力，不反复要求用户解释已有profile；旧 `logs.txt` 可能是另一模组的历史快照。用户明确今日640后，旧WN2384日志不能成为反复质疑当前连接的依据。
- 同一设备只标定当前安装的模组，采用公共A/B槽；不为256/384/640分别浪费一组分区。保留两槽是为了写入故障保护，不是为了多SKU同时存储。
- 中文短句、结论在前；“看logs.txt”立即完整重读 `docs/logs.txt`。用户执行多行操作时封装脚本，最终给真实Windows路径的一行指令及完整网址。
- 用户已确认可用时接受现场反馈；其余验证说晚点就不继续跑。交接三分钟内完成，顶部三十秒可恢复；只有明确要求才改HANDOFF/memory/skill，不擅自提交或推送。

### 本次实证与当前实现

- 640为TIFSC640/FW01.00.01.03；V5F同时负责采集/网络，V3F启动/握手。mode1小端16-bit输入→Y16BE，IPC v7，默认640且Y16开关开启。
- mode1临时探测119完整物理帧/2秒，512行/655360B、坏帧/FIFO0；LE ROI sigma3.36、BE860.99，原始prefix的LE范围21452..21460。恢复mode0并验证3帧Picture成功。它先证明数据格式，不直接证明温度精度。
- 完整流10.034秒188帧/18.736FPS/12.279MB/s，缺口409、partial/order0；并发diag曾WinError10054，整体stable=false。不能只看控制台成功窗口或把诊断失败说成视频全失败。
- 0/50°C各30帧：ROI16768.112/23930.790，帧间sigma31.464/6.317count，折算约0.220/0.044°C标准差（不是绝对误差）。存储模型`T=(Y16-16768)/143.26`，applied/stored true；用户后续确认可用。gain unknown保留，不虚构自动读取成功。
- 证据根目录：`out/radiometry/blackbody/20261008T032318.276198Z/`。`apply-20261008T032344.157613Z/result.json`、`before.packet`、`model.packet`已核验；旧384包generation890820277→新890820278为正常继承，初始上传包generation0由设备改写。

### 错误教训与可复用方法

1. **地址表达式要按链接器语义核对。** `.stack`内部条件表达式里的字面地址被当作段相对值，`0x2017F800+0x20138800=0x402B8000`。正确做法是在输出段外定义并用`ABSOLUTE(...)`；既要余量断言，也要精确地址等值断言。源码算术合理、链接成功都不保证符号地址正确。本次错误由代理引入，应直接承认修复，不归咎用户操作。
2. **不同协议资料可能描述不同枚举。** V0.4的0x45/85=IR/KBC等，AC020 SDK的WN2/TIF中间流=Picture0/TPD1。先查完整class/index/长度和支持系列，再用当前PN/FW实机回读与物理数据裁决；不要只读SDK搜索结果就说找不到型号，也不要把KB后数据当已证明SNR/NUC域。
3. **Picture无损不等于热数据无损。** 旧640 packed保存8-bit亮度+块内UV，不能恢复16-bit Y16。Y16需要独立映射和字节序归一化，不能只打开温度公式。
4. **按实占回收内存。** SRAM是896KiB（128ITCM+256DTCM+512共享），960KiB是CodeFlash。当前raw ring43+32+26+9槽×5120=550KiB，不是640KiB整帧缓存；边采边发、整帧跳过和半帧丢弃可以用于工程闭环，不能伪称零丢帧。V3F约11.1KiB代码保留16KiB，堆上界0x20138800、旧实占下剩33440B；`_sbrk`确实使用该上界。两核NOLOAD、slot映射、启动物理探针、IPC版本与断言必须一致。
5. **先验证模式，再做整流与标定。** 临时探测在USB初始化后主循环才发送一次mode1，ISR只记录/截止DMA，2秒后一次恢复mode0并验证Picture；不因一次试验叠加帧率/FFC/保存参数命令。后续持续Y16由用户单独明确授权。
6. **标定完成要有用户可用出口。** T640最初只会生成report，存储后端虽有但apply脚本限制384、固件/网页也拒绝640；必须全部接通。新入口`--apply`先在采集前检查应用接口，避免用户操作完黑体才发现固件不支持保存加载。
7. **保存证据与运行状态分开。** before.packet备份、CRC/SHA、实际参数与generation回读校验；读取manifest两次仅比较持久字段，不能因动态applied变化就误报数据变更。失去提交响应时保存状态可能未知，先`--verify-saved`，不盲目重复写入。
8. **兼容性以具体结构证明。** 公共槽仍用原384偏移0x50000/52000，schema1、112B头、16B `<HHIIi>`参数不变，384入口保持。v1保存点只支持非负整数（当前0/50）；分析可以做负温不等于已能保存任意有符号/小数点。不要偷偷改格式。
9. **资源同步也是交付的一部分。** 页面源、普通HTTP C include、gzip C include要一致；`embed_device_console.py --gzip-output ... --check`可检查mtime0压缩、Content-Length和解压正文。仅生成C资源文本，不自动构建固件。
10. **只报实际验证。** 新C/Python/UI夹具已写不等于跑过。代理本次只运行只读证据核对、语法/AST/资源一致性和差异检查，用户负责实机。早期分配、旧map或过期子agent输出不是当前证据；父代理应读回核实具体finding。HTTP与probe任务同V5F主循环，不能误报成并发写入并给阻塞UART套ISR seqlock。

### 关键边界与当前入口

- 公共标定Flash：A `0x08050000`、B `0x08052000`，每槽8KiB物理预留、4KiB逻辑、payload≤2KiB，共16KiB；标定RAM约4.5KiB。旧640/256区域不自动迁移/擦除，释放预留不等于自动扩大固件代码区。
- 旧384包格式/地址已核对兼容；同一板改为640并重新标定会更新公共槽中的当前模型。不能承诺跨SKU同时保留独立活动模型。
- 当前只是两点实验模型；不以原厂NUC-T可读、Y16>>2或页面温度显示宣布OEM完成。保存前主机比对PN/SN/FW，固件加载检查profile/包/参数/数据格式；不要把它说成逐帧gain/FFC/epoch已绑定。
- 保存入口：`py -3 "C:\Serein_Y\Sipeed\T384\tools\calibrate_t640_blackbody.py" --apply`。默认0/50°C、各30帧、0.01m/0.98，使用前按实际条件核对；默认无--apply仅采集。已有报告可`--apply-from`，只读校验用`--verify-saved`。
- [页面](http://192.168.17.1/) / [诊断](http://192.168.17.1/diag)。写入前关闭流；始终不写MINI2原厂标定区，不Erase All/Clear CodeFlash。
- `calibrate_t640_blackbody.py`、`t640_calibration_apply.py`、`calibration_storage_client.py`、`http_status.c`、网页与两份include组成当前完整链。`tools/windows_user_action.ps1`提供只读表探针/mode探针结果/Y16流检查，可能被Git忽略，不能仅靠git diff判断文件是否改变。

### 下次一次性提示词

```text
接手 /home/slam/Sipeed/T384。先读 AGENTS.md、HANDOFF.md 顶部、docs/runbooks/PROJECT_MEMORY.md 最新节、docs/logs.txt，并按 docs/runbooks/skills/t384-session-closure/SKILL.md 工作。
当前640 TIFSC640/FW01.00.01.03已跑通持续mode1小端Y16→Y16BE、110槽550KiB四段队列、共享Flash A/B、0/50两点采集/保存/回读/网页应用，我已确认可用；其他验证等我安排。384原流程和包格式保留。
本次目标：<直接写此次要解决的现象或功能>。
你负责看当前源码/日志并把用户流程改完整，优先工程可用和合理内存，保留完整帧/CRC/数据域边界；不要擅自重采标定、提速、扩大到OEM或增加不必要前置验证。编译、产物检查、烧录和上板全部我来，不要再让我等你确认map。已授权方案直接做；涉及新的模式/持久化/自动动作变化先说明具体行为再确认。
给出改动、实际做过的检查和明确可执行的一行操作。不要覆盖已有改动，不commit/push。只有我要求交接/沉淀时，更新HANDOFF顶部、memory和现有skill。
```

### Skill 落点

已更新 `docs/runbooks/skills/t384-session-closure/SKILL.md`，含本轮完整标定闭环和链接/协议/资源教训；按路径显式读取，不声称已注册自动发现。`.agents/`当前受环境只读约束，未绕过权限写入。

---

## 2026-09-24 本轮记忆：640 手机无帧 → 完整 packed ring → 手机约20 FPS

### 当前结果

- 手机无帧的根因不是双客户端、NCM 枚举或网页 watchdog，而是 640 V5F packed ring 只有 64 槽/165,888 B，小于一个完整 packed 帧 331,776 B。手机背压时 producer 在帧中途填满 ring，产生大量半帧 abort。
- 提交 `9a7e07b` 将 640 ring 扩为 128 槽：V5F DTCM 保存前 64 槽，共享 SRAM `0x20144800` 保存后 64 槽；共享 metadata 位于 `0x20143FE0`，大小 2,080 B。V3F heap 边界前移到 `0x20143FE0`，保留共享区。
- 用户已重新 Build/Merge/烧录并验证手机约 20 FPS。最新 `docs/logs.txt`：`source.fps_x1000=60039`、`pipeline.slot_count=128`、`pipeline.storage_capacity_bytes=331776`、`frames_completed=591`、`frames_aborted=0`、`protocol_errors=0`、`ncm.tx_drop=0`、`stream.frames=114`、`stream.write_errors=0`。
- `source.dropped_frames=311`、`pipeline.acquire_no_slot=312` 表示慢客户端下整帧边界跳过；这是当前保护策略，不等于半帧发布。关键正确性指标已从 abort 风暴恢复为 `frames_aborted=0`。

### 关键实现边界

- 保留 `T384-FRAME-CHUNK-V2`、36B envelope、TCP COPY、checksum、FRAME_START/END 和浏览器 parser；没有恢复 adaptive2 或 HTTP 丢旧 chunk。
- 共享 ring 使用 V3F 共享 SRAM；V5F/V3F linker 必须保持相同的 `FRAME_SHARED` 地址和大小，V3F heap 不能越过 `0x20143FE0`。
- 640 packed ring 的物理布局：V5F local `0x200C0300/0x28800`，shared metadata `0x20143FE0/0x820`，shared payload `0x20144800/0x28800`。后续不得只改 slot 宏而不更新两个 linker、地址映射和产物门禁。

### 验证边界与后续

- 用户完成了目标构建、合并、烧录和手机现场验证；代理没有运行 MRS/目标构建或烧录。
- 当前证明的是手机可持续出完整帧和无 mid-frame abort；还没有 60 秒/10 分钟手机严格窗口、Windows 回归、多平台、24 小时和正式测温证据。
- 下一轮只做持续性验证和 Windows 回归，先确认 `slot_count=128`、`storage_capacity_bytes=331776`、`frames_aborted=0`、NCM drop/error=0；不要重新引入网页 watchdog 或 latest-only 丢 chunk 变量。

## 2026-09-18 本轮记忆：384 保存收尾 → 640 出图约 10 FPS → 暂停

本节是最新合作约定。下方“等模组到货”“仅 V3F”“固定 18.1”等是历史阶段，冲突时以当前源码、用户现场反馈及 HANDOFF 顶部为准。

### 用户偏好（本轮反复纠正）

- 中文、简短、直接：用户说“看日志/日志/重复”时立刻重读 `docs/logs.txt`；“忘保存，重复”意味着重新读文件，不能继续引用旧快照。
- “快”不等于乱改。先抓直接证据，每轮只改一个吞吐变量；额度有限时缩小读取/输出和验证范围，不降低数据正确性或隐藏风险。
- 用户明确“编译测试我来做”：本轮与后续恢复默认由用户执行 MRS、目标编译、测试与烧录，代理只做源码/日志分析、必要最小修复和文本差异检查；扩大执行边界需新的明确授权。
- 顺序由用户控制：先解决 384 标定收尾，再单独跑通 640，最后网页设置选择设备。用户说“先这样吧”立即停止优化，别自动继续历史待办。
- 模组到货前不重复要求 PN/接板；不知型号时先准备安全入口，实际接上后由只读身份查询核对，不能凭“640 分辨率”推断协议支持。
- 不反复问已授权的小修复，不覆盖既有未提交修改，不自动 commit/push。用户要求本轮三分钟内交接，顶部必须 30 秒能恢复。
- 只在明确要求记录/交接时更新文档；本轮已明确授权 memory、HANDOFF、skill。

### 从错误里学到的最佳实践

1. **先核对真实地址/架构**：当前 `t384_product_config.h` 是 192.168.17.1，V5F 采集/V3F 网络。此前照搬旧 skill 的 18.1/单核信息是错误；给网址和构建入口前先查当前源码。
2. **分层看“无数据”**：USB 可连不代表视频完整，DVP 帧计数增长也不代表网页交付。帧长度、行数、完整帧、abort、queue、HTTP/NCM 错误要一起核对。源码身份、目标产物、用户烧录反馈、运行字段分别记录，不能假定同一版本。
3. **完整帧必须有容量/策略保证**：640 一帧 655360 B，40×5120 仅 204800 B。持续字节增长可以全是半帧；实际早期完整帧 0，不能继续只调 UART 或浏览器。
4. **USB 回归先隔离最近改动**：v5 引入 ITCM 像素和无条件展开后出现 Windows 无法识别；撤回该轮后连接恢复。相关性不等于确定根因，不能继续叠加 USB 描述符/PID/IP 修改。v6 采用 SRAM 像素、ITCM 元数据、对齐字访问、活动读租约才展开，用户确认出图。
5. **机芯配置按精确 PN/FW 和 SDK 核对**：实际 TIFSC640/FW01.00.01.03、Picture0/YUYV2；保留 6 个只读查询即可，不把 384 的 TPD/关闭数字输出/设帧率命令硬套。确认合法响应、CRC、长度/布局后才归一化。
6. **诊断计数不能滥用**：published 含无流时释放；60 FPS 是 source；停流 `stream.active=0` 时瞬时 FPS/BPS 清零。要测瓶颈，取活动期间带间隔的完整帧/字节差值，区分 sendbuf 耗尽与 tcp_write ERR_MEM。
7. **窗口翻倍只是候选实验**：640 TCP_SND_BUF 23360→46720，用户反馈仍 10 FPS，不能写成解决/提速成功，也不能据旧日志宣称唯一瓶颈。没有新增字段的活动快照时版本和原因仍需核对。
8. **Flash 按物理地址/擦除粒度判断**：0x50000/0x52000 逻辑槽应比较两个槽在真实页粒度下的页号；不能误将目标与 slot1 比较，导致写 slot1 总被拒绝。擦除/写字采用 FLASH_BASE+逻辑偏移，CRC/读回/最后有效标记等保护保留。
9. **模型生命周期与绑定要显式**：HTTP 模型不持有临时栈/过期 manifest 指针；校验 profile/负温与有符号字段/尺寸，截距按实际定点尺度计算。用户确认保存可用不等于重启/断电/OEM 精度全通过。
10. **修改工具失败先看自己的补丁**：同文件 apply_patch 多段 hunk 按源码顺序；逆序定位失败不能误说用户改了文件。历史非 UTF-8 源码修改必须保留编码，不顺手全文件转换。

### 项目关键约束与坑

- 正式工作路径 `/home/yserein/Sipeed/T384`，工程 `firmware/T384-RAW16-BENCH.wvsln`；双核、相对 linked folders，默认 profile640，IPC v6。旧测试快照和厂商资料不能当正式开发入口。
- 当前640是 native Picture 的 YUYV，8-bit 亮度加色度；网页 UYVY 是成像数据，不能套 384 的 Y16→KT/BT/NUC-T 测温。实验两点模型永远不冒充 OEM 链。
- v6 每5120B块验证 U/V 恒定，保存 32B 前缀+2560B亮度，物理2592B；128槽物理331776B/逻辑655360B。色度变化拒绝并计数，不丢颜色后假称无损。
- 像素分配 85+7+16+20 槽到既有 SRAM；FRAME220320/ITCM2048/DTCM18144/CODE41472/DATA56960。DATA 末尾5120B HTTP 展开 scratch 归唯一消费者租约所有；tcp_write COPY 完成后才能释放，不改成无 ACK 生命周期保障的 zero-copy。
- ISR 不阻塞、不动态分配、不长日志，跨核对齐/屏障/缓存和内存分区必须守住；扩大缓存不能侵占代码/栈/堆或靠旧 map 放行。
- 10 FPS 完整 UYVY 约6.5536 MB/s；25 FPS 需16.384 MB/s、60 FPS需39.3216 MB/s，协议开销另算。仍不能证明目前10FPS来自 USB 物理上限。
- 保留 TCP/IP 校验、既有36B envelope和像素精度，不偷偷改 USB身份、描述符、IP、原始流格式、电平或 GPIO 时序。换 Y8/压缩传输可能提速，但属于协议方案，先明确授权与兼容影响。
- 384 保存逻辑槽0x50000/0x52000；禁止 Erase All/Clear CodeFlash，Merge空洞也不能证明原标定保留。持久化/断电和正式温度精度仍待验证。
- 网页设备选择尚未实现；现在仅编译期 profile。640 当前仅图像，Android/iPhone/其他 PC、多平台及24小时不是已完成项。

### 本轮结果与验证边界

用户确认384保存问题可用，640 v6出图约10FPS，发送缓存增大后仍10FPS并要求暂停。最新保存日志是停流v6快照，不包含新增背压分类字段。代理仅运行源码/日志读取与 git diff --check，未运行编译/专项测试或烧录；准备的 tests/mini2_640_sram_frame_smoke.c 和初始化夹具不能列成已通过。此次文档未提交/发布。

### 下次一次性提示词

```text
接手 /home/yserein/Sipeed/T384。先读 AGENTS.md、HANDOFF.md 顶部30秒恢复、docs/runbooks/PROJECT_MEMORY.md 最新节、docs/logs.txt，并读取遵循 docs/runbooks/skills/t384-session-closure/SKILL.md；相关任务使用现有 RAW16/标定/NCM skill，但先核对其历史信息。
当前640 TIFSC640/FW01.00.01.03，双核V5F采集/V3F网络，640×512原生60FPS Picture/YUYV，v6 SRAM无损打包已出图约10完整FPS；TCP发送缓存扩大到46720后用户反馈仍10FPS，尚无包含新增计数的活动日志。实际IP192.168.17.1。384标定保存用户确认可用，但正式OEM测温未完成；网页设备选择尚未实现。
本轮目标：先根据我这次指定的方向推进，别自动续跑旧待办。编译、测试和烧录由我做；你先读最新日志与源码，给结论和依据，再做必要最小可回滚修改。每轮一个变量，不猜寄存器/机芯命令，不改变USB身份/IP/流协议，不覆盖已有改动；区分source和完整帧、候选改动和实测结果。中文简短，额度有限但不能乱。遇USB回归先隔离最近改动，分析吞吐先取得活动流证据。
结束或我说暂停时，在三分钟内更新HANDOFF顶部唯一当前状态、结果、3—5条下一步、路径、验证和未决问题；本次也同步新经验到memory和skill，不commit/push。
```

### Skill 存放

`docs/runbooks/skills/t384-session-closure/SKILL.md` 保存本轮接手/分层排障/停止与交接工作法；由于 `.agents/` 当前只读，未修改或安装到该目录。下次在提示词中按路径读取，不假定已加入可自动发现的 skill 列表。

---

## 2026-09-15 session暂停：等WN2384T到货

### 最终状态

- 用户确认未来模组WN2384T，等到货再继续，目标-20～150°C；环境温区/允许误差未确认。当前WN2256已取NUC-T两档，KT/BT仍拒绝，distance后缀缺失。实机八事务退出无锁定；WN2384T未上板验证。
- 原厂主机两点重标定输入含KT/BT、NUC-T、Vtemp及环境参数；内置两点为另一机制，只明确列WN2384T、SE51280T、TC2-C。支持接口不等于原表可导出或全温区精度已保证。

### 用户偏好：本轮反复纠正

- 聚焦拿到所需原厂数据、改善测温稳定性与准确性；已知成像可用时，不用持续流测试替代KT/BT获取或精度研究。本轮10分钟测试用户不需要，不再安排。
- 一句话先给状态和直接回答，再给证据；不反复要求执行同一路径、阅读同一失败。用户愿意换到官方支持模组，也愿意研究自主标定，但不能迎合“全范围满分”。
- 数据发现统一到docs/data/read_report.txt并保留原报告。暂停/结束HANDOFF要30秒能恢复，本轮要求三分钟内沉淀；恢复先读暂停状态，不能机械继续历史待办。
- 用户负责Windows/MRS/烧录/黑体现场；代理自主做授权的读取、最小修复、测试、资料核对和沉淀。已授权缓存方案不重复问确认；不自动写模组/commit/push。

### 错误中学到的最佳实践

- 区分本地拒绝、模组明确拒绝和不确定响应。根因1：CRC正确空正文C7拒绝后误追加CLOSE导致cleanup_failed。明确拒绝直接释放，损坏/超时/异常正文仍严格清理；场景14验证错误后下一事务完整成功。
- 根因2：make_path缺后缀在OPEN阶段误判部分发送，锁定需要may_be_open成立；场景10验证本地distance拒绝后nuct-low完整成功。测试不能只验证首事务报错，必须验证第二事务能否继续。
- HTTP409 start_rejected表明未进入模组，不是文件读取失败；C7 status=1或SDK-902不能证明物理无表。清理回执不能覆盖首错误。无可信后缀禁止F1回退。
- 支持名单按精确PN/FW，不凭分辨率或T384/T640项目名。WN2256未列不代表所有256不支持，WN2384T列出不代表所有384支持；P2L仅单点。
- 三种能力分开判断：原表只读导出、主机算法、模组内置重标定。内置接口是校准状态变更，需要独立授权，不能继承只读权限。
- 两点只能确定有限参数，不能同时辨识非线性/温漂/全画面/增益变化。可评估多条件基础模型+每台两点修正，是否足够由独立点验证；不直接调公式掩盖50°C约48°C误差。

### 项目关键约束与坑

- 复用RAW16 ring，不新增32KiB表RAM；DVP暂停到下载ACK/取消/超时，ISR禁止阻塞/大表运算。暂停状态解除不等于连续帧恢复已验收。
- WN2256实验公式、表和尺寸不能直接迁移到WN2384T。NUC-T两档相同不代表KT=1/BT=0；Y16>>2仅格式转换，Vtemp不是目标温度，gain/FFC/Vtemp/epoch需同帧绑定。
- 正式工作路径/home/yserein/Sipeed/T384，MRS相对linked folders，只构建/下载V3F，禁Erase All/Clear CodeFlash。docs/data厂商资料默认只读，用户指定read_report例外。
- 主机smoke不等于WCH编译/烧录：最新完整检查因内嵌页面更新后map过期退出1。新Windows验收分支未验证；WN2384T还未到货，不声称产品完成。
- 精度需明确目标/环境温区、最大误差、光学条件、发射率/距离/ROI与黑体不确定度；不以端点吻合、平均误差、稳定读数或不断流代替全范围验收。

### 下次一次性提示词

> 使用 $t384-radiometry-calibration-closure 接手T384；先读HANDOFF.md顶部、docs/runbooks/PROJECT_MEMORY.md、docs/data/read_report.txt和docs/logs.txt。当前暂停等WN2384T，目标-20～150°C，先确认到货及PN/SN/FW。聚焦原厂KT/BT/NUC-T/Vtemp和两点测温精度，区分只读导出、主机算法、模组内置重标定，不重复WN2256拒绝路径、不安排已知成像可用的吞吐测试。先一句话给状态和证据，再做授权的最小可验证工作；不猜表/后缀，不自动写模组/烧录/commit/push。数据汇总read_report.txt；暂停或结束整理唯一当前HANDOFF，将新偏好/根因沉淀memory与现有skill。

## 2026-09-15 session 收束：WN2256 实验测温误差与可复用工作法

### 本轮最终状态

- 用户已在 Windows 板上验证当前 256u 实验模型：50°C 黑体页面约显示 48°C，手掌约二十多°C。该结果说明二点模型能跟随温度趋势，但不能证明绝对精度、全量程或 OEM 算法正确。
- 当前实验模型仍为 `experimental-blackbody-2point-v1`：`T=(Y16-38659.97)/118.28`，数据来自 WN2256 高增益、0°C/50°C、1 cm、发射率 0.98、各30帧完整采集。
- 原厂已读数据仍只有 PN、Ktemp/Btemp/Address_CA 和 16384 项 NUC-T；KT/BT/距离表读取为 SDK `-902`，SNR/NUC 数据域、gain 语义、Vtemp→索引和正式运行链仍未闭环。因此本轮结论是“实验采集和趋势验证完成，正式辐射测温未完成”，不是“只差黑体标定”。
- 本轮未向 MINI2 写入二次标定区，未自动构建、烧录或覆盖用户数据；下一次应先做误差归因，再决定是否改变温度显示行为。

### 用户偏好（本次再次确认）

- 默认中文、先给结论和证据；不接受把理论、旧日志、静态 smoke 或一次页面读数说成产品完成。
- 当前直接使用 256/WN2256 验证，不要反复要求尚未具备的 T384/T640 模组。
- 用户负责 Windows、MRS、烧录、黑体和现场操作；代理负责代码、工具、静态检查、VMware 远程读取和数据分析。交接时必须把两方待办分开写。
- 每轮只改变一个变量；先读最新日志/manifest，再归因，再修改。遇到同一个失败不要重复猜 PID、USB 层或路径。
- 不为了“架构完整”浪费 RAM；不自动 commit、push、烧录、写表或覆盖用户已有改动。
- 长任务暂停/结束必须更新 `HANDOFF.md`，要求 3 天后打开文件 30 秒能恢复；用户明确要求沉淀时再更新长期 memory。

### 从错误里学到的最佳实践

- Windows PnP、usbipd、WSL `/dev/video*`、VMware USB 和原厂 SDK 是不同设备命名空间；每层分别验证，不能把 Camera/Shared/Attached 当成 SDK 控制通道已可用。
- 原厂 USB 控制通道与 UVC 视频通道必须分离。普通 `/dev/video0` 不是定制控制节点；Linux 等时视频失败时，采集脚本必须以完整帧字节数为硬门禁，0 字节不能生成成功 manifest。
- 预编译 SDK 的 Windows 依赖必须闭环（`/MD`、`advapi32.lib`、SDK DLL、`pthreadVC2.dll`）；LNK 警告与运行时 DLL 缺失是两个问题。停止继续猜 PID，记录真实错误码和层级。
- `Y16 >> 2` 只是格式转换，不等于 SNR/NUC；`module_temp` 是模组温度，不是目标温度；Vtemp、gain、FFC、epoch 必须独立采集并绑定到同一帧/标定记录。
- 二点黑体拟合只能标记 `experimental`。正式链必须有数据域声明（Y16/Y14/SNR/NUC14）、PN/SN/FW、gain、FFC、epoch、表长度和 SHA-256；缺任一关键身份时 fail-closed。
- `pico_tn160` 可借鉴事务、双槽、generation、CRC、calibration_id、产测采集与离线拟合分离，但不能复制其 ADC/NTC/TN160 公式到 MINI2 Y16。

### 关键约束和坑

- 当前 WN2256 是 256×192/50 FPS 验证件，不是 T384/T640 正式 SKU；`384u` 仍未上板。
- KT/BT/NUC-T/距离表不可按文件名或 Android 候选表猜测；必须按产品、固件、增益、项数、版本和 SHA 绑定。当前高低增益 NUC-T 内容相同这一事实也不等于 KT/BT 已确认。
- 当前高增益黑体采集 manifest 中 `humidity/ta_c/tu_c` 为空，`dvp.timing_validated=0`；这些缺口会限制正式精度结论。
- 页面约 48°C 的偏差可能来自黑体设定/表面实际温度、ROI/FFC 热稳定、发射率/距离、Y16 数据域或二点模型本身，下一轮必须单变量验证，不可直接调常数掩盖误差。
- 设备端应保持原始帧和状态传输，正式 KT/BT/NUC-T 计算可放受控的浏览器/主机适配层；不要在 DVP ISR 中逐像素做大表计算或把大表塞进 V3F RAM。

### 下次一次性提示词

```text
请接手 /home/slam/Sipeed/T384。先读 AGENTS.md、HANDOFF.md 顶部30秒恢复、docs/runbooks/PROJECT_MEMORY.md 最新章节、README.md、docs/logs.txt 和 /home/slam/Sipeed/C_context/KNOWN_FAILURES.md，并使用 $t384-radiometry-calibration-closure。

当前只做 256/WN2256 验证：高增益 0°C/50°C、1 cm、ε=0.98 各30帧已采集；实验模型 experimental-blackbody-2point-v1 为 T=(Y16-38659.97)/118.28；用户看到黑体约50→48°C、手掌二十多°C。原厂 PN/Ktemp/Btemp/Address_CA/16384项NUC-T已读，KT/BT/距离表返回-902，SNR/NUC数据域和gain语义未确认。

本轮先做单变量误差分析，不立即改显示逻辑、不写MINI2、不猜PID或表路径。用户负责Windows/MRS/烧录/黑体，代理负责工具、VMware读取和验证。结束时更新 HANDOFF.md 与 PROJECT_MEMORY.md，明确实验模型、正式链缺口、用户待办和代理待办；不自动commit/push。
```

### Skill 沉淀

- 新增 `.agents/skills/t384-radiometry-calibration-closure/SKILL.md`，固化“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”、Windows/WSL/VMware 分层、原厂表 fail-closed、黑体采集门禁、用户/代理分工和 30 秒 HANDOFF 结构。
- 测试提示词保存在该 Skill 的 `evals/evals.json`；本轮完成草案和静态校验，未启动 with-skill/baseline 基准评测。需要评测时再按 skill-creator 流程运行，不把未评测 Skill 说成已验证。

## 2026-09-14 当前 session：WN2256 原厂数据、Windows 黑体实验模型与 VMware 迁移

### 当前结论

- 当前硬件仍是 WN2256 256×192/50 FPS 验证件，不是 T384/T640 成品；用户要求当前阶段直接用256验证，不要反复要求先拿384/640。
- VMware 原厂 USB 通道探针已成功读取 PN、Ktemp/Btemp/Address_CA 和高低增益 NUC-T；KT/BT、距离表及二次表读取返回原厂 SDK `-902`。原厂目标点温度接口可读，但必须和黑体参考、Y16、Vtemp同步验证，不能把 module_temp 当目标温度。
- Windows T384 NCM 已完成高增益0°C/50°C各30帧完整Y16BE采集（1cm、发射率0.98）。稳定重采0°C ROI均值38659.967/std4.91，50°C均值44574.201/std2.58；页面实验模型改为 `experimental-blackbody-2point-v1`：`T=(Y16-38659.97)/118.28`。用户实测约50°C显示48°C、手掌20多°C，说明模型仅初步可用且仍有误差。
- 实验模型只写入256u网页/HTTP头，不写MINI2、不启用正式KT/BT/NUC-T链；384u保持`unavailable`。正式辐射测温仍需KT/BT、距离修正、SNR/NUC数据域和gain语义确认。

### 用户偏好（反复确认）

- 默认中文，结论直接；先给真实证据和当前状态，不用“理论上可以”替代验证。
- 当前先做256/WN2256；不要反复询问尚未到货的T384/T640模组。
- 用户负责Windows/MRS构建、烧录、黑体和真实硬件操作；代理负责代码、工具、静态检查、日志读取、远程VMware操作和分析。
- Windows原生工具应只走Windows SDK/VID-PID；WSL/usbipd是另一条路径，不能混在同一脚本里。VMware访问优先从WSL通过本地端口转发SSH，避免让用户手工迁移文件。
- 用户不接受无效重复尝试；每轮只改一个变量，先读取日志和证据再归因。
- 用户要求长任务结束时更新HANDOFF，三天后30秒可恢复；普通小改不自动提交或push。

### 从错误里学到的最佳实践

- `usbipd Shared/Attached`、Windows PnP Camera、WSL sysfs和`/dev/video*`是不同层；必须分别验证，不能把一个层的可见性当成另一个层可用。
- Windows AC020 SDK探针曾缺少`pthreadVC2.dll`、`advapi32.lib`和动态CRT设置；预编译SDK必须按PE依赖闭环复制DLL并使用`/MD`。
- 原厂SDK Linux V4L2控制必须使用定制`/dev/cmdX`，普通`/dev/videoX`只是视频节点；VMware/usbipd能传控制但未能稳定传UVC等时视频，产生0字节帧。采集脚本必须强制校验完整帧字节数。
- Windows OpenSSH/VMware访问应区分网络可达、端口转发目标和公钥认证；先比对SSH host key指纹，再迁移文件。当前稳定入口是`127.0.0.1:22022`。
- `pico_tn160`可借鉴事务、双槽、generation、CRC、calibration_id、fail-closed和产测脚本结构，但不能复制其ADC/NTC/TN160算法到MINI2 Y16。
- 黑体两点拟合只能是实验模型；0°C明显漂移时不能直接拟合。即使模型写入页面，也必须改名为experimental并保留回退路径。

### 项目关键约束和坑

- 产品链为DVP→bounded pipeline→RAW16 chunk→lwIP/TCP→TinyUSB NCM→Web；不能在HTTP造数、绕过所有权或把模拟source称为真实MINI2。
- `Y16 >> 2`只是格式转换，不等于SNR/NUC；原厂`temp_measure_with_NUC_value`要求输入是SNR后的NUC14。
- KT/BT长度可能1021或3601，NUC-T可能8192或16384；必须按PN/SN/FW/gain/version/sha绑定，不能按文件名猜。
- 当前map栈前余量约32KiB，任何把大表放入V3F RAM的方案都需重新核对map；正式设计倾向外置存储/浏览器计算。
- 当前实验模型高增益假设但WN2256 `basic_gain_get`返回`-205`；低增益未验证，不得宣称双增益支持。
- 原厂目标点温度接口支持列表与WN2256型号边界需实测；`module_temp`仅模块自身温度。

### 下次一次性提示词

```text
请接手 /home/slam/Sipeed/T384，先读 AGENTS.md、HANDOFF.md顶部30秒恢复、docs/runbooks/PROJECT_MEMORY.md最新章节、README.md、docs/logs.txt、/home/slam/Sipeed/C_context/KNOWN_FAILURES.md，并使用$t384-raw16-pipeline-closure。

当前只做256/WN2256验证，不要反复要求T384/T640模组。最新状态：Windows NCM高增益0°C/50°C各30帧完整Y16BE，1cm、发射率0.98；实验模型`experimental-blackbody-2point-v1`，公式`T=(Y16-38659.97)/118.28`，用户实测50°C约48°C、手掌20多°C，仍是实验模型。

VMware原厂探针已读到PN、Ktemp/Btemp/Address_CA和16384项NUC-T；KT/BT/距离表读取返回-902。不要把module_temp当目标温度，不要把Y16直接当NUC14，不要猜SNR/NUC或gain。

本轮先读最新日志和黑体manifest，做单变量误差分析；代码/工具改动前说明当前行为、拟改行为、边界、回退和最小验证。用户负责Windows/MRS/烧录/黑体，代理负责工具、静态检查、VMware远程和分析。结束时更新HANDOFF与PROJECT_MEMORY，不自动commit/push。
```

### Skill沉淀

- 继续维护`.agents/skills/t384-raw16-pipeline-closure/SKILL.md`，不新建重复Skill。
- 本次应保留的经验：Windows/WSL/VMware三层设备namespace区分；原厂USB控制与UVC视频分离；`/dev/cmdX`不是`/dev/videoX`；预编译SDK依赖闭环；黑体采集强制完整帧门禁；实验二点模型与正式OEM辐射链严格分离；用pico_tn160事务/manifest/fail-closed结构但不复制传感器公式。

## 2026-09-14 当前 session：正式测温架构边界、用户偏好与下次提示词

### 用户偏好（本次合作反复确认）

- 默认中文、结论直接；先给真实状态，不用“理论上应该可以”替代证据。
- 先读 `AGENTS.md`、`HANDOFF.md`、共享失败库和项目 Skill；没有 T384 公共 preflight 时明确记录限制，不冒用其他项目。
- 用户希望先修当前实际阻塞，再进入下一层；不要把实验温度、静态检查、旧 map 或页面瞬时结果写成正式产品完成。
- 用户接受先做架构和测试，但用户可感知的温度语义、状态机、协议或数据接受条件改变前，必须说明当前行为、拟改行为、边界、回退和最小验证。
- 用户负责 MRS 构建/烧录和真实硬件动作；代理负责源码、主机测试、状态分析、工具和明确操作脚本。
- 不要每轮机械要求 Clean；普通源码增量构建，只有 map/工程配置/芯片项/缓存确实需要时才 Clean。
- 不要为了省事把 256/384 工程、验证快照、原始资料或设计文件合并；`firmware/` 是正式主线，`tests/` 是独立可迁移快照，根目录 `designs/` 禁止创建。
- 用户明确希望 3 天后打开 `HANDOFF.md` 可以 30 秒恢复；交接要写已完成、未完成、证据层级、下一动作和风险。

### 本次关键判断

- 项目设计文档明确：KT/BT/NUC-T/距离表放外置存储，DVP raw16 传输，浏览器完成校正、温度和 cmap；因此正式架构不是把所有温度计算强行塞进 DVP ISR。
- SDK 的真实 `y16_to_y14()` 行为已在本地 x86_64 `libirparse.so` 验证为 `Y14 = Y16 >> 2`，例如 `42072 → 10518`；Y16 不能直接送入 KT/BT。
- SDK 正式计算输入是 `NUC14 0..16383`，然后执行 KT/BT Q14 和 NUC-T 查表；KT/BT 存在 1021/3601 项版本，NUC-T 存在 8192/16384 项版本。
- 当前 WN2256 TPD/Y16 是中间响应量，项目资料明确其不是已证明的探测器 ADC raw，也不能自动宣称已完成绝对辐射测温。
- 本地 Android SDK 确实有 KT/BT/NUC-T 候选二进制，但发现 KT/BT 为 1201 项、NUC-T 为 8192 项，不能未经型号/固件/来源绑定直接当作目标模组正式表。
- DVP 是否已经经过厂商 SNR/NUC 不能凭代码猜；SDK 的 `y14_image_spatial_noise_reduction()` 是黑盒。若 DVP 已是厂商 TPD 中间量，重复 SNR/NUC 会算错；若不是，则需要厂商算法/参数或可验证等价实现。
- `module_temp` 与 `Vtemp` 不能混用。已加入独立 Vtemp 查询基础命令，但 Vtemp→KT/BT 索引、gain 和 FFC 每帧绑定仍未完成。

### 已完成实现与验证

- 修复 `/diag` 正文超长造成的假 404：保留 source/pipeline/ROI/MINI2/NCM/stream 核心字段，压缩重复别名，`diag_response=6080`、头部预留 `128`。
- 新增 `firmware/Common/Raw16/t384_radiometry.{h,c}`：profile、产品/版本/增益/表长度校验、Y16→Y14、KT/BT Q14、NUC-T 8192/16384 边界、标定 epoch、FFC/gain/state fail-closed。
- 新增 `tests/radiometry_smoke.c`，覆盖 `42072→10518`、BT 正负、clamp、表长度、状态不就绪、增益不匹配、索引越界。
- 新增 `tools/validate_radiometry_tables.py`：KT/BT/NUC-T 文件长度、项数、SHA-256、范围报告；支持厂商变体的显式 `--kt-items/--nuc-items`，不会静默把候选表判成目标表。
- 加入独立 Vtemp 查询命令和 `/diag mini2.query_vtemp_*` 字段；MINI2 命令向量、radiometry smoke、RAW16 全套 host smoke 均通过。
- 当前完整 `bash tools/check_raw16_bench.sh` 在源码改动后会按设计停在旧 map 门禁；这表示必须 MRS 刷新 map，不是 host smoke 失败。

### 从错误里学到的最佳实践

- `/diag` 路由存在不等于能返回 200；`build_diag_response()` 返回 0 后会落到统一 404。诊断正文扩展必须同时做容量下界/展开长度检查，且不能只靠路由测试。
- 看到温度显示稳定，只能证明当前 Y16 与经验公式闭环，不证明端序、算法阶段、绝对温度或正式标定正确。
- 不要把 `Y16` 直接当 `NUC14`；先明确数据域：Y16 容器、Y14、SNR/NUC 输出和 NUC-T 输入是不同层。
- 不要拿 Android SDK 候选表或文件名推断目标模组；必须记录产品、固件、增益、表版本、项数和 SHA-256。
- 不要用简单均值/中值滤波冒充厂家 SNR；没有算法/参数证据时保持 fail-closed，并把 SNR 放在 source/calibration adapter 边界。
- `module_temp`、`Vtemp`、gain、FFC 是不同状态，必须分别命名、分别校验，最后通过同一 frame/calibration epoch 绑定。
- 正式表计算可放浏览器（设计文档原意），CH32 保持原始帧与状态传输；不要为了“看起来完整”在 ISR 中做逐像素 KT/BT/NUC-T。
- 用户实测、`/diag` 活动窗口、主机 GCC/smoke、MRS map/HEX、烧录身份、真机持续验收必须分层报告。

### 项目关键约束和坑

- 当前硬件是 WN2256 `256×192/50 FPS`，正式目标是 T384/T640；不能把 256 验证件写成正式 SKU 完成。
- 正式工程是 `firmware/T384-RAW16-BENCH.wvsln`，`CH32H417WEU/V3F`，`Erase All=false`、`Clear CodeFlash=false`；普通源码改动用 Incremental Build。
- `T384_RAW16_PROFILE=256u` 是 WN2256；`384u` 是 WN2384 目标。384 档容量、map 和真实 DVP 需重新验证，不能只改宏。
- 当前产品链是 DVP→有界 pipeline→RAW16 chunk→lwIP/TCP→NCM→浏览器完整帧；不能在 HTTP 发送端造像素或绕过所有权。
- 当前经验温度头是 `X-T384-Temperature-Model: empirical-linear-v1`；正式 KT/BT/NUC-T 接入前不得改成 radiometric。
- 现有 Skill `.agents/skills/t384-raw16-pipeline-closure/SKILL.md` 已覆盖闭环、活动窗口、背压、单变量、MRS/map/烧录边界；不新建重复 Skill。

### 下次一次性提示词

```text
请接手 /home/slam/Sipeed/T384，使用项目 Skill `$t384-raw16-pipeline-closure`。先读 AGENTS.md、HANDOFF.md 顶部30秒恢复、docs/runbooks/PROJECT_MEMORY.md 顶部、README.md、/home/slam/Sipeed/C_context/KNOWN_FAILURES.md，并记录公共 agent_preflight.py 没有 T384 项的限制。

当前产品链是 MINI2 DVP→V3F bounded pipeline→lwIP/TCP→TinyUSB NCM→Web；当前板上是 WN2256 256×192/50 FPS 验证件，不是 T384/T640 完成。先检查 git status，保护所有未提交改动。正式工程是 firmware/T384-RAW16-BENCH.wvsln，CH32H417WEU/V3F，Erase All/Clear CodeFlash 关闭；普通源码用 Incremental Build，不能把旧 map 当新源码证据。

当前网页温度仍是 empirical-linear-v1。项目设计文档规定 KT/BT/NUC-T/距离表外置、raw16 传浏览器；正式链必须是 Y16→Y14（SDK实测为 Y16>>2）→确认是否已厂商 SNR/NUC→KT/BT Q14→NUC-T→环境修正。不要把 module_temp 当 Vtemp，不要把候选 Android 表当目标模组正式表，不要猜 SNR。

本轮唯一目标：完成并验证正式测温的状态/表 manifest/浏览器计算边界，保持无有效目标表时 fail-closed 并继续显示实验模型。需要区分：源码、主机 smoke、MRS map/HEX、烧录、/diag、真机黑体回归。每轮按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”汇报。
```

## 2026-09-10 真实成像已上板与单宏规格切换

### 用户偏好

- WN2256/256×192 是当前真实硬件调试档；确认能连续成像后，先保持现状，不要无故继续改背压或协议。
- 用户希望 256/384 通过一个全局 `#define` 切换，不能散改宽高、帧率、行字节、pipeline 和网页常量。
- 用户要求 3 天后打开 `HANDOFF.md` 能在 30 秒内接上；交接必须明确当前已烧录版本、实测证据、未验证边界和下一步。
- 用户要求把反复校正过的偏好、失败教训、项目坑和可复用提示词写入项目 memory；相关流程应沉淀到已有项目 Skill，而不是新建重复 Skill。

### 已确认状态

- 用户已烧录宏改动后的 24 槽版本，并确认真实 WN2256 画面连续成像。
- 最新 `/diag`：`source.kind=mini2-dvp-raw16-v1`，`Camera WN2256`，FW `00.00.08.03`，`256×192`，`50 FPS`，每帧 192 行/98304 B，FIFO=0，`pipeline.slot_count=24`，`capacity_bytes=98304`，`acquire_no_slot=0`，`protocol_errors=0`，`ncm.tx_drop=0`。
- 最新累计快照：`source.frames=1085`、`published=764`、`dropped=321`、`stream.frames=320`、流连接/断开各 3 次。该值跨越无流和多次连接，不能直接当活动窗口丢帧率；“连续成像”来自用户实测确认。
- `firmware/Common/Raw16/t384_raw16.h` 的 `T384_RAW16_PROFILE`：`256u`=WN2256（256×192/50 FPS），`384u`=WN2384 目标（384×288/30 FPS）；网页已按 HTTP 头动态适配两种尺寸。

### 最佳实践与坑

- 看到画面后仍要区分可见成像、活动连续流、严格无丢帧和长稳；下一次应在复位后只开一个流，用活动前后 `/diag` 差分，而不是用累计 dropped 下结论。
- 用户确认宏版本已烧录时，应以 `/diag pipeline.slot_count=24` 和画面为运行证据，不要重复误报“宏版本尚未烧录”。源码时间/map/HEX 版本链仍只用于后续新改动。
- 256 档 24×4096 B 正好覆盖一帧；384 档当前 12×6144 B 小于 221184 B 单帧。切到 384u 必须重新评估队列容量、map 的 `_ebss→固定栈` 余量和真实 WN2384 DVP，编译通过不等于产品完成。
- DVP FIFO/行帧计数为 0 且能成像，证明当前采样设置在板上可用，但尚未替代逻辑分析仪对采样沿、同步极性、字节序和消隐/信息行的正式冻结。
- 用户侧“连续”反馈是重要实测，但若要宣称稳定，仍需明确测试窗口和指标；不应在无时间窗口时自动扩大结论。

### 下次一次性提示词

```text
请接手 /home/slam/Sipeed/T384，先读 AGENTS.md、HANDOFF.md 顶部30秒恢复、docs/runbooks/PROJECT_MEMORY.md 顶部、README.md、docs/logs.txt、/home/slam/Sipeed/C_context/KNOWN_FAILURES.md，并检查当前 Skills。使用 $t384-raw16-pipeline-closure；若公共 preflight 没有 T384 项，记录限制，不冒用其他项目。

当前板上已烧录宏改动后的 24 槽真实 WN2256 版本，用户已确认 256×192、50 FPS 画面连续成像；/diag 的 source.kind=mini2-dvp-raw16-v1、FIFO=0、pipeline.slot_count=24、capacity=98304。不要把累计 dropped 当活动流丢帧，也不要重复判断该版本未烧录。

规格切换只改 firmware/Common/Raw16/t384_raw16.h 的 T384_RAW16_PROFILE：256u 为 WN2256，384u 为 WN2384 目标；网页从 HTTP 头自适配。每轮按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”，区分用户实测、/diag活动差分、主机静态、MRS map/HEX、烧录和长稳。普通源码用增量 Build；不要自动烧录、commit 或 push。384u 切换前先重新核对队列容量、RAM余量和 WN2384 实机证据。
```

### Skill 维护

- 继续使用并维护 `.agents/skills/t384-raw16-pipeline-closure/SKILL.md`，不新建重复 Skill。
- Skill 应保留：活动窗口差分、单宏规格切换、24 槽仅适用于 256 档、旧 map/HEX 版本链、真实成像/连续流/严格稳定性分层，以及用户负责 MRS 构建和烧录。

## 2026-09-02 迁移、25.5 FPS 与协作方式沉淀

### 用户偏好（反复调整后确认）

- 用户要的是能迁移、能复现、能独立构建的合格工程，而不是只在当前机器上构建成功。MRS tracked metadata 必须使用相对路径；发现盘符绝对路径时应修工程元数据，不能把迁移责任推给用户。
- `firmware/` 只能放唯一正式开发源码；备份、阶段验证和已验证完整工程放 `tests/`。验证快照要能独立打开和构建，因此与正式源码的逐字副本是有意设计，不应为了“消除重复”抽成共享目录或软链接。
- 用户对已确认的构建路径有明确判断时，不要每轮重复要求 Clean、核对 map/HEX 或重新证明路径。普通源码改动用 Incremental Build；只有芯片项、linker、工程资源、目录迁移或缓存确实失效时才 Clean。
- 用户只允许 `docs/design/`，禁止根目录 `designs/`。不能顺从外部设计工具或 Skill 的默认输出目录，也不能用 `.gitignore` 掩盖违规目录。
- 用户重视可解释的单变量实验。必须说明“改了什么、为什么突然有效、之前为什么无效”，不能只报告 FPS 上升。有效因果链是：`-O2` 约 `22.9→23.9`，通用 checksum-on-copy 约 `23.9→23.7` 被证伪并回退，自有融合 copy+checksum 约 `23.7→27.7`。
- 用户不希望把 Clean 当仪式、把构建当代理的默认动作或把烧录混进验证。用户负责 MRS 构建/烧录；代理做源码、静态检查、map 证据和设备反馈分析，并精确区分每一层。
- Git 提交必须按用户指定范围。用户明确说 docs 不提交时，只提交代码/工程配置；提交前核对工作区、diff 和暂存区，不得把未跟踪文档顺手加入。
- 审查应先报告再修改，只应用确定不改变行为的简化；不要为“代码更漂亮”拆验证快照、抽无意义 helper 或触碰协议/缓冲所有权。

### 从错误里学到的最佳实践

- **命名迁移要全链闭环**：工程名、芯片项、linked folders、源码标识、VID/PID、检查脚本和 Skill 都要搜证据；但用户已完成的命名动作不能反复代做。旧 `.mrs/obj` 是机器缓存，应丢弃重建，不能手改生成依赖文件冒充可迁移修复。
- **QEU/WEU 不能凭文件名判断**：目标是 `CH32H417WEU6`，正式与 RAW16 验证工程必须选 MRS `CH32H417WEU`，只构建/下载 V3F，并禁止 `Erase All`、`Clear CodeFlash`；厂商 base 快照里的 QEU 元数据不能作为目标板下载工程。
- **不要机械要求 Clean**：Clean 会增加时间且抹去增量证据。只有元数据、linker、芯片或迁移后缓存变化才需要一次；普通 `.c/.py` 改动不需要。
- **不要把时间戳门禁当代码失败**：静态脚本发现 source 比 map 新时是在阻止用旧 map 证明新源码。纯注释也会触发该门禁，因此不要在编译源码里做无价值注释 churn；若补丁撤销且内容与验证快照逐字相同，应确认最终 diff 后再判断是否需要构建。
- **不要把 non-synthetic 写成真实 MINI2**：DMA 等效 source 为了不做合成全像素断言使用 non-synthetic wire flag，但 `/diag source.kind=dma-equivalent-dvp-source-v1` 才是真实身份。工具的 `real/frame-integrity-check` 标签可能误导；改用户可见语义前需明确确认，现阶段交接中必须写清。
- **优化必须保留缓冲所有权**：`TCP_WRITE_FLAG_COPY` 和 TinyUSB RX→pbuf 复制分别保护 lwIP 异步发送和回调缓冲生命周期。没有 ACK/重传/部分确认/断连回收证据时，不能为了零拷贝删除。
- **找到性能根因后要冻结有效变量**：当前融合 copy+checksum 解决了重复遍历 payload 的热路径成本，60 秒实测 `27.750 FPS`。后续稳定性修复不得顺手改回通用 checksum、扩大 NCM RAM 或重写 ring。
- **审查要尊重架构意图**：`firmware/Common/` 与 `tests/.../Common/` 的重复是验证快照；两份 MRS 检查脚本的相似代码覆盖范围不同。强行共享会降低独立迁移性或增加中间层，不是高质量简化。
- **小优化也要验证真实分支**：`tools/t384_raw16_bench.py` 的 `--expect-source real` 原本仍分配 221,184 B 期望帧并转成 `bytes`；按需生成且缓存 `memoryview` 后，必须分别覆盖 real 不需要期望帧和 synthetic 仍逐字节校验。

### 当前项目关键约束和坑

- 正式 RAW16 工程入口为 `firmware/T384-RAW16-BENCH.wvsln`；已验证 27.750 FPS 快照为 `tests/ch32h417 t384 raw16 bench/`；NCM/OV2640 保护快照为 `tests/ch32h417_t384_ncm/`。
- MRS `.wvsln/.wvproj/.project/.cproject` 只能使用相对 linked folders；`.mrs/`、`V3F/obj/` 是本机缓存。迁移后清一次旧缓存，日常增量构建。
- 当前产品数据面是 `DMA-equivalent source → 12×6144 B ring → 36 B RAW16LE-CHUNK-V1 → lwIP/TCP → TinyUSB NCM → USBHS → 浏览器完整帧`。384×288 RAW16 单帧 `221,184 B`；25.5 FPS 是 `5,640,192 B/s`。
- 当前实测只证明 DMA 等效源下游有 `27.750 FPS / 6.138 MB/s` 能力；真实 MINI2 DVP、V5F跨核、T640、测温和多平台兼容均未完成。
- 当前首要稳定性风险是 RAW16 流关闭后 HTTP 短时拒绝连接；下一步应保持吞吐路径不变，单独验证 TCP PCB/客户端释放/TIME_WAIT，随后做 10 分钟持续测试。
- 24 小时前必须修 DHCP T1/T2 `ciaddr` 续租，覆盖断连、主机休眠恢复和看门狗；否则不能声明长期稳定。
- 根目录 `designs/` 永久禁止；正式设计放 `docs/design/`。不要新建历史 CDTR 目录，也不要把本地生成缓存提交。

### 下次一次性达到当前效果的推荐提示词

```text
请使用项目 Skill `$t384-raw16-pipeline-closure` 接手 `/home/slam/Sipeed/T384`。先完整读取 AGENTS.md、HANDOFF.md 的“30秒恢复”、docs/runbooks/PROJECT_MEMORY.md 顶部最新章节、README.md 和 /home/slam/Sipeed/C_context/KNOWN_FAILURES.md；公共 agent_preflight.py 没有 T384 项就明确记录，不冒用其他项目。

当前已确认：正式源码/MRS工程只在 firmware/；阶段验证完整工程在 tests/。正式入口是 firmware/T384-RAW16-BENCH.wvsln，CH32H417WEU，只用 V3F，Erase All/Clear CodeFlash 均关闭。MRS tracked metadata 必须用相对 linked folders；.mrs 和 V3F/obj 是本机缓存。普通源码修改只做 Incremental Build，不要每轮要求 Clean、重复核对我已确认的构建路径或自动烧录。根目录 designs/ 禁止创建，设计资料只放 docs/design/。

DMA 等效 source 已在 Windows 60 秒严格测试达到 1665帧、27.750 FPS、6.138 MB/s，序号缺口/逆序/不完整帧均为0。有效优化是项目自有融合 copy+checksum；通用 checksum-on-copy 已证伪并回退。不要回退这条吞吐路径，也不要把 non-synthetic 标志称为真实 MINI2；真实来源以 /diag source.kind 判断。

本轮先按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”汇报当前状态，再只做我指定的唯一任务。修改前检查 git 状态，保护未提交改动；用户可感知逻辑、协议、USB描述符、buffer ownership和构建系统变化必须先给当前/拟改行为、边界、回退和验证并等我明确确认。每次只改一个变量，区分静态检查、MRS构建、烧录、瞬时实测和持续验收。不要把主机GCC、旧map或页面瞬时FPS写成上板完成。

若继续当前闭环：先只读复现 RAW16 流断开后 HTTP 超过25秒短时拒绝，保持融合 checksum-copy、ring、NCM和协议不变；修复后做10次开关流，要求页面和 /diag 2秒内恢复且connect/disconnect配平，再做Windows 10分钟完整帧≥25.5 FPS、≥5,640,192 B/s且全部错误计数为0。真实MINI2到板后只替换source adapter，先验证1.8V电平、PCLK/行场/采样沿、字节序、信息行和完整帧；未上板就明确写未上板验证。

用户要求commit时，先展示/核对 status、diff和暂存范围；docs未获授权不得提交，永不自动push。
```

### Skill 沉淀状态

- 不新建重复 Skill；继续维护 `.agents/skills/t384-raw16-pipeline-closure/SKILL.md`。
- 本轮新增应覆盖：`firmware/`/`tests/` 独立工程边界、相对 MRS 路径、Incremental Build 默认、QEU/WEU/V3F烧录红线、融合 checksum-copy 冻结、non-synthetic 身份歧义、代码提交范围与 30 秒交接。
- 现有 Skill 已有 eval 文件；本轮受三分钟交接时限约束，只做结构校验，不运行完整 with-skill/baseline 基准。后续若实际触发或执行偏离，再按 skill-creator 流程迭代。

## 2026-09-01 项目命名迁移

- 项目、工作区、固件工程、代码标识和项目 Skills 的当前统一名称为 `T384`。
- 当前协议头为 `X-T384-*`，本地测试 USB 身份为 `VID/PID=1A86:E384`；该 PID 仍未分配，只能用于开发，不能发布。
- 改名前 22.9 FPS 与既有枚举/DHCP/HTTP 结果仍是真实历史证据，但不自动证明重命名后的 E384 固件已构建、烧录或上板。
- 旧 MRS `obj/.mrs` 与 UI 临时输出已移出工作区；迁移后必须重新 Clean + Build，以新 map/HEX 建立版本链。
- 历史章节保留当时的旧工程名、PID 和产物路径，读取时以本节、`HANDOFF.md` 顶部和当前源码为准。

## 2026-08-31 RAW16 25 FPS 闭环合作沉淀

### 用户偏好（反复校正后确认）

- 先完成当前唯一目标再扩展：现在唯一目标是 `384×288 RAW16` 真实端到端先到 25 FPS；不要提前接测温、T640、24 小时或重做其他功能。
- 用户要的是 `DVP 原始数据 → DMA/有界缓冲 → lwIP/TCP → USB NCM → 浏览器完整帧` 的真实工程链路。硬件未到时只允许模拟 source；下游协议、带宽、内存、背压和浏览器消费必须走正式路径，不能用 HTTP 即时造数据、假 FPS 或绕过缓存所有权。
- 真实 MINI2 到板后应只替换 source adapter，不重写 queue/wire/NCM/browser。若硬件事实证明跨核或缓存模型不同，必须明确说明边界，不能假装只换函数必然成立。
- 页面必须直接显示真实画面和真实源/流 FPS；不接受必须运行外部脚本才能看到核心结果。但严格脚本仍用于 60 秒逐帧验收，页面观感不能代替协议校验。
- 用户希望代理自己读取设备、`/diag`、日志和现象，不把能自动取得的证据反复交给用户查看。
- UI 视觉参考 NanoUPS Portal 模板，功能参考 pico_tn160；两者不能混淆。记录默认关闭，只能由用户主动开启；设备端禁止保存历史，浏览器记录也必须有明确上限和手动导出边界。
- 用户不接受“理论上应该可以”或人为伪造更高指标。必须区分：源码、主机静态检查、MRS 构建、已烧录身份、目标板瞬时结果、持续验收。
- 每轮最好是单变量、可回滚、可量化；用户关心是否达到 25 FPS，而不是堆很多看似高级的优化。

### 从错误中学到的最佳实践

- 不要把“模拟测试”理解成测试专用旁路。曾经在 HTTP 发送处即时生成 RAW16、占用像素写 magic，虽然能测速，却绕过 DVP 后的缓冲所有权和背压；正确做法是只模拟采集源并进入正式流水线。
- 不要报告未烧录代码的效果。必须建立 `源码时间 → map/HEX 时间 → 用户烧录确认 → /diag 身份/行为` 版本链；旧 map 门禁不是源码失败，但禁止把新源码写成板上结果。
- 页面 source FPS 高、stream FPS 为 0 时，先看完整帧语义。队列满后 abort 半帧曾导致浏览器永远等不到 FRAME_END；正确策略是可暂停模拟源，或真实不可暂停源按整帧边界丢弃，不能发布半帧。
- `ncm.rx_busy_drop` 原名容易误导：TinyUSB callback 返回 false 后数据报仍待 renew，不一定真实丢包。修改前要读第三方栈语义；本轮 RX batch drain 将它大幅降低，但 22.8→22.9 证明它不是主要吞吐瓶颈。
- 浏览器伪彩热点必须测后判断。256 项 LUT 是正确且低风险的实现优化，但实板只增加约 0.1 FPS；不要因为代码看起来更快就把它写成根因。
- 不能把 USB 480 Mbps 链路速率当有效 payload。T384@25 需要 `5,529,600 B/s`；验收必须按完整帧 payload、序号、错误计数和持续时间。
- 背压计数必须分层解释：本轮 `pipeline` 满和 HTTP backpressure 高、NCM TX backpressure/drop 为 0，瓶颈更可能在 lwIP TCP COPY/checksum/ACK 驱动发送路径，而不是 USBHS 提交层。
- 优化顺序应由证据决定：先去掉源端 CPU 生成（19.2→22.8，确认有效），再验证 ACK/LUT（22.8→22.9，收益很小）；下一步应查 TCP copy/checksum，而不是继续调已证伪层。
- 不要一次叠加多个高风险变量。对 checksum、窗口、zero-copy、NTB 大小分别测试；否则达到或下降后无法确认因果，也难以安全回退。
- zero-copy 不能只去掉 `TCP_WRITE_FLAG_COPY`：pipeline slot 必须保持到远端 ACK，断连/重传/部分 ACK 都需正确释放，否则会被 DMA 覆写或永久泄漏。没有所有权测试不得实施。

### 项目关键约束和坑

- 当前独立 bench 单帧 `384×288×2 = 221,184 B`；25 FPS 是 `5.5296 MB/s`，7 MB/s 压力门对应约 31.648 FPS。22.9 FPS 现场 payload 为 `5.062656 MB/s`，还差 `466,944 B/s / 约9.2%`。
- 当前正式链：DMA 等效 source → 12×6144 B（8 行）ring → 36 B `RAW16LE-CHUNK-V1` envelope → lwIP raw TCP → TinyUSB NCM 16 KiB IN NTB → USBHS burst → Fetch 重组 → 浏览器伪彩。
- 当前 `TCP_SND_BUF=16×1460≈23 KiB`、pipeline 72 KiB、NCM IN 16 KiB×3、OUT 4 KiB×1；旧 map `_ebss=0x20171fe8`，到固定栈仍有约 55 KiB，但任何内存扩大必须由新 map 重新核对至少 32 KiB 余量。
- 最新 22.9 FPS 证据：source/stream 约 22.86/23.00 FPS，完整帧/发布帧一致；pipeline abort/drop/protocol error 为 0；NCM TX backpressure/drop 为 0；HTTP backpressure 和 pipeline no-slot 持续增长。
- DMA 等效源只消除 CPU 逐像素生成，不证明 MINI2 DVP 电平、PCLK/采样沿、字节序、消隐、信息行、FIFO 或 V5F/V3F 共享内存。`source.synthetic=0` 只表示不做全像素 synthetic 图案断言，不代表真实传感器。
- 用户负责 MRS 目标构建和烧录。代理可以改源码、跑主机检查和读设备，但不能把主机 GCC 当 WCH 目标构建，也不能自行烧录。
- 只修改 `firmware/ch32h417_t384_raw16_bench/`；已工作的 OV2640/NCM 主线 `firmware/ch32h417_t384_ncm/` 是保护基线。
- 24 小时声明仍被 DHCP T1/T2 续租兼容、主机休眠恢复、看门狗和多平台矩阵阻塞；先达到 25 FPS，再做 10 分钟，最后才进入 24 小时。

### 下次一次性达到当前效果的推荐提示词

```text
请使用项目 Skill `$t384-raw16-pipeline-closure` 接手 `/home/slam/Sipeed/T384`。先读 AGENTS.md、HANDOFF.md 顶部 30 秒恢复、docs/runbooks/PROJECT_MEMORY.md 顶部和 /home/slam/Sipeed/C_context/KNOWN_FAILURES.md；公共 preflight 没有 T384 项就记录限制，不冒用其他项目。

本轮唯一目标是让独立 `firmware/ch32h417_t384_raw16_bench/` 的 384×288 RAW16 从当前真实 22.9 FPS / 5.062656 MB/s 达到连续 60 秒完整帧 ≥25 FPS / ≥5.5296 MB/s。不要改已工作的 `ch32h417_t384_ncm`，不要做 UI 重设计、测温、T640 或 24 小时测试。用户负责 MRS V3F Clean/Build 和烧录；你负责源码、静态检查、版本链、直接读取 http://192.168.18.1/diag 和 Windows 侧验收。

当前已证伪：继续扩大 NCM RX ACK budget 或优化浏览器伪彩不是主要方向（22.8→22.9 FPS）。保持 DMA 等效 source、12×6144 B ring 和 RAW16LE-CHUNK-V1 不变，按 source→pipeline→TCP COPY/checksum→NCM→USB→browser 分层取证。每次只改一个变量，优先验证 LWIP_CHECKSUM_ON_COPY；若不足再评估融合 copy+checksum，zero-copy 只有在 ACK 前槽所有权、重传、部分 ACK 和断连回收测试完整时才允许。

每轮按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”汇报，严格区分源码/静态/MRS/烧录/上板。通过条件为 60 秒完整帧 ≥25 FPS、序号缺口/不完整帧/drop/write error/timeout/NCM TX drop 全为 0；未达到就给出证据和下一单变量，不要说“理论上可以”。
```

### Skill 沉淀状态

- 新建项目 Skill：`.agents/skills/t384-raw16-pipeline-closure/SKILL.md`。
- 触发场景：T384 RAW16、384×288、25 FPS、7 MB/s、DVP/DMA 模拟源、pipeline 背压、lwIP/TCP/NCM/浏览器整链吞吐和 MINI2 source 替换。
- 本轮生成 Skill 草案、元数据和 3 个 eval 提示词并做结构校验；未在三分钟交接窗口内运行 with-skill/baseline 完整基准，后续发现触发或执行偏差再迭代。

## 2026-08-31 DMA 等效源隔离测试（待上板）

- 目标：区分 19.2 FPS 的瓶颈是在模拟源 CPU 逐像素生成，还是在 NCM/TCP/HTTP 下游；不改变 RAW16 分块、队列、协议或浏览器。
- 实现：硬件构建默认 `T384_FRAME_SOURCE_DMA_EQUIV=1`。源初始化时只生成一个 8 行、6144 B 的确定性种子，之后用 CH32H417 DMA1 Channel 1 内存到内存搬运到 pipeline 槽，DMA 完成后才 `commit_write`；队列满仍暂停当前物理帧。种子按每个分块重复，故 `source.kind=dma-equivalent-dvp-source-v1`、`source.synthetic=0`，严格工具只检查帧边界/序号/吞吐，不做全像素图案断言。
- 预期判定：若烧录后完整流达到 ≥25 FPS（≥5.5296 MB/s），CPU 源生成是主要瓶颈；若仍约 19 FPS，则瓶颈在 DVP 之外的 NCM/TCP/HTTP/主机消费链路。DMA 等效结果仍不等于 MINI2 DVP，真实 DVP 的 PCLK、字节序、FIFO、暂停/整帧丢弃和 V5F 跨核共享内存需另测。
- 现场反馈：用户页面观察到 `22.8 FPS`。`/diag` 确认 DMA 等效源已运行，活动快照中 `source.frames=268`、`source.published_frames=268`、`stream.frames=263`，丢帧、协议错误、NCM TX drop 和 HTTP 写错误为 0；队列满/backpressure 仍明显。结论是 CPU 源生成是部分瓶颈，NCM/TCP/HTTP 下游仍有约 10% 缺口。
- 验证边界：主机硬件头文件语法检查通过，主机 smoke test 仍覆盖 CPU fallback；已完成用户 MRS 构建、烧录和 DMA 运行确认，但尚未完成 Windows 侧 60 秒严格工具（WSL Python socket 返回 `Operation not permitted`），也未完成真实 MINI2 DVP。

## 2026-08-31 RAW16 流帧率为零的根因

- 现场出现 `source.fps_x1000≈32544`、`pipeline.queued_chunks=12`、`pipeline.acquire_no_slot` 持续增长、`source.published_frames=1`、`stream.frames=0`。这不是浏览器 CRC 误判，也不是 NCM 未枚举；旧模拟源在固定 12 槽队列满时 abort 当前帧，HTTP 只能发送缺少 `FRAME_END` 的半帧，浏览器按设计全部丢弃。
- 修正后的模拟源在获取不到队列槽时归还本次 credit 并暂停当前物理帧，待消费者释放槽后继续提交；只有真正的 DMA/填充/协议错误才 abort。这样流帧率可能低于源目标帧率，但每个显示帧必须完整，不能用“收到零散字节”冒充画面帧。
- 真实 DVP 若不能暂停硬件采集，必须在 source adapter 内提供整帧暂存或在开始发布前实现整帧丢弃策略，绝不能把半帧暴露给浏览器；这仍是 MINI2 上板前的接口约束。

## 2026-08-28 RAW16 正式流水线边界

- 模拟验证不得在 HTTP/NCM 发送循环中临时生成像素，也不得把 magic、序号或尺寸写进 RAW16 像素。唯一允许模拟的是采集源；模拟源必须与未来真实 DVP 源使用同一个 `t384_frame_source.h -> t384_frame_pipeline.h` 入口。
- 当前正式下游是 `source adapter -> 12×6144 B 固定 SPSC ring -> 36 B RAW16LE-CHUNK-V1 envelope -> lwIP/TCP -> USB NCM -> 浏览器完整帧重组/伪彩`。HTTP 只消费已提交分块，浏览器只显示完整且偏移/序号/CRC 合法的帧；模拟源遇到队列满时暂停当前帧，真实 DVP 若不能暂停则必须在 source adapter 内整帧丢弃/暂存，不能让半帧进入正式队列。
- 同核 V3F 接 MINI2 时，只替换 `t384_frame_source_sim.c` 的 source API 实现；queue、wire、HTTP/NCM、浏览器和诊断不变。DVP ISR 推荐只更新 source-local DMA 完成标志，由 `t384_frame_source_task()` 串行操作队列元数据；commit 前 DMA 必须完成，abort 前 DMA 必须停止。
- “只换采集源”有硬件前提：当前没有证据证明 V5F DMA 可写 V3F 数据 RAM，也没有已验证的跨核大块 ring。若最终强制 V5F 采集，先验证共享地址、cache/屏障和通知，再把 IPC 封装进 source adapter；不得把跨核能力当成现状。
- 当前验证只覆盖 DVP 数据进入 RAM 之后的分块、背压、传输和浏览器伪彩，不覆盖 MINI2 电气/时序/字节序/信息行，也不包含正式 KT/BT/NUC-T 测温。7,000,000 B/s 对 384×288 RAW16 是约 31.648 fps 的压力门，不是 25/25.5 fps 产品档位。
- 设备端只保留 73,728 B 独立对齐 DMA payload、192 B 带外元数据和累计计数，不保存帧历史；累计字节使用一致性快照下的 64 位真实值。产品页面不包含 `?mock` 假运行入口；浏览器记录仍默认关闭，只有用户主动开始才写本机 IndexedDB，最多 86,400 点。

## 2026-08-28 OV2640 连续 25 fps 闭环记忆

- 原 `/capture.jpg` 的首次 `capture started; retry shortly` 不是相机故障，而是单帧触发式架构的预期 503；该架构不满足“打开即连续流畅 25 fps”，不要恢复成页面定时轮询单帧。
- 当前已验证链路是 WCH 官方 CH32H417 板上的 `OV2640 352×288 JPEG → DVP 双 64 KiB 帧槽 → lwIP raw TCP multipart MJPEG → USBHS NCM → Windows`，页面和直接流地址分别为 `http://192.168.18.1/`、`http://192.168.18.1/stream.mjpg`。
- 真实持续验证：5 秒 144 帧（28.8 fps），20 秒 572 帧（28.6 fps），每帧均有匹配 SOI/EOI；活动时源端和排入 lwIP 的帧率均为 28–29 fps，NCM TX drop、坏帧、FIFO/64 KiB 溢出、超时、无槽丢帧、HTTP 写错误均为 0。
- 相机 SCCB 的 WCH `Delay_Us/Delay_Ms` 会临时接管并停止 SysTick0。必须先完成 OV2640 寄存器初始化，再调用 `t348_time_init()`；否则 lwIP 定时器、FPS 和慢客户端超时会失效。
- 115200 UART 的长周期诊断是阻塞发送，一秒打印数百字节会制造约一帧量级的主循环停顿；连续流固件使用 `/diag`，不要恢复每秒长 UART 日志。
- 双槽必须区分 capture/complete/processing/ready/leased；解析 JPEG 时不能提前释放 processing 槽，否则 DVP ISR 可能重用并覆写它。慢客户端必须超时断开并释放 leased 槽。
- 本闭环只验证 OV2640 JPEG，不等于 MINI2 产品数据面完成。下一阶段仍需单独实现并验证 MINI2 RAW16、384/640 帧完整性、字节序、DMA/cache、USB持续吞吐和测温链路。
- 24 小时稳定性有一个确定的协议风险：当前 DHCP lease 为 86,400 秒，而 DHCPREQUEST 处理强制依赖 Option 50，未覆盖只用 `ciaddr` 的 T1/T2 续租。不能用延长 lease 掩盖；应正确处理续租并跨越至少一次真实续租验证。
- Windows 当前报告 NCM `LinkSpeed=480 Mbps`；OV2640 20 秒实测只有约 0.164 MB/s。T384 RAW16 25 fps 需要 5.5296 MB/s，T640 25 fps 需要 16.384 MB/s，50 fps 需要 32.768 MB/s；USB2 理论值不能替代合成满载和真实 RAW16 持续测试。
- T640 RAW16 单帧 655,360 B，超过 V3F 320 KiB 与参考 V5F 256 KiB 的单核数据 RAM，最终数据面必须采用分块/有界环形流水线和明确背压/丢帧策略，不能照搬 OV2640 整帧槽。

## 用户偏好（合作中反复确认过）

- 默认使用中文，结论直接、具体；不接受没有证据的“应该没问题”。
- 硬件审核重点是“需求和板子是否一致”，优先报告真正可能导致打板失败的接线、电源、方向和电平问题，不要用大量“没问题”掩盖关键风险。
- 表格顺序：合理项放前面，不合理项放后面；问题要写清严重度、证据、为什么错、怎么改。
- 审核原理图必须核对连接器的真实物理 pin、模块端命名、MCU 物理 pin、网络连通性和信号方向；不能只看相似网名或芯片引脚号。
- MINI2 不是两路同时连接；P2 和 U4 共享 USB 线在“同一时刻只插一个接口、供电隔离”的前提下不要重复判为主问题。
- 用户已经明确：WORK_MODE 不使用、CAM_5V_EN 不需要、U4 是烧录/服务接口、epro2 不必查看。本轮及后续不要反复建议这些已冻结决策，除非需求发生变化。
- R30=100k 已经取消；不能再把旧文档中的 R30 记录当作当前原理图状态。
- 用户要求先审核、先汇报，不要未经授权改原理图；本次明确要求更新 HANDOFF 和 memory 时才修改这两个文件。
- 每次原理图变更后必须重新确认最新导出文件的时间、页数、文本和视觉内容。
- 用户重视可执行接线表：要写清 VCCA/VCCB、A/B 侧、TX/RX 交叉、OE、电源域和实际连接器 pin。

## 从错误中学到的最佳实践

- **同名不代表同方向。** MINI2 的 UART0/UART1 命名是模块对端视角。必须按物理 pin 和方向核对：模块 TX→CH32 RX，模块 RX→CH32 TX。
- **MINI2 UART1 不等于 CH32 UART1。** 当前设计使用 MINI2 UART1，经 U8 接到 CH32 `PC10/PC11`，对应 CH32 `USART3_TX/RX`。审查时必须分别写清“模块接口名称”和“MCU 外设名称”。
- **先看物理 pin，再看网络名。** `IIC_MST1_SCK_PAO146` 的尾部 6 是 CN1 接口编号，不是 PA14 的一部分；当前应核对 CN1 pin6 与 U7 B2 是否实际同网，并确认 MCU 是 PA14。
- **PDF 文字不是网络连通性证明。** 无实体线的标签、跨页网络和相似名称必须在源工程网络高亮、ERC 或导出网表中确认；只看 PDF 时必须声明证据边界。
- **XO 下拉阻值是 CH32 功能配置。** CH32H417 的 XO 约 60–110kΩ 下拉会选择 VIO18 默认 1.2V；目标 1.8V 时 R30 应浮空/DNP或按数据手册选择对应阻值。最新 PDF 已移除 R30，旧记录已解决。
- **SGM4553 是双向信号转换器，不是电源域可互换器件。** A 侧固定 CH32 `VIO_1.8`，B 侧固定 MINI2 3.3V，OE 使用 `LV_EN`，不能因为信号双向就交换 VCCA/VCCB。
- **先区分三类结论。** P0 是确定会阻断功能或有重大电源安全风险；P1 是应修正的标注/可靠性设计；P2 是需要上板、固件、布局或真机验证的未知项。静态检查通过不等于整机完成。
- **不要被旧交接文件误导。** 先查最新 PDF 的文件时间和 PDF 创建时间，再把旧 HANDOFF/规划文档中的问题逐项标记为已修复、仍存在或已被用户决策取消。
- **不要为了“全接上”制造功能。** 未使用的 WORK_MODE、nRST、额外串口或模块 USB/MIPI 可以按接口资料浮空/NC；是否接入必须由产品需求决定。
- **不要把相邻方案当本项目证据。** AX620Q/MIPI 原理图、256×192 `web.zip` 和厂商 UVC 示例不能直接证明 CH32H417 + T384/T640 + USB NCM 方案成立。
- **公共 preflight 没有项目项时不能冒用其他项目。** 记录 `invalid choice`，然后继续项目特定的只读检查。

## 项目关键约束和坑

- 产品目标是 T384/T640；暂不做 256×192。
- 产品链路是 MINI2 → 8-bit DVP → CH32H417 V5F + DMA/缓存 → USB NCM → 本地 HTTP/Web；不是 UVC 产品，也不是 MIPI 方案。
- MINI2 主电源要求 5V±10%，启动探测器约有 500mA 浪涌。5V 输入经过肖特基、保险丝、负载开关或其他路径后的压降必须按最坏条件核算和实测。
- 当前最新 PDF 的 P2 VBUS 经 D1/D2 `LMBR4010BST5G` 到 VSYS。两只并联路径不等于在最低 VBUS、浪涌和温升条件下自动满足 MINI2 pin1/pin50 的 5V±10%；这是当前首板最重要的电源风险。
- `VDD_3V3_EXT=3.3V` 是 MINI2 电源，不代表 DVP/I²C 的逻辑电平被切到 3.3V。默认上电的 DVP/I²C 域是 1.8V；UART 和 nRST 是 3.3V。
- 如果需求改成 DVP/I²C 直接工作在 3.3V，当前无 DVP 电平转换的板子不能直接沿用；必须重新评审电平和启动切换时序。
- CH32 电源关系：`VDD33 ≥ VDD33A ≥ VDDIO ≥ VIO18`；`VDD12A/VDDK` 目标约 1.17–1.27V；当前 U2 分压约 1.23V。U2 标题仍错误写成 `VSYS→1V8`，实际是 `VDDIO_1V2`。
- CH32 VIO18 默认值受 XO 下拉配置影响；不能只看电容和网络名推断电压。
- DVP 连接器命名 `DVP_DATA8..15` 对应模块 DATA0..7，当前正确映射为 `PC6..PC9/PD12..PD15`；CLK/HSYNC/VSYNC/FSYNC 为 `PB12/PB13/PB14/PC12`。
- DVP RAW16 的字节序、采样沿、行场消隐和信息行仍需逻辑分析仪确认；不能把“8 根数据线接对”写成“视频协议已完成”。
- MINI2 UART 基线是 115200 8N1、无流控；UART0 经 U5 接 CH32 USART4，UART1 经 U8 接 CH32 USART3。
- MINI2 I²C 从地址是 7-bit `0x3C`；`0x78/0x79` 只是 8-bit 写/读地址。命令 buffer 起始地址 `0x1D00`，状态寄存器 `0x0200`，CRC-16/XMODEM 必须用向量验证。
- SGM4553 内部上拉可支持当前 I²C 方案，但实际上升沿、总线电容、stub 和布局必须上板测；不要仅凭“无需外部上拉”宣称高速稳定。
- WORK_MODE 未使用时保持 MINI2 pin28 浮空；PD10 不得被固件推成 3.3V。nRST 未使用时可浮空，但这意味着没有软件控制机芯复位能力。
- USB-C P2 为设备侧公头，CC1/CC2 各 5.1kΩ 下拉；A6/D+ 必须到 USBHS_DP/PB8，A7/D−必须到 USBHS_DM/PB9。
- P2 与 U4 共享 USB D+/D−只能建立在单接口使用约束上；若未来要求两口同时可插入，必须增加切换、互斥和回灌保护。
- 当前 USB3 SuperSpeed 未连接，只能按 USB2 NCM 评估。640×512×16-bit 原始流为约 16.384MB/s@25fps、32.768MB/s@50fps，持续吞吐和 NCM 移动端兼容仍未证明。
- U4 物理上是 USBHS 烧录/服务口，不是 SWD。是否能通过 USB Boot/IAP 进入和恢复，取决于 CH32 启动条件与固件；如果改用 WCH-Link SWD，必须另有 SWD 通路或测试点。
- 没有真实板卡时，不能宣称电压、帧率、吞吐、NCM、iPhone/Android/PC兼容、测温精度或烧录恢复已经完成。

## 当前最新状态（2026-08-26）

- 最新审核文件：`docs/SCH_CH32H417WEU6-R0_2026-08-26.pdf`，2页，PDF创建时间 16:13:02。
- 已确认：DVP、UART0、MINI2 UART1、I²C 主/从、USB D+/D−、SGM4553 电源方向、CH32 3.3V/1.23V/VIO18 电源域、WORK_MODE/nRST/CAM_5V_EN 的当前取舍。
- 当前 P0：VSYS 经过 D1/D2 后的最坏压降未闭环。
- 当前 P1：U2 `VSYS→1V8` 标题错误；USB D+/D−缺低电容 ESD 是量产可靠性建议。
- 当前 P2：U4 USB IAP、NCM、DVP时序、PCB DRC/阻抗、晶体负载、真实上电和热测试未验证。
- `HANDOFF.md` 已更新为本状态；本文件作为长期项目记忆，不替代原始接口表和数据手册。

## 下次一次性达到本效果的推荐提示词

```text
请接手当前 T348 项目，先只读执行：
1. 读取项目 AGENTS.md、HANDOFF.md、docs/runbooks/PROJECT_MEMORY.md、README.md、C_context/KNOWN_FAILURES.md；检查当前可用 Skills。
2. 查找最新原理图导出文件，记录文件时间、页数和版本；不要默认旧 PDF、旧源工程、BOM 或 epro2 同步。除非我明确要求，不看 epro2、不改代码/原理图。
3. 运行项目对应 agent_preflight.py；如果公共脚本没有 T348 项，不要冒用其他项目，记录失败原因并继续。
4. 按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”闭环审核：逐个核对真实连接器物理 pin、模块端信号方向、MCU 复用、网络连通性、电源域、电压、默认状态、USB极性、未使用脚和数据手册。
5. 需求基线是 T384/T640：MINI2 5V±10%/约500mA浪涌，DVP/I²C默认1.8V，UART/nRST为3.3V，8-bit DVP，USB NCM+本地Web；不要把 UVC、MIPI、256×192或历史方案混进结论。
6. 输出表格时先列合理项，再列不合理项；不合理项分 P0/P1/P2，写清证据、影响和具体修改/验证动作。只报告真正的问题，不重复已经由我明确取消或确认的项目。
7. 最后给出能否打板的明确结论、已运行命令、未验证项，并把当前状态写入 HANDOFF.md；如果我要求沉淀，再更新 PROJECT_MEMORY.md。
```

## Skill 沉淀

- 已创建项目内 Skill：`.agents/skills/t348-schematic-handoff-review/SKILL.md`。
- 触发场景：接手 T348/T384/T640、审核最新原理图、核对 MINI2/CH32 接口、电平、电源、USB、烧录或要求生成 30 秒可接手的 HANDOFF。
- Skill 固化：最新文件发现、资料优先级、物理 pin 优先、UART方向、PA14/pin6 消歧、R30/VIO18、电源压降、U4/P2 单口约束、P0/P1/P2 报告和验证边界。
- 测试提示词保存在 `.agents/skills/t348-schematic-handoff-review/evals/evals.json`，覆盖 UART 同名方向、XO 下拉和旧版文件不同步三类失败。
- 本轮只创建 Skill 草案和测试提示词，没有运行基准评测；后续如果 Skill 触发效果不稳定，再做 with-skill/baseline 对比。

## 2026-08-27 官方板 USB NCM 联调记忆

### 用户偏好（本轮反复调整后确认）

- 除非用户明确说“改”，默认只读调查；不要一边排障一边自主改源码、工程文件或文档。HANDOFF/memory 也只在用户明确要求时更新。
- 所有固件必须由用户在 MounRiver Studio 编译和烧录；Codex 只做测试工程源码、静态检查和可执行的编译/烧录指引，不能替用户生成或烧录固件。
- 先完成当前唯一阻塞目标。NCM 不通时不要扩展到 OV2640 彩色图像、DVP画面、测温或新硬件；这些旁支即使能工作也不能回答 NCM 问题。
- 已成功工程是保护基线，默认不改。优先把失败工程与 `docs/data/Petros_DVP/`、`firmware/ch32h417_usbhs_diag/` 做精确差异核对。
- 操作说明必须明确到“打开哪个 `.wvsln`、选哪个核、烧哪个 HEX、看哪个 VID/PID、串口哪个 pin”，避免让用户在 V3F/V5F/Merge.bin 之间猜。
- 用户提供的真实硬件结果优先级最高：DVP成功、USBTreeView、UART日志和实际烧录结果比静态推断更可信。
- 不接受“应该可以”。必须区分静态通过、用户编译通过、烧录成功、USB枚举成功和 NCM 网络成功五个层级。

### 从本轮错误学到的最佳实践

- **先分层，不把所有 USB 问题叫 NCM 问题。** `Connection Status 0x00` 是物理 attach/控制器层；读取描述符后才谈 VID/PID和驱动；`mounted=1` 后才谈 NCM、DHCP、HTTP。
- **不要选错观察对象。** `VID:PID=1A86:5537`、产品 `CH32H417`、驱动 `CH375W64.SYS` 是 CH372/CH375 诊断固件，不是 NCM。NCM 测试版应查 `1A86:E348`。
- **“DVP工程成功”只能证明共同硬件基线。** 它排除板卡、线材和部分 USBHS 初始化后，剩余首要嫌疑是失败工程自写的 TinyUSB DCD/EP0，而不是摄像头、周期或 lwIP。
- **比较工程必须比较元数据，不只比较 C 代码。** 本轮静态脚本暴露 NCM `.wvproj` 错选 `CH32H417QEU`，成功官方板基线为 `CH32H417WEU`；还要核对 `target_path`、`downloadMerged`、power-out 和清 Flash 设置。
- **串口宏名不等于物理引脚。** NCM 原先默认 UART6/PA12；切到 UART8 后，其本地 `debug.c` 又曾映射 PB4，而成功诊断库是 UART8/PA15。必须同时核对 `debug.h` 选择和 `debug.c` GPIO AF。
- **单 V3F NCM 不需要 V5F。** 工程只有 V3F 时应烧 `T348-NCM_V3F.hex`；旧 V5F 固件未被 V3F 唤醒就不会自动运行，不能凭“Flash里可能有旧V5F”推断它阻塞 USB。
- **驱动缓存需要独立 PID，但它不是 attach 修复。** FE1C 曾用于 UVC，5537绑定 CH375；用本地临时 E348 可隔离缓存。主机完全看不到设备时仍应查 DEV_EN/IRQ/EP0。
- **ISR 中不打印。** 只累加 `volatile` 计数，在主循环每秒打印快照，避免 UART 阻塞改变 USB 时序。
- **无输出的检查失败要追踪脚本。** `bash -x tools/check_ncm_firmware.sh` 定位到 MCU 型号断言，不能因编译器无报错就把整个脚本说成通过。
- **主机 GCC 静态检查不是 WCH 目标编译。** 厂商 `debug.c` 原有的 `_write(fd)` 告警和 `ptrdiff_t` include差异应与本次改动分开；不要为让主机检查好看而顺手重写厂商库。

### 当前关键约束和坑

- 当前联调硬件是 WCH 官方 CH32H417WEU 板，不是 T348 自研板；当前 USB 使用 USBHS/USB 2.0 High-Speed、PB8/PB9，不是 USBSS/USB 3.0。
- 当前 NCM 工程入口为 `firmware/ch32h417_t348_ncm/T348-NCM.wvsln`，只有 V3F；下载 `V3F/obj/T348-NCM_V3F.hex`，不选 V5F/Merge.bin。
- 本项目 NCM 控制面地址统一为 `192.168.18.1/24`，DHCP 地址池为 `192.168.18.2-.4`；`192.168.17.1` 仅属于旧地址基线/另一项目，不能混用。
- 当前测试身份为 `1A86:E348`，使用未分配的 WCH VID/PID 组合，只限本地排障，正式产品必须取得合法身份并重测所有主机。
- 当前调试输出已对齐成功诊断工程：USART8/PA15、115200 8N1。期望启动三行后每秒一行 `USBHS diag`。
- `docs/data/Petros_DVP/` 和 `firmware/ch32h417_usbhs_diag/` 是只读成功基线；不要把主线继续开发在参考目录，也不要破坏已成功诊断工程。
- NCM 自写 TinyUSB DCD 的 EP0/IRQ 尚未上板证明；静态看时钟、UTMI、DMA、DEV_EN 与成功基线接近，最有价值的新证据是 reset/setup/address/mounted 计数。
- 当前 README 仍保留旧的 QEU/FE1C/“未构建”描述，本轮因用户限定只更新 HANDOFF、memory 和 Skill而未改；恢复工作时以 `HANDOFF.md` 顶部和实际源码为准，后续获授权再同步 README。
- `mounted=1` 前不要排查 DHCP/HTTP；`mounted=1` 后再看 WINNCM、SET_INTERFACE、NTB、ARP和 `http://192.168.18.1/`。
- 当前仍未由用户编译/烧录本轮 E348 版本，未上板验证，不能写成“NCM 已实现并可用”。

### 下次一次性达到本轮效果的推荐提示词

```text
请使用项目 Skill `$t348-usb-ncm-bringup` 接手 T348 官方 CH32H417WEU 板的 USB2 NCM 联调。先读取 AGENTS.md、HANDOFF.md、docs/runbooks/PROJECT_MEMORY.md、README.md、/home/slam/Sipeed/C_context/KNOWN_FAILURES.md 和 docs/logs.txt，运行真实可用的 T348 preflight；没有项目项就记录限制，不冒用其他项目。

本轮唯一目标是让 `firmware/ch32h417_t348_ncm` 在 Windows 完成 USB NCM 枚举，再验证 DHCP/HTTP；不要测试 OV2640、DVP、测温或自研板。以 `docs/data/Petros_DVP/` 和 `firmware/ch32h417_usbhs_diag/` 为只读成功基线。先按“固件身份→UART执行→USB attach/IRQ→EP0描述符→WINNCM绑定→NCM/DHCP/HTTP”分层定位，并按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”汇报。

未经我明确说“改”不要修改任何文件。若我授权修改，只做最小测试源码，不动成功基线；所有固件必须由我在 MRS 编译和烧录。每一步明确告诉我打开哪个 wvsln、选 V3F 还是 V5F、下载哪个 HEX、串口引脚/波特率、USBTreeView应查哪个 VID/PID，以及日志各计数如何决定下一步。不要把静态检查写成上板成功。
```

### Skill 沉淀

- 项目 Skill：`.agents/skills/t348-usb-ncm-bringup/SKILL.md`。
- 固化内容：官方板/自研板边界、V3F-only 烧录、CH372与NCM设备身份区分、分层枚举判定、WEU/QEU工程坑、UART宏与真实 AF 映射、独立测试 PID、ISR计数策略和用户编译边界。

## 2026-08-27 NCM 已枚举、DHCP/APIPA 未闭环的合作记忆

本节是当前最新 NCM 长期记忆；与上方旧状态冲突时以本节和 `HANDOFF.md` 顶部为准。

### 用户偏好（本轮反复调整后确认）

- 用户只接受结果和合理内存占用，不希望为了架构漂亮而扩大改动；每次修复必须有直接证据和最小验证。
- 用户明确说“看最新日志”时，必须立即读取 `docs/logs.txt` 全部最新内容并以真实日志覆盖旧推断，不能继续引用过期 USBTreeView/HANDOFF。
- 用户明确说“已经烧录最新”时，应把这条真实硬件反馈作为最高优先级事实；先核对源码/HEX 时间形成版本证据，再推进下一层，不能反复让用户“再烧最新”。
- WSL 能直接判断网口。应优先自己运行 `ip/ifconfig/route/curl --interface` 等只读验证，不要把可自动完成的网络判断转给用户。
- 最终产品必须即插即用。Windows NCM IPv4 保持自动获取；不能把手工静态 IP 当作修复或要求最终用户配置。
- 用户不接受长时间无反馈。少量网络命令不应拖成几十分钟；若权限/沙箱阻断，要快速说明并切换可行方式，不重复空转。
- 用户要求明确区分“当前已烧录版本”和“刚刚才修改、尚未构建的源码”。每次源码改变后都要说明当前板上是否可能包含该改动。
- 用户不接受把猜测性修改表述为已定位根因。必须说清“证据支持”“候选修复”“已烧录验证”“仍然失败”四种状态。
- 当前只解决 NCM/DHCP/HTTP；不要扩展到 OV2640、DVP、RAW16、测温或自研板问题。
- 所有固件仍由用户在 MRS 构建和烧录；代理负责源码、静态检查、日志判读和精确的一次性操作说明。

### 从错误里学到的最佳实践

- **版本链必须闭环**：分别记录源码修改时间、MRS 构建产物时间、用户烧录确认和运行日志行为。不能因为工作区源码更新，就假定板上已运行该源码；也不能在用户确认最新烧录后继续假定是旧固件。
- **APIPA 的含义要一次说对**：`169.254.x.x` 是 Windows DHCP 失败后的回退，不代表网卡设错，也不表示 USB 枚举失败。`mounted=1 + APIPA` 应直接进入 DHCP Discover/Offer/Request/ACK 分层。
- **指定接口的 HTTP 200 只证明特定层**：`curl --interface eth6` 访问旧 `.17.1` 成功，证明 NCM 单播和 HTTP 基础路径可工作；它不证明 DHCP 成功，也不证明新 `.18.1` 固件已验证。
- **地址段切换不是 DHCP 修复**：从 `192.168.17.1` 改成 `192.168.18.1` 只用于避免与另一项目混淆，不能改变 APIPA 根因。
- **不应先靠猜测连续改 DHCP**：本轮先加发送缓冲重试、再把 DHCP 回包改为 `udp_sendto_if()`，但没有先加分阶段计数；用户烧录后仍 APIPA，说明这种流程证据不足。正确顺序是先仪表化，再根据 `discover/offer/request/ack` 计数只改失败层。
- **修复声明必须以后验验证为准**：静态语法通过、参考实现相似或理论上更合理，都不能写成“问题解决”；只有自动获得 `.18.2-.4/24` 并正常打开 `.18.1` 才算 DHCP/HTTP 闭环。
- **最新日志已证伪旧分支**：`init:1/0`、`addr:1`、`mounted:1`、`unhandled:0` 且传输计数增长，已排除当前阶段的 attach、EP0、Code 43 和“没有 NCM 设备”。不要回退排查。
- **主机路由和设备 DHCP要分清**：无 `.18.0/24` 地址/路由时，普通浏览器会走默认物理网卡；绑定 `eth6` 可做诊断，但正式方案仍必须让设备 DHCP 自动配置直连子网。
- **工具权限失败不是设备结论**：WSL 添加地址因缺少 `CAP_NET_ADMIN` 返回 `RTNETLINK` 错误，只说明当前执行环境不能改接口；不能据此判断 Windows 或固件。
- **静态脚本失败要报告真实失败点**：当前完整检查因 `.wvproj` 实际写 `CH32H417QEU`、脚本要求 `CH32H417WEU` 而失败；相关文件语法检查通过不等于整个脚本通过。
- **HANDOFF 顶部必须实时覆盖历史**：旧 HANDOFF 曾仍写“枚举未知”，而日志已是 `mounted=1`。长交接文档必须把唯一实时状态放在顶部，并明确后文为历史，否则会让下一 session 重复旧排查。

### 项目关键约束和坑（当前 NCM 阶段）

- 当前真实硬件是 WCH 官方 CH32H417WEU 板；当前主线是 V3F USBHS/USB 2.0 CDC-NCM 控制面。V5F、DVP、RAW16 和测温尚不属于当前闭环。
- MRS 入口固定为 `firmware/ch32h417_t348_ncm/T348-NCM.wvsln`；下载 `V3F/obj/T348-NCM_V3F.hex`，不选 V5F，不烧 `Merge.bin`。
- 最新真实 UART 日志已证明 USB 枚举完成：`mounted=1`。当前唯一 P0 是 Windows DHCP 租约失败，NCM 接口 `eth6` 为 `169.254.79.116/16`。
- 本项目地址固定为 `192.168.18.1/24`，DHCP 池为 `.18.2-.4`；另一项目/旧固件使用 `.17.1`。排障时先确认正在测试哪一版，禁止混用两个地址。
- DHCP 当前故意不下发默认网关和 DNS，避免抢占主机普通联网流量；不要为“看起来像路由器”随意增加 gateway/DNS。若怀疑 Windows 兼容性，必须抓 Offer/ACK 后再评审选项。
- 最终用户不应配置静态 IP。静态 `192.168.18.2/24` 只能作为实验性单播/路由隔离测试，不能写进产品步骤。
- 当前源码包含两个尚未证明解决问题的候选变化：`ncm_link_output()` 的 8 次有限重试、`dhserver.c` 的 `udp_sendto_if()`。最新烧录仍 APIPA，后续不得把它们当成根因已修。
- 下一轮最有价值的源码不是第三个猜测性修复，而是 DHCP/NCM 计数：`discover/offer_attempt/offer_err/request/ack_attempt/ack_err/malformed/no_entry/no_netif` 与 `rx_frame/rx_drop/tx_frame/tx_backpressure`。
- 判定树：`discover=0` 查 Windows 是否发包和 NCM OUT；`discover>0, offer=0` 查解析/资源；`offer_ok>0, request=0` 查 Offer 格式和 Windows 接受；`request>0, ack_ok>0` 仍 APIPA 则抓 ACK 内容/时序。
- 开发 VID/PID 为 `1A86:E348`，只用于隔离缓存，禁止发布。串口为 USART8/PA15、115200 8N1，ISR 内禁止打印。
- 当前 `.wvproj` 实际仍为 `CH32H417QEU`，而真实板为 WEU；固件可运行但工程元数据/静态断言冲突仍需单独收敛，不要把它误判成已证实的 DHCP 根因。
- 工作区 `.git` 是空目录，不是可用 Git 仓库；不能依赖 `git status/diff` 证明本轮文件边界。

### 下次一次性达到当前效果的推荐提示词

```text
请使用项目 Skill `$t348-usb-ncm-bringup` 接手 `/home/slam/Sipeed/T348` 的 CH32H417 USB2 CDC-NCM 联调。先完整读取 AGENTS.md、HANDOFF.md 顶部实时状态、docs/runbooks/PROJECT_MEMORY.md 最新 NCM 章节、README.md、/home/slam/Sipeed/C_context/KNOWN_FAILURES.md 和 docs/logs.txt，并运行真实可用的 preflight；公共脚本没有 T348 项就记录限制，不冒用别的项目。

当前硬件是 WCH 官方 CH32H417WEU 板，工程是 firmware/ch32h417_t348_ncm/T348-NCM.wvsln，只用 V3F。用户负责 MRS 构建和烧录。当前最新事实是 USB 已 mounted=1，Windows/WSL NCM 接口 eth6 仍为 169.254.x.x；本项目设备地址 192.168.18.1、DHCP 池 192.168.18.2-.4。不要再排查 attach、EP0、Code 43、DVP、OV2640、测温，也不要要求用户设置静态 IP或重复烧录所谓“最新版本”。

按“目标→状态→误差→控制动作→反馈→修正→验证→沉淀”推进。先核对源码时间、HEX 时间、用户烧录确认和运行日志，建立版本链；然后只增加 DHCP Discover/Offer/Request/ACK 与 NCM RX/TX/backpressure 分阶段计数，在主循环打印，ISR 内不打印。拿到计数前不要继续猜 DHCP 修复。只有 Windows 自动获得 192.168.18.2-.4/24，且无需指定接口即可打开 http://192.168.18.1/，才能报告问题解决。每轮说明实际改动、实际命令、结果和未验证项；用户说“看日志”时立即读取 docs/logs.txt 并以它覆盖旧结论。
```

### Skill 沉淀状态

- 复用并更新现有项目 Skill：`.agents/skills/t348-usb-ncm-bringup/SKILL.md`，不重复创建同类 Skill。
- Skill 应覆盖：版本链、`mounted=1 + APIPA` 决策树、WSL `eth6` 定向验证、禁止静态 IP 作为产品方案、DHCP 四阶段计数优先、候选修复与已验证修复的状态区分、30 秒 HANDOFF 结构。
- Skill 元数据位于 `.agents/skills/t348-usb-ncm-bringup/agents/openai.yaml`；更新后应运行 skill-creator 的 `quick_validate.py`。
