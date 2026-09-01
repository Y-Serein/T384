#ifndef T384_PRODUCT_CONFIG_H
#define T384_PRODUCT_CONFIG_H

/*
 * Development USB identity.
 *
 * This is an unallocated, local-only test identity under the WCH VID. The PID
 * intentionally differs from both the WCH UVC reference and the CH372/CH375
 * diagnostic so Windows cannot reuse either cached driver binding. Override
 * both macros with an allocated product identity before distributing firmware.
 */
#ifndef T384_USB_VID
#define T384_USB_VID 0x1A86u
#endif

#ifndef T384_USB_PID
#define T384_USB_PID 0xE384u
#endif

#define T384_USB_BCD_DEVICE 0x0100u

#define T384_NCM_IPV4_A 192u
#define T384_NCM_IPV4_B 168u
#define T384_NCM_IPV4_C 18u
#define T384_NCM_DEVICE_HOST 1u
#define T384_NCM_FIRST_CLIENT_HOST 2u
#define T384_NCM_CLIENT_COUNT 3u

#define T384_HTTP_PORT 80u

#endif
