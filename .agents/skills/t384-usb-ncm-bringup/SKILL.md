---
name: t384-usb-ncm-bringup
description: 接手和排查 T384 工作区中 WCH 官方 CH32H417WEU 板的 USBHS/USB 2.0 CDC-NCM 枚举、Windows WINNCM、169.254/APIPA、DHCP、192.168.18.1 HTTP 与 MRS V3F 版本/烧录闭环。用户提到 NCM 不枚举、USBTreeView No device、mounted=1 但网卡为 169.254、DHCP 不分配、17.1/18.1 访问失败、Petros_DVP、V3F/V5F、CH375、PID 缓存、USBHS DCD、EP0、WSL 网口判断或要求生成 30 秒 HANDOFF 时使用。
---

# T384 USB NCM 联调

## 边界

- 当前联调硬件是 WCH 官方 CH32H417WEU 主板；不要用未到货的 T384 自研板问题解释官方板现象。
- 当前目标仅为 USB2 High-Speed NCM 闭环。用户未要求时不测 OV2640 彩色图像，不接 DVP/RAW16/测温。
- 用户负责 MRS 编译和烧录；只交付工程源码，不生成固件、不操作板卡。
- 未获明确授权只读诊断；获授权后也只做最小、可回滚源码修改。
- 不改 `docs/data/` 参考工程。以 `docs/data/Petros_DVP/` 和已成功的 `firmware/ch32h417_usbhs_diag/` 为基线对比。
- 最终用户的 Windows NCM IPv4 应保持自动获取。静态 IP 只能做临时隔离实验，不能作为产品修复。
- 用户确认已烧录最新版本时，将其作为真实硬件证据；先核对源码、HEX 时间和日志，禁止无证据要求重复烧录。

## 开工读取

依次读取 `AGENTS.md`、`HANDOFF.md`、`docs/runbooks/PROJECT_MEMORY.md`、`README.md`、`/home/slam/Sipeed/C_context/KNOWN_FAILURES.md` 和 `docs/logs.txt`（存在时）。检查当前可用 Skills。公共 preflight 没有 T384 项时只记录限制，不冒用其他项目名。

## 闭环顺序

按 `目标 → 状态 → 误差 → 控制动作 → 反馈 → 修正 → 验证 → 沉淀` 工作，每次只推进一层：

1. **版本链**：确认打开 `firmware/ch32h417_t384_ncm/T384-NCM.wvsln`，工程只有 V3F；记录相关源码时间、`V3F/obj/T384-NCM_V3F.hex` 时间、用户烧录确认和运行日志。下载 V3F HEX，不选 V5F，不烧 `Merge.bin`。
2. **执行证据**：先取得启动日志。当前测试源码对齐成功诊断工程，使用 USART8/PA15、115200 8N1。没有日志先查下载目标、供电和引脚，不猜 NCM 描述符。
3. **物理 attach/控制枚举**：USBTreeView 的 `Connection Status 0x00` 表示主机尚未检测到设备，不能归因于 Windows NCM 驱动。已成功 CH372 诊断证明同板、同线、同口 USB2 HS 物理链路后，优先查自制 TinyUSB DCD 的 BUS_RESET、EP0 SETUP/IN/OUT 和 SET_ADDRESS。
4. **描述符/驱动绑定**：设备出现后再检查设备、配置、BOS、MS OS 2.0 `WINNCM` 描述符和 Windows 驱动。不要把 `1A86:5537` CH372/CH375 诊断设备当成 NCM。
5. **网络数据面**：只有 `tud_mounted()` 成功后才检查 SET_INTERFACE、NCM NTB、DHCP、ARP、`http://192.168.18.1/`、重连和吞吐。`169.254.x.x` 是 DHCP 失败后的 APIPA，不是用户静态配置错误。

## 当前测试基线

- 真实板 MCU：`CH32H417WEU`。每次读取 `.wvproj` 的实际 `mcu`；若仍为 `CH32H417QEU`，把它作为独立工程元数据风险，不凭名称直接归因于 DHCP。
- 测试 VID/PID：`1A86:E384`，仅为避免 UVC/CH375 缓存的本地临时身份，不得发布。
- 当前设备地址：`192.168.18.1/24`，DHCP 池 `.18.2-.4`；`192.168.17.1` 属于另一项目/旧基线，不得混用。
- 调试串口：USART8/PA15，115200 8N1，与已成功 USBHS 诊断工程一致。
- USBHS DCD 每秒在主循环打印计数，禁止在 ISR 内 `printf`。
- 运行 `bash tools/check_ncm_firmware.sh` 只验证主机语法、工程元数据和描述符；通过不等于 MRS 构建或上板通过。

## 日志判定

要求用户回传完整的启动三行和一行 `USBHS diag`：

- `init:0/1`：USBHS PLL/时钟初始化失败。
- `init:1/0` 且 `irq/rst/link/xfer` 全零：查固件实际执行、DEV_EN、IRQ、供电和 attach。
- `rst>0` 且 `setup=0`：查 EP0 SETUP 检测/TRANSFER 状态。
- `setup>0` 且 `addr=0`：查 GET_DESCRIPTOR/EP0 数据与状态阶段。
- `addr>0` 且 `mounted=0`：查配置/BOS/类描述符或控制请求。
- `mounted=1`：枚举已完成，转查 WINNCM 绑定和 NCM 网络数据面。

计数快照允许字段间有轻微竞争，只用于定位阶段，不作为协议正确性证明。

## mounted=1 但 APIPA 的处理

出现 `mounted=1` 且 Windows/WSL 网卡为 `169.254.x.x` 时：

1. 读取完整 `docs/logs.txt`，确认 NCM 接口 MAC、RX/TX 和错误计数；WSL 中可用 `curl --interface <ncm-if>` 做定向单播诊断，但不能把它等同于 DHCP 成功。
2. 不再修改 EP0、描述符或让用户设置静态 IP；也不要仅通过改 `.17`/`.18` 地址段声称修复 DHCP。
3. 在主循环输出 DHCP 计数：`rx`、`discover`、`offer_attempt/result`、`request`、`ack_attempt/result`、`malformed`、`no_entry`、`no_netif`。
4. 同时输出 NCM 计数：RX frame/drop、TX frame/backpressure。ISR 只累加，不打印。
5. 按证据推进：
   - `discover=0`：查 Windows 是否发 DHCP、NCM OUT NTB 和帧输入路径。
   - `discover>0 && offer_attempt=0`：查 DHCP 选项解析、地址池和资源分配。
   - `offer_result=OK && request=0`：抓 Offer，核对 Windows 接受的选项、广播和 server-id。
   - `request>0 && ack_result=OK` 仍 APIPA：抓 ACK 内容和时序，不继续猜第三个修复。
6. 完成标准：主机自动获得 `192.168.18.2-.4/24`，无需绑定接口即可打开 `http://192.168.18.1/`。

不要把 `ncm_link_output()` 重试、`udp_sendto_if()`、router/DNS 选项或参考项目差异单独写成根因；只有目标板后验结果才可确认。

## 修改与验证护栏

- USB描述符、VID/PID、端点或 NCM 参数属于协议变更；修改前说明 Windows/Linux/macOS/移动端影响并取得确认。
- 中断/DMA 路径不阻塞、不动态分配、不长日志；缓冲所有权、对齐和 cache 必须明确。
- 不因想快速看到设备而切回 UVC 或套用 CH375 驱动。
- 先加观测再改行为；同一层连续两次猜测性修改仍失败时，必须停止猜测并建立分阶段计数/抓包证据。
- 严格区分源码已改、静态通过、MRS 构建、用户烧录、目标板验证；每次回复说明处于哪一级。
- 不把静态检查、厂商示例成功或一次枚举写成 NCM 产品完成。
- 最终列出实际命令和结果，并明确“未上板验证”的项目。

## 交接沉淀

用户要求交接时，将实时状态写到根目录 `HANDOFF.md` 顶部：精确工程/产物及时间、用户烧录确认、当前日志、USBTreeView VID/PID、主机接口/IP、已证伪项、失败层和 3–5 个下一步。必须替换过期顶部状态，而不是只在文件尾追加。长期偏好、错误教训和稳定坑写入 `docs/runbooks/PROJECT_MEMORY.md`，不要把会快速过时的计数值当长期事实。
