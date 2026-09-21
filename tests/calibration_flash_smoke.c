/* Exercise the production WCH hooks, rather than overriding them with RAM
 * hooks. Model the vendor's 4/8 KiB erase granularity on mapped host memory. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "ch32h417_flash.h"
#include "t384_calibration_storage.h"

static unsigned erases;
static int unlocked;
void FLASH_Unlock(void) { unlocked = 1; }
void FLASH_Lock(void) { unlocked = 0; }
FLASH_Status FLASH_ErasePage(uint32_t address)
{
    assert(unlocked);
    assert(address >= FLASH_BASE + T384_CAL_STORAGE_SLOT0_ADDR &&
           address <= FLASH_BASE + T384_CAL_STORAGE_SLOT1_ADDR);
    const size_t page = (*(uint32_t *)(uintptr_t)FLASH_CFGR0_BASE & (1u << 28)) ? 8192u : 4096u;
    address &= ~(uint32_t)(page - 1u);
    memset((void *)(uintptr_t)address, 0xFF, page);
    ++erases;
    return FLASH_COMPLETE;
}
FLASH_Status FLASH_ProgramWord(uint32_t address, uint32_t word)
{
    assert(unlocked);
    assert(address >= FLASH_BASE + T384_CAL_STORAGE_SLOT0_ADDR &&
           address + sizeof(word) <= FLASH_BASE + T384_CAL_STORAGE_SLOT1_ADDR + T384_CAL_STORAGE_SLOT_SIZE);
    *(uint32_t *)(uintptr_t)address &= word;
    return FLASH_COMPLETE;
}

int main(void)
{
    void *slots = (void *)(uintptr_t)(FLASH_BASE + T384_CAL_STORAGE_SLOT0_ADDR);
    assert(mmap(slots, 0x4000u, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == slots);
    void *config = (void *)(uintptr_t)FLASH_CFGR0_BASE;
    assert(mmap(config, 4096u, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == config);
    for (unsigned dual = 0; dual <= 1u; ++dual) {
        *(uint32_t *)config = dual << 28;
        memset(slots, 0xFF, 0x4000u);
        assert(t384_cal_storage_init() == T384_CAL_NO_VALID);
        t384_cal_empirical_2point_t payload = {0, 50, 29670, 34400, 94600};
        t384_cal_manifest_t manifest = {0};
        manifest.magic = T384_CAL_STORAGE_MAGIC;
        manifest.schema = T384_CAL_STORAGE_SCHEMA;
        manifest.payload_len = sizeof(payload);
        manifest.payload_crc32 = t384_cal_crc32(&payload, sizeof(payload));
        manifest.calibration_id = 1u;
        strcpy(manifest.model, T384_CAL_MODEL_EMPIRICAL_2POINT);
        strcpy(manifest.profile, "384x288");
        memset(manifest.identity, 0x42, sizeof(manifest.identity));
        manifest.gain = 0xFFu;
        manifest.header_crc32 = t384_cal_crc32(&manifest, sizeof(manifest));
        for (unsigned generation = 1; generation <= 3u; ++generation) {
            const uint32_t old_address = FLASH_BASE + (generation == 2u ? T384_CAL_STORAGE_SLOT1_ADDR : T384_CAL_STORAGE_SLOT0_ADDR);
            uint8_t previous[T384_CAL_STORAGE_SLOT_SIZE];
            memcpy(previous, (void *)(uintptr_t)old_address, sizeof(previous));
            assert(t384_cal_storage_begin(&manifest) == T384_CAL_OK);
            assert(t384_cal_storage_write(0, &payload, sizeof(payload)) == T384_CAL_OK);
            assert(t384_cal_storage_finish() == T384_CAL_OK);
            assert(!unlocked);
            assert(memcmp(previous, (void *)(uintptr_t)old_address, sizeof(previous)) == 0);
            assert(t384_cal_storage_init() == T384_CAL_OK);
            t384_cal_manifest_t saved;
            assert(t384_cal_storage_manifest(&saved) == T384_CAL_OK);
            assert(saved.generation == generation);
            t384_cal_empirical_2point_t actual;
            assert(t384_cal_storage_read_data(0, &actual, sizeof(actual)) == T384_CAL_OK);
            assert(memcmp(&actual, &payload, sizeof(payload)) == 0);
        }
    }
    assert(erases == 6u);
    puts("production Flash hooks: single/dual-mode first commit, alternating slots and reboot passed");
    return 0;
}
