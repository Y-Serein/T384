# T384 热成像插件

本工作区名为 `T384`，用于开发基于 CH32H417 的 T384/T640 热成像插件。目标链路是从 MINI2 384/640 机芯接收 DVP 原始数据，经 USB NCM 向手机或 PC 提供本地 Web 页面，由浏览器完成测温换算、伪彩显示和交互。

## 当前阶段

- MINI2 原厂表只读 UART/HTTP 代理已在当前 WN2256 验证件取得高低增益 NUC-T，错误事务可继续；KT/BT仍拒绝、距离后缀未确认。证据见[读取汇总](docs/data/read_report.txt)。
- 当前成像、RAW16传输和录像不依赖KT/BT，实验温度保留既有二点模型；正式OEM辐射测温仍需KT/BT及数据域/帧状态闭环。当前可运行版本和持续流验收见[运行说明](docs/runbooks/CURRENT_IMAGE_ONLY_OPERATION.md)。

- 正式 `firmware/` 已建立完整分块链路：DMA 等效源进入12×6144 B ring，再经
  `T384-FRAME-CHUNK-V1`、TCP、USB NCM到浏览器；默认请求 MINI2 TPD/Y16BE，
  控制或确认失败时回退 Picture/UYVY，且不发送保存参数命令。历史 Windows 60秒
  严格实测为 `27.750 FPS / 6.138 MB/s`，已超过25.5 FPS目标，但该成绩早于真实
  MINI2 TPD/Y16 接入，不能替代本次上板复验。
- 已验证阶段工程位于 `tests/ch32h417 t384 raw16 bench/`；NCM/OV2640保护基线位于
  `tests/ch32h417_t384_ncm/`。这些不再放回 `firmware/`。
- 目标 SKU 暂定为 T384 与 T640；现有设计明确暂不做 256×192 型号。
- 目标传输协议是 USB NCM，不是 UVC。仓库中的 CH32H417 `UVC-DVP` 仅是 DVP/USBSS 参考实现。
- 当前 NCM 控制面设备地址为 `192.168.18.1/24`，DHCP 地址池为 `192.168.18.2` 到 `192.168.18.4`；该地址段与另一项目的 `192.168.17.1` 区分使用。
- 原理图电源风险、真实 MINI2 DVP、V5F 数据面、正式测温精度和跨平台验证仍未完成。
- 当前测试 `VID/PID=0x1A86/0xE384` 只用于本地开发和驱动缓存隔离，禁止发布；正式
  USB 身份冻结后必须重新验证各主机的枚举和驱动缓存。

## 目标数据链路

```text
MINI2 384/640
  -> 8-bit DVP（每像素 16-bit 的具体打包/时序待确认）
  -> CH32H417 V5F + DMA/缓冲
  -> USB NCM + 本地 HTTP 服务
  -> iPhone / Android / PC 浏览器
  -> KT/BT/NUC-T/距离修正 + cmap + 测温交互
```

640×512×16-bit 原始帧的纯像素吞吐为：

- 25 fps：`16.384 MB/s = 15.625 MiB/s`
- 50 fps：`32.768 MB/s = 31.25 MiB/s`

这还不含协议、缓冲和重传开销，不能只凭 USB 标称速率判断可行性，必须在 CH32H417 真机上测持续吞吐、丢帧和缓存水位。

## 关键资料

1. [产品设计草案](docs/design/T384_640%20热成像插件.md)：SKU、定价、目标架构、校正表和吞吐设想。
2. `docs/design/图片和附件/MINI2 384 硬件接口资料-V1.0.xlsx`：50Pin 接口、电平、电源和信号定义。
3. `docs/design/图片和附件/MINI2系列串口指令集_V0.4_20231222.xlsx`：UART/I²C 命令、CRC、视频输出与电压切换。
4. `docs/data/EVT/EXAM/USBSS/DEVICE/UVC/UVC-DVP/`：CH32H417 DVP + USBSS/USBHS 示例，仅作移植参考。
5. `docs/data/EVT/EXAM/USBSS/DEVICE/CH372Device/`：CH32H417 USBSS 设备与测速参考。
6. `firmware/`：当前正式384×288 RAW16开发工程。
7. `tests/ch32h417 t384 raw16 bench/`：已通过27.750 FPS验证的阶段工程。
8. `tests/ch32h417_t384_ncm/`：已工作的NCM/OV2640保护基线。
9. `docs/data/SDK/Win_Linux/AC020_win&&linux_SDK_2.4.5/`：机芯命令、数据格式和测温算法参考。

以下附件不能当作当前主线：

- `SCH_XDV_SOC1_31023_2024-07-06.pdf` 是 AX620Q/XDV_SOC1 参考原理图，不是 CH32H417 目标板原理图。
- `web.zip` 是 iRay 256×192 Linux 本地原始流原型，依赖压缩包外的 helper 与 udev 文件；只可参考 UI、RAW 数据保护和温度交互。

## 已确认的接口边界

- 机芯主电源为 `5V ±10%`，资料提示启动探测器时约有 `500mA` 浪涌。
- MINI2 的 DVP/I²C 电源域默认上电为 `1.8V`，上电后可通过命令切换为 `3.3V`；UART 引脚资料标为 `3.3V`。原理图设计前必须逐信号确认，不能把所有 IO 当成同一电平。
- DVP 逻辑数据为 `DVP_DATA8..15` 对应 `DATA0..7`；帧同步、采样沿、字节序和 RAW16 打包方式尚未冻结。
- UART 参数为 `115200 8N1`，无流控。
- I²C 从地址为 `0x3C`（7-bit）；资料中的 `0x78/0x79` 是 8-bit 写/读地址。
- 测温换算当前文档基线是 `KT` 为 Q14、`BT` 按 `int16_t`、`NUC-T` 为开尔文温度乘 16，但表版本、增益切换和边界条件仍需用实际模组/SDK验证。

## 目录

- `docs/design/`：项目自有产品设计与附件。
- `docs/data/EVT/`：CH32H417 厂商 EVT、手册和示例，默认只读。
- `docs/data/SDK/`：热成像机芯厂商 SDK，默认只读。
- `firmware/`：唯一正式开发固件源码与MRS工程。
- `tests/`：主机测试和已验证阶段工程。
- `AGENTS.md`：本项目执行、修改和验证规则。
- `HANDOFF.md`：当前接手状态和下一步。

NCM 与 RAW16 静态检查分别为 `bash tools/check_ncm_firmware.sh` 和
`bash tools/check_raw16_bench.sh`。固件由用户在 Windows/MounRiver Studio 2.5.0
中只构建/烧录 V3F。正式入口为 `firmware/T384-RAW16-BENCH.wvsln`。

两套RAW16工程的 linked folders均为相对路径。`.mrs/`和`V3F/obj/`是本机缓存，
不作为可迁移资产；迁移后一次性重新生成即可，普通迭代不要求每次Clean。

NCM 与 RAW16 主工程已选择 MRS 的 `CH32H417WEU` 芯片项，对应目标原理图器件
`CH32H417WEU6`；该项已由 WCH Petros_DVP 官方板工程和本地 V1.6 数据手册确认。
主工程只构建/下载 V3F，并关闭 `Erase All` 与 `Clear CodeFlash`；
`ch32h417_t384_base` 的 QEU 元数据只属于厂商参考基线，禁止用于目标板下载。

## 开始工作

依次阅读：

1. `AGENTS.md`
2. `HANDOFF.md`
3. `docs/design/T384_640 热成像插件.md`

不要把主机静态检查、旧名称产物或 `docs/data/` 内任一厂商示例构建成功表述成
重命名后的 T384 固件已经完成 MRS 构建或上板验证。
