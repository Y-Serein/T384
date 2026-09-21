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
static uint8_t g_written[T384_CAL_STORAGE_MAX_PAYLOAD / 8u];
static uint32_t g_written_count;
static unsigned g_active_slot;

#if defined(T384_CAL_USE_WCH_FLASH)
static int flash_range_valid(uint32_t a, size_t n)
{
    const uint32_t end=T384_CAL_STORAGE_SLOT1_ADDR+T384_CAL_STORAGE_SLOT_SIZE;
    return n!=0u && a>=T384_CAL_STORAGE_SLOT0_ADDR && a<end && n<=end-a;
}
#endif

__attribute__((weak)) int t384_cal_flash_read(uint32_t a, void *d, size_t n)
{
#if !defined(T384_CAL_USE_WCH_FLASH)
    (void)a; (void)d; (void)n; return -1;
#else
    if (!d || !flash_range_valid(a,n)) return -1;
    /* Slot constants are image offsets. WCH's Flash routines and physical
     * readback use the Flash alias, as in EVT FLASH_Program/hardware.c. */
    memcpy(d, (const void *)(uintptr_t)(FLASH_BASE + a), n);
    return 0;
#endif
}
__attribute__((weak)) int t384_cal_flash_erase(uint32_t a, size_t n)
{
#if !defined(T384_CAL_USE_WCH_FLASH)
    (void)a; (void)n; return -1;
#else
    if (!flash_range_valid(a,n) || (a % 0x1000u) != 0u || (n % 0x1000u) != 0u) return -1;
    /* WCH FLASH_ErasePage masks addresses to 8 KiB in dual Flash mode
     * (FLASH_CFGR0 bit 28). The two slots only share an erase page if they
     * sit in the same 8 KiB block; writing one would then destroy the other,
     * so refuse only in that combined case. */
    if ((*(volatile uint32_t *)(uintptr_t)FLASH_CFGR0_BASE & (1u<<28)) != 0u &&
        (T384_CAL_STORAGE_SLOT0_ADDR & 0xFFFFE000u) ==
        (T384_CAL_STORAGE_SLOT1_ADDR & 0xFFFFE000u)) return -1;
    FLASH_Unlock();
    for (size_t off = 0u; off < n; off += 0x1000u) {
        if (FLASH_ErasePage(FLASH_BASE + a + (uint32_t)off) != FLASH_COMPLETE) {
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
    if (!s || !flash_range_valid(a,n) || (a % 4u) != 0u || (n % 4u) != 0u) return -1;
    FLASH_Unlock();
    for (size_t off = 0u; off < n; off += 4u) {
        uint32_t word = 0xFFFFFFFFu;
        memcpy(&word, (const uint8_t *)s + off, n - off >= 4u ? 4u : n - off);
        if (FLASH_ProgramWord(FLASH_BASE + a + (uint32_t)off, word) != FLASH_COMPLETE) {
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

static int manifest_fields_valid(const t384_cal_manifest_t *m)
{
    if (!m || m->magic != T384_CAL_STORAGE_MAGIC || m->schema != T384_CAL_STORAGE_SCHEMA ||
        m->payload_len == 0 || m->payload_len > T384_CAL_STORAGE_MAX_PAYLOAD ||
        !memchr(m->model, 0, sizeof(m->model)) ||
        !memchr(m->profile, 0, sizeof(m->profile)) ||
        strcmp(m->model, T384_CAL_MODEL_EMPIRICAL_2POINT) != 0 ||
        (strcmp(m->profile, "256x192") != 0 && strcmp(m->profile, "384x288") != 0 &&
         strcmp(m->profile, "640x512") != 0) || m->calibration_id == 0 ||
        (m->gain != 1u && m->gain != 2u && m->gain != 0xFFu) || !identity_present(m->identity)) return 0;
    return 1;
}
static int valid_manifest(const t384_cal_manifest_t *m)
{ return manifest_fields_valid(m) && m->header_crc32 == header_crc(m); }

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
        /* Adjacent generations remain ordered across uint32 wrap. */
        if (!found || (m.generation != best.generation &&
                       m.generation - best.generation < 0x80000000u)) {
            best=m; memcpy(g_payload,g_staging_payload,m.payload_len);
            g_active_slot=i; found=1;
        }
    }
    if (!found) { memset(&g_active,0,sizeof(g_active)); return T384_CAL_NO_VALID; }
    g_active=best; return T384_CAL_OK;
}

t384_cal_status_t t384_cal_storage_begin(const t384_cal_manifest_t *m)
{
    if (g_staging_valid) return T384_CAL_BUSY;
    if (!manifest_fields_valid(m)) return T384_CAL_FORMAT;
    if (m->header_crc32 != header_crc(m)) return T384_CAL_CRC;
    g_staging=*m; g_staging.generation=g_active.generation+1u;
    memset(g_written,0,sizeof(g_written));
    memset(g_staging_payload,0,sizeof(g_staging_payload));
    g_written_count=0u; g_staging_valid=1; return T384_CAL_OK;
}
t384_cal_status_t t384_cal_storage_write(uint32_t off,const void *d,uint32_t n)
{
    if (!g_staging_valid) return T384_CAL_BUSY;
    if (!d || off > g_staging.payload_len || n > g_staging.payload_len-off) return T384_CAL_RANGE;
    memcpy(g_staging_payload+off,d,n);
    for (uint32_t i=off; i<off+n; ++i) {
        const uint8_t mask=(uint8_t)(1u << (i & 7u));
        if (!(g_written[i >> 3] & mask)) {
            g_written[i >> 3] |= mask; ++g_written_count;
        }
    }
    return T384_CAL_OK;
}

/* Small readback chunks fit the 2 KiB V3F stack. Do not publish a slot until
 * both its payload and header have survived a physical Flash readback. */
static int flash_matches(uint32_t address, const void *data, size_t length)
{
    uint8_t scratch[64];
    const uint8_t *expected=(const uint8_t *)data;
    for (size_t off=0; off<length; off+=sizeof(scratch)) {
        size_t count=length-off;
        if (count>sizeof(scratch)) count=sizeof(scratch);
        if (t384_cal_flash_read(address+(uint32_t)off,scratch,count)!=0 ||
            memcmp(scratch,expected+off,count)!=0) return -1;
    }
    return 0;
}

t384_cal_status_t t384_cal_storage_finish(void)
{
    if (!g_staging_valid) return T384_CAL_BUSY;
    if (g_written_count != g_staging.payload_len) return T384_CAL_RANGE;
    if (t384_cal_crc32(g_staging_payload,g_staging.payload_len) !=
        g_staging.payload_crc32) return T384_CAL_CRC;
    g_staging.header_crc32=header_crc(&g_staging);
    const unsigned slot=valid_manifest(&g_active)?1u-g_active_slot:1u;
    const uint32_t a=slot_addr(slot);
    const size_t aligned_length=g_staging.payload_len & ~(size_t)3u;
    if (t384_cal_flash_erase(a,T384_CAL_STORAGE_SLOT_SIZE)!=0) goto failed;
    if (aligned_length && t384_cal_flash_write(a+sizeof(g_staging),
        g_staging_payload,aligned_length)!=0) goto failed;
    if (aligned_length != g_staging.payload_len) {
        uint32_t tail=0xFFFFFFFFu;
        memcpy(&tail,g_staging_payload+aligned_length,
               g_staging.payload_len-aligned_length);
        if (t384_cal_flash_write(a+sizeof(g_staging)+(uint32_t)aligned_length,
                                &tail,sizeof(tail))!=0) goto failed;
    }
    /* Magic is the last programmed word. Until then boot ignores this slot. */
    if (flash_matches(a+sizeof(g_staging),g_staging_payload,g_staging.payload_len)!=0 ||
        t384_cal_flash_write(a+4u,(const uint8_t *)&g_staging+4u,sizeof(g_staging)-4u)!=0 ||
        flash_matches(a+4u,(const uint8_t *)&g_staging+4u,sizeof(g_staging)-4u)!=0 ||
        t384_cal_flash_write(a,&g_staging.magic,4u)!=0 ||
        flash_matches(a,&g_staging.magic,4u)!=0) goto failed;
    g_active=g_staging; g_active_slot=slot;
    memcpy(g_payload,g_staging_payload,g_staging.payload_len);
    g_staging_valid=0; return T384_CAL_OK;
failed:
    g_staging_valid=0; return T384_CAL_FLASH;
}
void t384_cal_storage_abort(void){g_staging_valid=0;}
t384_cal_status_t t384_cal_storage_manifest(t384_cal_manifest_t *m){if(!m)return T384_CAL_INVALID_ARGUMENT; if(!valid_manifest(&g_active))return T384_CAL_NO_VALID;*m=g_active;return T384_CAL_OK;}
t384_cal_status_t t384_cal_storage_read_data(uint32_t o,void *d,uint32_t n){if(!d)return T384_CAL_INVALID_ARGUMENT;if(!valid_manifest(&g_active))return T384_CAL_NO_VALID;if(o>g_active.payload_len||n>g_active.payload_len-o)return T384_CAL_RANGE;memcpy(d,g_payload+o,n);return T384_CAL_OK;}
const char *t384_cal_status_name(t384_cal_status_t s){static const char*n[]={"ok","invalid-argument","busy","range","format","crc","identity","no-valid","flash"};return s<(sizeof(n)/sizeof(n[0]))?n[s]:"unknown";}
