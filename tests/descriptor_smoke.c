#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "t384_product_config.h"
#include "tusb.h"

uint8_t tud_network_mac_address[6] = {0x02, 0x53, 0x49, 0x50, 0x34, 0x80};
char t384_usb_serial[13] = "025349503480";

static tusb_speed_t test_speed = TUSB_SPEED_HIGH;
static const uint8_t *control_buffer;
static uint16_t control_length;

tusb_speed_t tud_speed_get(void)
{
    return test_speed;
}

bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request,
                      void *buffer, uint16_t length)
{
    (void)rhport;
    (void)request;
    control_buffer = (const uint8_t *)buffer;
    control_length = length;
    return true;
}

const uint8_t *tud_descriptor_device_cb(void);
const uint8_t *tud_descriptor_configuration_cb(uint8_t index);
const uint8_t *tud_descriptor_device_qualifier_cb(void);
const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index);
const uint8_t *tud_descriptor_bos_cb(void);
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid);
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request);

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void validate_configuration(const uint8_t *descriptor,
                                   uint8_t descriptor_type,
                                   uint16_t bulk_packet_size,
                                   uint8_t notification_interval)
{
    const uint16_t total_length = read_le16(descriptor + 2);
    assert(descriptor[0] == 9u);
    assert(descriptor[1] == descriptor_type);
    assert(total_length == (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN));

    unsigned interface_count = 0u;
    unsigned endpoint_count = 0u;
    unsigned ncm_functional_count = 0u;
    bool saw_notif = false;
    bool saw_bulk_in = false;
    bool saw_bulk_out = false;

    for (uint16_t offset = 0u; offset < total_length;) {
        const uint8_t length = descriptor[offset];
        const uint8_t type = descriptor[offset + 1u];
        assert(length >= 2u);
        assert((uint16_t)(offset + length) <= total_length);

        if (type == TUSB_DESC_INTERFACE) {
            ++interface_count;
        } else if (type == TUSB_DESC_ENDPOINT) {
            const uint8_t address = descriptor[offset + 2u];
            const uint8_t attributes = descriptor[offset + 3u];
            const uint16_t packet = read_le16(descriptor + offset + 4u);
            ++endpoint_count;
            if (address == 0x81u) {
                saw_notif = attributes == TUSB_XFER_INTERRUPT && packet == 64u &&
                            descriptor[offset + 6u] == notification_interval;
            } else if (address == 0x82u) {
                saw_bulk_in = attributes == TUSB_XFER_BULK && packet == bulk_packet_size;
            } else if (address == 0x02u) {
                saw_bulk_out = attributes == TUSB_XFER_BULK && packet == bulk_packet_size;
            }
        } else if (type == TUSB_DESC_CS_INTERFACE &&
                   descriptor[offset + 2u] == CDC_FUNC_DESC_NCM) {
            assert(length == 6u);
            assert(descriptor[offset + 5u] ==
                   (NCM_NETWORK_CAPS_ETH_FILTER | NCM_NETWORK_CAPS_NTB_INPUT_SIZE));
            ++ncm_functional_count;
        }
        offset = (uint16_t)(offset + length);
    }

    assert(interface_count == 3u);
    assert(endpoint_count == 3u);
    assert(ncm_functional_count == 1u);
    assert(saw_notif && saw_bulk_in && saw_bulk_out);
}

int main(void)
{
    const tusb_desc_device_t *device =
        (const tusb_desc_device_t *)tud_descriptor_device_cb();
    assert(device->bcdUSB == 0x0201u);
    assert(device->bDeviceClass == TUSB_CLASS_MISC);
    assert(device->bNumConfigurations == 1u);
    assert(device->idVendor == T384_USB_VID);
    assert(device->idProduct == T384_USB_PID);

    test_speed = TUSB_SPEED_HIGH;
    validate_configuration(tud_descriptor_configuration_cb(0u), TUSB_DESC_CONFIGURATION,
                           512u, 9u);
    validate_configuration(tud_descriptor_other_speed_configuration_cb(0u),
                           TUSB_DESC_OTHER_SPEED_CONFIG, 64u, 50u);

    test_speed = TUSB_SPEED_FULL;
    validate_configuration(tud_descriptor_configuration_cb(0u), TUSB_DESC_CONFIGURATION,
                           64u, 50u);
    validate_configuration(tud_descriptor_other_speed_configuration_cb(0u),
                           TUSB_DESC_OTHER_SPEED_CONFIG, 512u, 9u);

    const uint8_t *bos = tud_descriptor_bos_cb();
    assert(bos[1] == TUSB_DESC_BOS);
    assert(read_le16(bos + 2u) == 33u);

    const uint8_t *qualifier = tud_descriptor_device_qualifier_cb();
    assert(qualifier[1] == TUSB_DESC_DEVICE_QUALIFIER);

    const uint16_t *mac_string = tud_descriptor_string_cb(5u, 0x0409u);
    assert((mac_string[0] & 0xffu) == 26u);
    assert(mac_string[1] == '0' && mac_string[2] == '2');

    const uint16_t *serial_string = tud_descriptor_string_cb(3u, 0x0409u);
    assert((serial_string[0] & 0xffu) == 26u);
    assert(serial_string[1] == '0' && serial_string[2] == '2');

    tusb_control_request_t request = {0};
    request.bmRequestType_bit.type = TUSB_REQ_TYPE_VENDOR;
    request.bRequest = 1u;
    request.wIndex = 7u;
    assert(tud_vendor_control_xfer_cb(0u, CONTROL_STAGE_SETUP, &request));
    assert(control_length == 0xB2u);
    assert(read_le16(control_buffer) == 0x000Au);
    assert(memcmp(control_buffer + 30u, "WINNCM", 6u) == 0);
    return 0;
}
