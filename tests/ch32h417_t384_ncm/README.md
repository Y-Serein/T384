# CH32H417 T384/T640 USB NCM bring-up

这是 T384/T384/T640 的独立 NCM 主线起点，不修改
`tests/ch32h417_t384_base/` 中的 WCH UVC-DVP 编译基线。

## 当前闭环

```text
USB2 HS/FS CDC-NCM -> TinyUSB NTB -> lwIP IPv4 -> DHCP -> HTTP status page
```

- 设备固定地址：`192.168.18.1/24`
- DHCP 地址池：`192.168.18.2` 到 `192.168.18.4`
- DHCP 不下发默认网关和 DNS，避免劫持主机的普通联网流量。
- 页面入口：`http://192.168.18.1/`
- USB 描述符包含 CDC-NCM、Microsoft OS 2.0 `WINNCM` 和 iOS 26
  需要的 Ethernet Filter / NTB Input Size 能力。
- Bulk 端点：EP2 OUT / EP2 IN；通知端点：EP1 IN。
- HS Bulk 最大包 512 字节，FS 回退最大包 64 字节。
- NTB 输入/输出上限均为 8192 字节。

当前代码实现了 NCM/IP/HTTP 最小链路，但静态检查只覆盖语法、工程元数据和描述符，
不证明 USB 枚举、DHCP、IP 或页面可访问。DVP、MINI2 RAW16、帧缓存、测温表和
图像 API 尚未接入。

## 工程入口

- MounRiver Studio：`T384-NCM.wvsln`
- V3F 工程：`V3F/T384-NCM_V3F.wvproj`
- 主循环：`V3F/User/main.c`
- CH32H417 TinyUSB DCD：`Common/USB/dcd_ch32h417_usbhs.c`
- USB 描述符：`Common/USB/usb_descriptors.c`
- NCM/lwIP/DHCP：`Common/App/t384_ncm.c`
- 状态页：`Common/App/http_status.c`

当前先由 V3F 承担 USB 与网络控制面，V5F 工程未加入。本阶段这样做是为了把
USB NCM 枚举与 DVP/DMA 解耦；后续接入 MINI2 时，V5F 负责 DVP/RAW16，V3F
继续负责 USB/网络，并通过明确所有权的共享缓冲区交接帧数据。

不依赖 WCH 工具链的静态检查可运行：

```bash
bash tools/check_ncm_firmware.sh
```

项目挂载层当前不允许修改执行位，因此不要用 `./tools/check_ncm_firmware.sh`。

## 构建与验证状态

项目继承 CH32H417 EVT 的 MRS/GCC12 工程格式。2026-08-27 已使用 Windows 上的
MounRiver Studio 2.5.0 / WCH RISC-V Embedded GCC12 完成 V3F 构建，工程芯片项为
`CH32H417WEU`。当前联调硬件是 WCH 官方 CH32H417 主板及其 OV2640 DVP 模组，不是
T384 自研板；`docs/data/Petros_DVP/` 是该官方板已实测成功的 V3F + USBHS 基线。

当前可烧录产物：

- `V3F/obj/T384-NCM_V3F.hex`
- `V3F/obj/T384-NCM_V3F.bin`

目标工具链编译和静态检查已通过，但 2026-08-27 这版尚未重新烧录验证。仍需验证
Windows USB 枚举、NCM 驱动、DHCP、HTTP、持续吞吐和断连恢复；不能把构建通过写成
上板通过。

## USB 身份红线

`Common/App/t384_product_config.h` 暂时沿用 WCH 参考工程的
`VID=0x1A86, PID=0xFE1C`，只用于本地开发。该 PID 曾对应 UVC，Windows 可能
保留驱动缓存。发布前必须由产品侧分配不冲突的 Sipeed VID/PID，并在 Windows、
Linux、macOS、Android 和 iPhone 上重新验证；不要直接发布当前标识。

## 第三方来源

- WCH CH32H417 EVT 外设库：来自本仓库
  `tests/ch32h417_t384_base/Vendor/WCH/CH32H417/SRC`，仅用于 WCH MCU。
- TinyUSB 0.18.0：来自本机 `pico_tn160` 的 Pico SDK 2.2.0 副本，MIT；NCM
  类包含该项目已经验证过的 iOS 26/PR #3630 回移补丁。
- lwIP 2.2.0 development：来自本机 ESP-IDF 提交
  `ea1c174c1cbb7348bd8ba0ff1eb306246938dd80`，BSD；ESP 扩展在
  `lwipopts.h` 中关闭。
- TinyUSB `dhserver`：MIT。

对应许可证保留在 `ThirdParty/TinyUSB/LICENSE` 和 `ThirdParty/lwIP/COPYING`。
