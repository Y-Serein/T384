# Petros USBHS-only diagnostic

This directory is a source-only copy of the user-proven `docs/data/Petros_DVP`
MounRiver Studio project. Its project layout, startup code, linker scripts,
system clock code, V3F/V5F subprojects, and MRS metadata are retained.

Only the diagnostic application path differs:

- `Common/Common/hardware.c` skips OV2640 and DVP initialization and starts only
  the USBHS device controller.
- `Common/Common/ch32h417_usbhs_device.[ch]` and `usb_desc.[ch]` come from WCH
  CH32H417EVT `EXAM/USBHS/DEVICE/CH372Device` (driver V1.0.1, 2026-04-10).
- Existing UVC/DVP/USBSS files remain in the copied tree because the Petros MRS
  project structure is intentionally preserved, but the USBHS diagnostic driver
  does not reference them.

Open `Petros_DVP.wvsln` exactly as with the proven source project. Build and
flash only the V3F project for this test. Compilation, flashing, and hardware
validation are intentionally left to the user.

Expected USB identity: USB 2.0 High-Speed, VID:PID `1A86:5537`, product
`CH32H417`. Debug UART is the unchanged Petros configuration: USART8 TX on PA15
at 115200 baud. Successful host configuration prints
`USBHS enumeration complete`.
