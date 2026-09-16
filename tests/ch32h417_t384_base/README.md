# CH32H417 T384 编译基线

这是从 WCH `CH32H417EVT/EXAM/USBSS/DEVICE/UVC/UVC-DVP` 提取的自包含双核示例，用于先确认 MounRiver Studio 工程能够打开并编译。

## 打开与编译

在 MounRiver Studio 中打开：

`tests/ch32h417_t384_base/UVC-DVP.wvsln`

优先使用 `Build Solution`。如果需要分别构建，先构建 `UVC-DVP_V3F`，再构建 `UVC-DVP_V5F`；V5F 工程配置会合并 V3F 产物。

## 当前行为

- 工程保留厂商原始 UVC-DVP 行为，尚未改成 T384 产品固件。
- 默认 `Run_Core_V3FandV5F`：V3F 是主核并执行 `Hardware()`；V5F 当前只做启动握手。
- DVP 绑定 OV2640、SCCB、MJPEG/YUV；USB 绑定 UVC，不是 MINI2 RAW16，也不是 USB NCM。
- 只进行编译，不要把当前工程烧录到 T384 硬件。GPIO、电平、DVP 极性和 Type-C 硬件接口尚未冻结。

## 自包含依赖

- `Common/`：原 UVC、DVP、USBSS/USBHS 和链接脚本。
- `V3F/`、`V5F/`：双核 MounRiver Studio 工程。
- `Vendor/WCH/CH32H417/SRC/`：工程实际使用的 WCH Core、Peripheral、Startup 源码。

工程文件中的外部链接已改为以上本地相对路径，不再引用 `docs/data/`。

## 来源与限制

来源：`docs/data/CH32H417EVT/EXAM/USBSS/DEVICE/UVC/UVC-DVP/` 和 `docs/data/CH32H417EVT/EXAM/SRC/`。

WCH 文件头要求相关软件和二进制用于南京沁恒微电子生产的微控制器。本项目目标 MCU 是 CH32H417，后续移植仍需保留来源、许可证和修改记录。

当前未在本环境编译、未枚举 USB、未连接 MINI2、未上板验证。
