#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "t384_calibration_storage.h"

static uint8_t flash_image[0x30000];

int t384_cal_flash_read(uint32_t address, void *dst, size_t length)
{
    if (!dst || address + length > sizeof(flash_image)) return -1;
    memcpy(dst, flash_image + address, length);
    return 0;
}

int t384_cal_flash_erase(uint32_t address, size_t length)
{
    if (address + length > sizeof(flash_image)) return -1;
    memset(flash_image + address, 0xff, length);
    return 0;
}

int t384_cal_flash_write(uint32_t address, const void *src, size_t length)
{
    if (!src || address + length > sizeof(flash_image)) return -1;
    memcpy(flash_image + address, src, length);
    return 0;
}

int main(void)
{
    memset(flash_image, 0xff, sizeof(flash_image));
    assert(t384_cal_storage_init() == T384_CAL_NO_VALID);

    t384_cal_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.magic = T384_CAL_STORAGE_MAGIC;
    manifest.schema = T384_CAL_STORAGE_SCHEMA;
    manifest.payload_len = 16;
    manifest.calibration_id = 7;
    strcpy(manifest.model, T384_CAL_MODEL_EMPIRICAL_2POINT);
    strcpy(manifest.profile, "256x192");
    manifest.identity[0] = 0x42;
    manifest.gain = 1;
    const uint8_t payload[16] = {
        0, 0, 50, 0, 0x20, 0x96, 0, 0, 0x40, 0xAD, 0, 0,
        0x2C, 0xF8, 0, 0,
    };
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    assert(t384_cal_storage_write(0, payload, sizeof(payload)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);

    t384_cal_manifest_t active;
    assert(t384_cal_storage_manifest(&active) == T384_CAL_OK);
    assert(active.generation == 1 && active.payload_len == sizeof(payload));
    uint8_t roundtrip[sizeof(payload)] = {0};
    assert(t384_cal_storage_read_data(0, roundtrip, sizeof(roundtrip)) == T384_CAL_OK);
    assert(memcmp(roundtrip, payload, sizeof(payload)) == 0);

    t384_cal_storage_abort();
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    t384_cal_storage_abort();
    return 0;
}
