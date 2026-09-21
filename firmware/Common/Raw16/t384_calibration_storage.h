#ifndef T384_CALIBRATION_STORAGE_H
#define T384_CALIBRATION_STORAGE_H

#include <stdint.h>
#include <stddef.h>

#define T384_CAL_STORAGE_MAGIC 0x54334331u /* T3C1 */
#define T384_CAL_STORAGE_SCHEMA 1u
#define T384_CAL_MODEL_EMPIRICAL_2POINT "t384-empirical-2point-v1"
#define T384_CAL_STORAGE_SLOT_SIZE 0x1000u
#define T384_CAL_STORAGE_SLOT0_ADDR 0x00050000u
#define T384_CAL_STORAGE_SLOT1_ADDR 0x00052000u
#define T384_CAL_STORAGE_MAX_PAYLOAD 2048u
#define T384_CAL_STORAGE_PROFILE_MAX 16u
#define T384_CAL_STORAGE_ID_MAX 32u

typedef enum {
    T384_CAL_OK = 0, T384_CAL_INVALID_ARGUMENT, T384_CAL_BUSY,
    T384_CAL_RANGE, T384_CAL_FORMAT, T384_CAL_CRC, T384_CAL_IDENTITY,
    T384_CAL_NO_VALID, T384_CAL_FLASH
} t384_cal_status_t;

typedef struct __attribute__((packed)) {
    uint32_t magic, schema, generation;
    uint32_t payload_len, payload_crc32, header_crc32;
    uint32_t calibration_id;
    char model[32];
    char profile[T384_CAL_STORAGE_PROFILE_MAX];
    uint8_t identity[T384_CAL_STORAGE_ID_MAX];
    uint8_t gain;
    uint8_t reserved[3];
} t384_cal_manifest_t;

typedef struct {
    uint16_t zero_c;
    uint16_t hot_c;
    uint32_t zero_raw;
    uint32_t hot_raw;
    int32_t counts_per_c_x1000;
} t384_cal_empirical_2point_t;

/* Platform may override these hooks with WCH FLASH erase/program routines. */
int t384_cal_flash_read(uint32_t address, void *dst, size_t length);
int t384_cal_flash_erase(uint32_t address, size_t length);
int t384_cal_flash_write(uint32_t address, const void *src, size_t length);

t384_cal_status_t t384_cal_storage_init(void);
/* Upload includes CRC32 of the supplied header (header_crc32 zeroed) and
 * payload. Generation is assigned locally after validation. Writes may arrive
 * in any order, but every byte must be covered before commit. Stored payloads
 * are quarantined data, not proof of a validated/applied temperature model. */
t384_cal_status_t t384_cal_storage_begin(const t384_cal_manifest_t *manifest);
t384_cal_status_t t384_cal_storage_write(uint32_t offset, const void *data,
                                         uint32_t length);
t384_cal_status_t t384_cal_storage_finish(void);
void t384_cal_storage_abort(void);
t384_cal_status_t t384_cal_storage_manifest(t384_cal_manifest_t *manifest);
t384_cal_status_t t384_cal_storage_read_data(uint32_t offset, void *data,
                                             uint32_t length);

uint32_t t384_cal_crc32(const void *data, size_t length);
const char *t384_cal_status_name(t384_cal_status_t status);

#endif
