#include "t384_calibration_storage.h"
#include <string.h>

#if defined(Core_V3F) && !defined(T384_HOST_SYNTAX_CHECK)
#define T384_CAL_USE_WCH_FLASH 1
#include "ch32h417_flash.h"
#endif

typedef char t384_cal_slot_size_check[(sizeof(t384_cal_manifest_t) < T384_CAL_STORAGE_SLOT_SIZE) ? 1 : -1];

#define SLOT_COUNT 2u
static t384_cal_manifest_t g_active;
static uint8_t g_payload[T384_CAL_STORAGE_MAX_PAYLOAD];
static t384_cal_manifest_t g_staging;
static uint8_t g_staging_payload[T384_CAL_STORAGE_MAX_PAYLOAD];
static uint8_t g_staging_valid;

__attribute__((weak)) int t384_cal_flash_read(uint32_t a, void *d, size_t n)
{
#if !defined(T384_CAL_USE_WCH_FLASH)
    (void)a; (void)d; (void)n; return -1;
#else
    if (!d || n == 0u) return -1;
    memcpy(d, (const void *)(uintptr_t)a, n);
    return 0;
#endif
}
__attribute__((weak)) int t384_cal_flash_erase(uint32_t a, size_t n)
{
#if !defined(T384_CAL_USE_WCH_FLASH)
    (void)a; (void)n; return -1;
#else
    if (n == 0u || (a % 0x1000u) != 0u || (n % 0x1000u) != 0u) return -1;
    FLASH_Unlock();
    for (size_t off = 0u; off < n; off += 0x1000u) {
        if (FLASH_ErasePage(a + (uint32_t)off) != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
    }
    FLASH_Lock();
    return 0;
#endif
}
__attribute__((weak)) int t384_cal_flash_write(uint32_t a, const void *s, size_t n)
{
#if !defined(T384_CAL_USE_WCH_FLASH)
    (void)a; (void)s; (void)n; return -1;
#else
    if (!s || n == 0u || (n % 4u) != 0u) return -1;
    FLASH_Unlock();
    for (size_t off = 0u; off < n; off += 4u) {
        uint32_t word = 0xFFFFFFFFu;
        memcpy(&word, (const uint8_t *)s + off, n - off >= 4u ? 4u : n - off);
        if (FLASH_ProgramWord(a + (uint32_t)off, word) != FLASH_COMPLETE) {
            FLASH_Lock();
            return -1;
        }
    }
    FLASH_Lock();
    return 0;
#endif
}

uint32_t t384_cal_crc32(const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data; uint32_t crc = 0xFFFFFFFFu;
    while (length--) { crc ^= *p++; for (uint8_t i=0;i<8;i++) crc = (crc>>1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc&1u)); }
    return ~crc;
}

static uint32_t header_crc(const t384_cal_manifest_t *m)
{ t384_cal_manifest_t t = *m; t.header_crc32 = 0; return t384_cal_crc32(&t, sizeof(t)); }
static int identity_present(const uint8_t *id)
{ for (unsigned i=0;i<T384_CAL_STORAGE_ID_MAX;i++) if (id[i] != 0u && id[i] != 0xFFu) return 1; return 0; }

static int valid_manifest(const t384_cal_manifest_t *m)
{
    if (!m || m->magic != T384_CAL_STORAGE_MAGIC || m->schema != T384_CAL_STORAGE_SCHEMA ||
        m->payload_len == 0 || m->payload_len > T384_CAL_STORAGE_MAX_PAYLOAD ||
        strcmp(m->model, T384_CAL_MODEL_EMPIRICAL_2POINT) != 0 ||
        (strcmp(m->profile, "256x192") != 0 && strcmp(m->profile, "384x288") != 0 &&
         strcmp(m->profile, "640x512") != 0) || m->calibration_id == 0 ||
        (m->gain != 1u && m->gain != 2u) || !identity_present(m->identity) ||
        m->header_crc32 != header_crc(m)) return 0;
    return 1;
}

static uint32_t slot_addr(unsigned i) { return i ? T384_CAL_STORAGE_SLOT1_ADDR : T384_CAL_STORAGE_SLOT0_ADDR; }

t384_cal_status_t t384_cal_storage_init(void)
{
    if (g_staging_valid) return T384_CAL_BUSY;
    /* Boot-time scratch reuses the idle staging buffer instead of a 2 KiB
     * local array, which alone would exhaust the V3F stack budget. */
    t384_cal_manifest_t best, m; int found=0;
    memset(&best,0,sizeof(best));
    for (unsigned i=0;i<SLOT_COUNT;i++) {
        if (t384_cal_flash_read(slot_addr(i), &m, sizeof(m)) != 0 || !valid_manifest(&m)) continue;
        if (t384_cal_flash_read(slot_addr(i)+sizeof(m), g_staging_payload, m.payload_len) != 0 ||
            t384_cal_crc32(g_staging_payload,m.payload_len) != m.payload_crc32) continue;
        if (!found || m.generation > best.generation) { best=m; memcpy(g_payload,g_staging_payload,m.payload_len); found=1; }
    }
    if (!found) { memset(&g_active,0,sizeof(g_active)); return T384_CAL_NO_VALID; }
    g_active=best; return T384_CAL_OK;
}

t384_cal_status_t t384_cal_storage_begin(const t384_cal_manifest_t *m)
{
    if (g_staging_valid) return T384_CAL_BUSY;
    if (!m || m->payload_len == 0 || m->payload_len > T384_CAL_STORAGE_MAX_PAYLOAD ||
        m->calibration_id == 0 || strcmp(m->model, T384_CAL_MODEL_EMPIRICAL_2POINT) != 0 ||
        (strcmp(m->profile, "256x192") != 0 && strcmp(m->profile, "384x288") != 0 &&
         strcmp(m->profile, "640x512") != 0) || (m->gain != 1u && m->gain != 2u) ||
        !identity_present(m->identity)) return T384_CAL_FORMAT;
    g_staging=*m; g_staging.generation=g_active.generation+1u; g_staging_valid=1; return T384_CAL_OK;
}
t384_cal_status_t t384_cal_storage_write(uint32_t off,const void *d,uint32_t n)
{
    if (!g_staging_valid) return T384_CAL_BUSY;
    if (!d || off > g_staging.payload_len || n > g_staging.payload_len-off) return T384_CAL_RANGE;
    memcpy(g_staging_payload+off,d,n); return T384_CAL_OK;
}
t384_cal_status_t t384_cal_storage_finish(void)
{
    if (!g_staging_valid) return T384_CAL_BUSY;
    g_staging.payload_crc32=t384_cal_crc32(g_staging_payload,g_staging.payload_len); g_staging.header_crc32=header_crc(&g_staging);
    unsigned slot=(g_staging.generation&1u)?1u:0u; uint32_t a=slot_addr(slot);
    if (sizeof(g_staging)+g_staging.payload_len > T384_CAL_STORAGE_SLOT_SIZE || t384_cal_flash_erase(a,T384_CAL_STORAGE_SLOT_SIZE)!=0 ||
        t384_cal_flash_write(a,&g_staging,sizeof(g_staging))!=0 || t384_cal_flash_write(a+sizeof(g_staging),g_staging_payload,g_staging.payload_len)!=0) { g_staging_valid=0; return T384_CAL_FLASH; }
    g_active=g_staging; memcpy(g_payload,g_staging_payload,g_staging.payload_len); g_staging_valid=0; return T384_CAL_OK;
}
void t384_cal_storage_abort(void){g_staging_valid=0;}
t384_cal_status_t t384_cal_storage_manifest(t384_cal_manifest_t *m){if(!m)return T384_CAL_INVALID_ARGUMENT; if(!valid_manifest(&g_active))return T384_CAL_NO_VALID;*m=g_active;return T384_CAL_OK;}
t384_cal_status_t t384_cal_storage_read_data(uint32_t o,void *d,uint32_t n){if(!d)return T384_CAL_INVALID_ARGUMENT;if(!valid_manifest(&g_active))return T384_CAL_NO_VALID;if(o>g_active.payload_len||n>g_active.payload_len-o)return T384_CAL_RANGE;memcpy(d,g_payload+o,n);return T384_CAL_OK;}
const char *t384_cal_status_name(t384_cal_status_t s){static const char*n[]={"ok","invalid-argument","busy","range","format","crc","identity","no-valid","flash"};return s<(sizeof(n)/sizeof(n[0]))?n[s]:"unknown";}
