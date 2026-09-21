#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "t384_calibration_storage.h"

static uint8_t flash_image[0x60000];
static int flash_failure;
static unsigned flash_write_count;

int t384_cal_flash_read(uint32_t address, void *dst, size_t length)
{
    if (flash_failure==-3) return -1;
    if (!dst || address + length > sizeof(flash_image)) return -1;
    memcpy(dst, flash_image + address, length);
    return 0;
}

int t384_cal_flash_erase(uint32_t address, size_t length)
{
    if (flash_failure==-2) return -1;
    if (address + length > sizeof(flash_image)) return -1;
    memset(flash_image + address, 0xff, length);
    return 0;
}

int t384_cal_flash_write(uint32_t address, const void *src, size_t length)
{
    if (!src || address + length > sizeof(flash_image)) return -1;
    if (flash_failure>0 && ++flash_write_count==(unsigned)flash_failure) return -1;
    for (size_t i=0; i<length; ++i) flash_image[address+i] &= ((const uint8_t *)src)[i];
    if (flash_failure==-1) flash_image[address] ^= 1u;
    return 0;
}

static void seal_manifest(t384_cal_manifest_t *m,const void *payload)
{
    m->payload_crc32=t384_cal_crc32(payload,m->payload_len);
    m->header_crc32=0u;
    m->header_crc32=t384_cal_crc32(m,sizeof(*m));
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
    seal_manifest(&manifest,payload);
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    assert(t384_cal_storage_write(0, payload, sizeof(payload)) == T384_CAL_OK);
    assert(t384_cal_storage_finish() == T384_CAL_OK);

    t384_cal_manifest_t active;
    assert(t384_cal_storage_manifest(&active) == T384_CAL_OK);
    assert(active.generation == 1 && active.payload_len == sizeof(payload));
    uint8_t roundtrip[sizeof(payload)] = {0};
    assert(t384_cal_storage_read_data(0, roundtrip, sizeof(roundtrip)) == T384_CAL_OK);
    assert(memcmp(roundtrip, payload, sizeof(payload)) == 0);

    /* Corrupt or unterminated upload headers never read outside their fields. */
    t384_cal_manifest_t bad=manifest;
    memset(bad.model,'a',sizeof(bad.model));
    assert(t384_cal_storage_begin(&bad)==T384_CAL_FORMAT);
    bad=manifest; bad.header_crc32 ^= 1u;
    assert(t384_cal_storage_begin(&bad)==T384_CAL_CRC);
    bad=manifest; bad.magic=0u;
    assert(t384_cal_storage_begin(&bad)==T384_CAL_FORMAT);

    /* Coverage is per byte, so overlapping writes cannot hide missing data. */
    assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
    assert(t384_cal_storage_write(0u,payload,8u)==T384_CAL_OK);
    assert(t384_cal_storage_write(0u,payload,8u)==T384_CAL_OK);
    assert(t384_cal_storage_finish()==T384_CAL_RANGE);
    uint8_t corrupt[16]; memcpy(corrupt,payload,sizeof(corrupt)); corrupt[15]^=1u;
    assert(t384_cal_storage_write(8u,corrupt+8u,8u)==T384_CAL_OK);
    assert(t384_cal_storage_finish()==T384_CAL_CRC);
    assert(t384_cal_storage_write(8u,payload+8u,8u)==T384_CAL_OK);
    t384_cal_storage_abort();

    /* Erase/program failures and silent corruption preserve the old slot on
     * both runtime reads and reboot. Magic is written only after readback. */
    for (int failure=-3; failure<=3; ++failure) {
        if (failure==0) continue;
        flash_failure=failure; flash_write_count=0u;
        assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
        assert(t384_cal_storage_write(0u,payload,sizeof(payload))==T384_CAL_OK);
        assert(t384_cal_storage_finish()==T384_CAL_FLASH);
        assert(t384_cal_storage_manifest(&active)==T384_CAL_OK && active.generation==1u);
        flash_failure=0;
        assert(t384_cal_storage_init()==T384_CAL_OK);
        assert(t384_cal_storage_manifest(&active)==T384_CAL_OK);
        assert(active.generation==1u);
    }

    /* Non-word payload lengths are padded in Flash, never in the CRC. */
    manifest.payload_len=15u;
    seal_manifest(&manifest,payload);
    assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
    assert(t384_cal_storage_write(8u,payload+8u,7u)==T384_CAL_OK);
    assert(t384_cal_storage_write(0u,payload,8u)==T384_CAL_OK);
    assert(t384_cal_storage_finish()==T384_CAL_OK);
    assert(t384_cal_storage_init()==T384_CAL_OK);
    assert(t384_cal_storage_manifest(&active)==T384_CAL_OK);
    assert(active.generation==2u && active.payload_len==15u);

    t384_cal_storage_abort();
    assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
    t384_cal_storage_abort();
    /* A valid generation-0 slot must supersede UINT32_MAX, and next commit
     * must erase the other physical slot regardless of generation parity. */
    t384_cal_manifest_t previous=active;
    previous.generation=UINT32_MAX; previous.header_crc32=0u;
    previous.header_crc32=t384_cal_crc32(&previous,sizeof(previous));
    active.generation=0u; active.header_crc32=0u;
    active.header_crc32=t384_cal_crc32(&active,sizeof(active));
    memset(flash_image,0xff,sizeof(flash_image));
    memcpy(flash_image+T384_CAL_STORAGE_SLOT0_ADDR,&active,sizeof(active));
    memcpy(flash_image+T384_CAL_STORAGE_SLOT0_ADDR+sizeof(active),payload,15u);
    memcpy(flash_image+T384_CAL_STORAGE_SLOT1_ADDR,&previous,sizeof(previous));
    memcpy(flash_image+T384_CAL_STORAGE_SLOT1_ADDR+sizeof(previous),payload,15u);
    assert(t384_cal_storage_init()==T384_CAL_OK);
    assert(t384_cal_storage_manifest(&active)==T384_CAL_OK && active.generation==0u);
    assert(t384_cal_storage_begin(&manifest)==T384_CAL_OK);
    assert(t384_cal_storage_write(0u,payload,15u)==T384_CAL_OK);
    assert(t384_cal_storage_finish()==T384_CAL_OK);
    assert(memcmp(flash_image+T384_CAL_STORAGE_SLOT0_ADDR,&active,sizeof(active))==0);
    return 0;
}
