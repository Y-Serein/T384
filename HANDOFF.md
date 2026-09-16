# T384 当前交接（2026-09-15）

## 30 秒恢复：暂停，等WN2384T到货再继续

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
