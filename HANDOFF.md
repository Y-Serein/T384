# T384 当前交接（2026-09-17）

## 30秒恢复：最新优化固件用户实测约29FPS；工作暂停，持续稳定性待验收

### 当前状态

- 最新指令：用户要求“你的工作暂停，我去测试”。代理已暂停主动采集和固件修改；本次仅按用户要求更新HANDOFF。不要自行恢复测速、再次要求下载或占用唯一RAW16流，等待用户测试反馈或明确恢复指令。
- 最新反馈：“实测29帧了最新固件”。用户已确认使用最新TCP对齐读取优化固件，约29FPS，超过≥25FPS速率目标；尚无代理对该版的60秒完整帧/跳号/CRC报告或10分钟及重连验收，不能写成完整稳定流程已完成。
- B方案固定为V5F采集/V3F网络。优化前双帧v2已确认身份/五段自检，Windows60秒19.567FPS；该成绩是优化前基线，不能作为当前29FPS版的测速结果。
- 用户USB重插后网络恢复，Windows读取双帧身份、两核boot/init及五段访问自检均1。并发每2秒diag的单流运行约83设备发送帧后读超时，诊断仍响应；source约30FPS、HTTP输出约20FPS，TCP背压增长，NCM TX背压/drop为0。不能认定源或NCM容量为瓶颈。
- 不并发diag的Windows原生60秒严格工具：1174完整帧/19.567FPS/4.328MB/s，跳号626、逆序0、不完整帧0，测试因吞吐与跳号退出1；未满足≥25FPS。日志/tmp/t384-double-no-diag.log。用户页面约20FPS得到独立实测支持。
- 新版源身份mini2-dvp-v5f-double-frame-v2，IPC版本2、frame_banks=2；第二帧216KiB分为ITCM96、DTCM18、共享代码42、共享数据60KiB。两核map IPC944B，V3F堆余35176B、V5F16556B，Merge.bin218544B。启动必须验证全部五段访问成功；不能混用旧单帧核。
- 前两处HTTP修复已用户下载并实测：关闭前隔离旧PCB回调，静态页面按发送窗口持续补充。Windows60秒601完整帧/10.003FPS、最长间隔131ms、半帧/逆序0、诊断无错误，3次重连恢复82–88ms。该历史成绩只证明当时稳定性要求，不满足新≥25FPS要求。
- 未commit/push/发布/自动烧录，保留用户已有未提交改动。保持Erase All/Clear CodeFlash关闭；合并BIN含FF空洞，标定槽实际保留尚未验证。

### 已尝试及结果

- bash tools/check_dualcore_firmware.sh：双规格实际DVP ISR/真实pipeline、101帧消费与采集交错、慢消费完整帧丢弃、短帧不破坏另一个读租约、序号回绕、scratch互斥、384 ASan/UBSan及跨核RPC通过。LeakSanitizer关闭。
- tools/build_dualcore.ps1：实际MRS WCH GCC12.2.0两核目标Build、HEX合并与产物检查通过。bash tools/check_raw16_bench.sh完整日志通过；python3 tools/check_dualcore_artifacts.py通过。
- 双帧并发诊断测速退出1，报告out/stability/after-double-frame-windows.json的stable=false、windows为空；最后有效活动诊断累计stream.frames=83，随后读超时。不能用该报告计算60秒FPS；有效60秒结果来自不并发diag的严格工具日志。
- 最新优化的完整回归bash tools/check_raw16_bench.sh已结束，日志/tmp/t384-aligned-copy-full.log最终显示RAW16双核检查通过；目标构建日志/tmp/t384-aligned-copy-target.log。此前“仍运行中”已过时。
- TCP融合copy/checksum优化只加入已验证的源4字节对齐提示，保留alias-safe memcpy及非对齐目标写入；多对齐/长度lwIP参考对照及ASan/UBSan通过，HTTP回归通过。实际新汇编每8字节源读取由8次lbu改为2次lw；WCH两核构建/合并检查通过，IPC和堆余不变。diag新增http.checksum_aligned_reads=1。用户随后确认最新固件约29FPS；相比优化前19.567FPS基线有明显改善，但用户短时速率与代理严格完整帧验收口径不同，持续稳定性仍待验证。

### 下一步（用户恢复工作后执行）

1. 用户已确认最新优化固件约29FPS，不再要求重复下载；等待用户明确恢复指令，暂停期间不访问设备或占用用户测试流。
2. 获准恢复且用户测试结束后，读取diag确认http.checksum_aligned_reads=1、双帧身份、两核boot/init和五段自检1。仅允许一条RAW16流；确需关闭页面时提供http://192.168.17.1/完整链接。
3. Windows原生60秒完整帧验收≥25FPS/≥5529600B/s，并记录跳号、半帧/CRC/逆序、源丢弃、HTTP超时/写错误和NCM TX drop；并发diag若复现超时，另测不并发diag以定位，不能降低门槛。
4. ≥25FPS通过后再测10分钟+10次重连；未通过则根据活动差分只选一个变量，禁止无证据扩大TCP/NCM缓冲或改变线上协议。

### 关键相对路径

- firmware/Common/App/http_status.c、tests/module_files_http_smoke.c、tools/check_module_files.sh：TCP关闭与静态页面回归。
- firmware/Common/Raw16/t384_frame_pipeline_full.c、t384_dualcore.{h,c}、t384_frame_source_remote.c：双帧/跨核访问。
- firmware/Common/Ld/{V3F,V5F}/、firmware/README.md：内存分区账本。
- tools/build_dualcore.ps1、tools/merge_dualcore_hex.py、tools/check_dualcore_artifacts.py、firmware/V5F/obj/Merge.bin：双核目标构建和下载产物，实际Windows路径C:\Serein_Y\Sipeed\T384\firmware\V5F\obj\Merge.bin。
- firmware/Common/App/arch/cc.h：最新TCP对齐源读取优化；诊断身份http.checksum_aligned_reads=1。
- tools/t384_stream_stability.py、out/stability/after-double-frame-windows.json、out/stability/after-static-refill-windows.json：当前失败和历史实测证据。

### 验证状态

双帧v2主机/目标构建/用户下载及五段自检通过，Windows60秒19.567FPS，完整帧≥25FPS未达成。源对齐读取优化主机对照/ASan/UBSan/HTTP回归及两核目标构建通过，最新完整RAW16回归日志已通过；用户已确认最新优化固件实测约29FPS，尚无代理优化后严格完整帧或持续稳定性报告。先前单帧HTTP修复Windows60秒及3次重连通过但仅10FPS。用户速率反馈已超过25FPS；代理≥25完整FPS/10分钟及重连验收尚未完成。

### 未决问题

USB重插前不可达原因、29FPS版的完整帧/跳号/持续稳定性、并发诊断时流超时是否消失、完整帧跳号及源丢弃、运行时栈、实际下载保留标定槽、手机/其他PC系统、24小时和正式测温均未验证。

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
- **关键路径**：[工程](firmware/T384-RAW16-BENCH.wvsln)、[辐射核心](firmware/Common/Raw16/t384_radiometry.c)、[实验模型配置](firmware/Common/App/t384_product_config.h)、[黑体分析](tools/analyze_blackbody_pair.py)、[标定手册](docs/runbooks/RADIOMETRY_CALIBRATION.md)。
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
