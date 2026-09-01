#include <stdint.h>
#include <string.h>

#include "t384_product_config.h"
#include "tusb.h"

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_INTERFACE,
    STRID_MAC,
};

enum {
    ITF_NUM_NCM = 0,
    ITF_NUM_NCM_DATA,
    ITF_NUM_TOTAL,
};

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0201,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = T384_USB_VID,
    .idProduct = T384_USB_PID,
    .bcdDevice = T384_USB_BCD_DEVICE,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device_descriptor;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN)
#define EPNUM_NET_NOTIF 0x81
#define EPNUM_NET_OUT   0x02
#define EPNUM_NET_IN    0x82

/* CDC-NCM 1.0 table 5-2: Ethernet filter and NTB input-size requests. */
#define T384_NCM_CAPABILITIES \
    (NCM_NETWORK_CAPS_ETH_FILTER | NCM_NETWORK_CAPS_NTB_INPUT_SIZE)

#define T384_CDC_NCM_DESCRIPTOR(_itfnum, _desc_stridx, _mac_stridx, \
                                _ep_notif, _ep_notif_size, _ep_notif_interval, \
                                _epout, \
                                _epin, _epsize, _maxsegmentsize) \
    8, TUSB_DESC_INTERFACE_ASSOCIATION, _itfnum, 2, TUSB_CLASS_CDC, \
        CDC_COMM_SUBCLASS_NETWORK_CONTROL_MODEL, 0, 0, \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 1, TUSB_CLASS_CDC, \
        CDC_COMM_SUBCLASS_NETWORK_CONTROL_MODEL, 0, _desc_stridx, \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0110), \
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, _itfnum, \
        (uint8_t)((_itfnum) + 1), \
    13, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ETHERNET_NETWORKING, \
        _mac_stridx, 0, 0, 0, 0, U16_TO_U8S_LE(_maxsegmentsize), \
        U16_TO_U8S_LE(0), 0, \
    6, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_NCM, U16_TO_U8S_LE(0x0100), \
        T384_NCM_CAPABILITIES, \
    7, TUSB_DESC_ENDPOINT, _ep_notif, TUSB_XFER_INTERRUPT, \
        U16_TO_U8S_LE(_ep_notif_size), _ep_notif_interval, \
    9, TUSB_DESC_INTERFACE, (uint8_t)((_itfnum) + 1), 0, 0, \
        TUSB_CLASS_CDC_DATA, 0, NCM_DATA_PROTOCOL_NETWORK_TRANSFER_BLOCK, 0, \
    9, TUSB_DESC_INTERFACE, (uint8_t)((_itfnum) + 1), 1, 2, \
        TUSB_CLASS_CDC_DATA, 0, NCM_DATA_PROTOCOL_NETWORK_TRANSFER_BLOCK, 0, \
    7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0, \
    7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0

static const uint8_t fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    T384_CDC_NCM_DESCRIPTOR(ITF_NUM_NCM, STRID_INTERFACE, STRID_MAC,
                            EPNUM_NET_NOTIF, 64, 50, EPNUM_NET_OUT, EPNUM_NET_IN,
                            64, CFG_TUD_NET_MTU),
};

static const uint8_t hs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    T384_CDC_NCM_DESCRIPTOR(ITF_NUM_NCM, STRID_INTERFACE, STRID_MAC,
                            EPNUM_NET_NOTIF, 64, 9, EPNUM_NET_OUT, EPNUM_NET_IN,
                            512, CFG_TUD_NET_MTU),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    if (index != 0u) {
        return NULL;
    }
    return tud_speed_get() == TUSB_SPEED_HIGH ? hs_configuration : fs_configuration;
}

static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0201,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
    .bReserved = 0,
};

const uint8_t *tud_descriptor_device_qualifier_cb(void)
{
    return (const uint8_t *)&device_qualifier;
}

static uint8_t other_speed_configuration[CONFIG_TOTAL_LEN];

const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    if (index != 0u) {
        return NULL;
    }
    memcpy(other_speed_configuration,
           tud_speed_get() == TUSB_SPEED_HIGH ? fs_configuration : hs_configuration,
           sizeof(other_speed_configuration));
    other_speed_configuration[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return other_speed_configuration;
}

#define MS_OS_20_DESC_LEN 0xB2
#define BOS_TOTAL_LEN (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t bos_descriptor[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, 1),
};

const uint8_t *tud_descriptor_bos_cb(void)
{
    return bos_descriptor;
}

static const uint8_t ms_os_20_descriptor[] = {
    /* Microsoft compatible ID below is WINNCM. */
    U16_TO_U8S_LE(0x000A),
    U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    U16_TO_U8S_LE(0x0008),
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_NUM_NCM, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    U16_TO_U8S_LE(0x0014),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'N', 'C', 'M', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007),
    U16_TO_U8S_LE(0x002A),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0,
    't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0,
    'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),
    '{', 0, '5', 0, '7', 0, 'C', 0, '6', 0, 'C', 0, '8', 0, 'B', 0,
    '6', 0, '-', 0, 'B', 0, 'D', 0, '2', 0, 'D', 0, '-', 0, '4', 0,
    'E', 0, '6', 0, 'E', 0, '-', 0, '9', 0, '6', 0, '9', 0, 'C', 0,
    '-', 0, '0', 0, '1', 0, 'C', 0, 'B', 0, '0', 0, '2', 0, 'F', 0,
    'A', 0, '5', 0, '1', 0, '7', 0, '1', 0, '}', 0, 0, 0, 0, 0,
};

TU_VERIFY_STATIC(sizeof(ms_os_20_descriptor) == MS_OS_20_DESC_LEN,
                 "invalid Microsoft OS 2.0 descriptor length");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == 1u && request->wIndex == 7u) {
        return tud_control_xfer(rhport, request,
                                (void *)(uintptr_t)ms_os_20_descriptor,
                                sizeof(ms_os_20_descriptor));
    }
    return false;
}

extern uint8_t tud_network_mac_address[6];
extern char t384_usb_serial[13];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t descriptor[33];
    static const char *const strings[] = {
        NULL,
        "Sipeed Co. Ltd.",
        "Sipeed T384/T640 Thermal NCM",
        t384_usb_serial,
        "Sipeed Thermal USB Network",
    };
    unsigned count = 0u;

    if (index == STRID_LANGID) {
        descriptor[1] = 0x0409;
        count = 1u;
    } else if (index == STRID_MAC) {
        static const char hex[] = "0123456789ABCDEF";
        for (unsigned i = 0; i < sizeof(tud_network_mac_address); ++i) {
            descriptor[1 + count++] = hex[tud_network_mac_address[i] >> 4];
            descriptor[1 + count++] = hex[tud_network_mac_address[i] & 0x0fu];
        }
    } else {
        if (index >= TU_ARRAY_SIZE(strings) || strings[index] == NULL) {
            return NULL;
        }
        count = strlen(strings[index]);
        if (count > 32u) {
            count = 32u;
        }
        for (unsigned i = 0; i < count; ++i) {
            descriptor[1 + i] = (uint8_t)strings[index][i];
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (count * 2u + 2u));
    return descriptor;
}
