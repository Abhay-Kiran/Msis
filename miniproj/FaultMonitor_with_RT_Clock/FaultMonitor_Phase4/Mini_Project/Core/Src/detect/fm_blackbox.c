/*
 * fm_blackbox.c -- see fm_blackbox.h. Owner: Arfan.
 *
 * Uses the HAL flash driver (stm32f4xx_hal_flash*.c): unlock, erase a
 * sector, program 32-bit words, lock. RM0390 chapter 3 explains the
 * flash interface; voltage range 3 (2.7-3.6 V, the Nucleo's 3.3 V) lets
 * us program a whole word at a time.
 */
#include "fm_blackbox.h"
#include "fm_trace.h"       /* fm_crc32 */

#include "main.h"
#include <stddef.h>
#include <string.h>

#if FM_ENABLE_MONITOR && FM_ENABLE_DETECT

static uint32_t s_next_slot;   /* first free slot                          */
static uint32_t s_count;       /* valid reports                            */
static uint32_t s_next_seq;    /* sequence number for the next report      */

static inline const fault_report_t *slot_ptr(uint32_t i)
{
    return (const fault_report_t *)(FM_FLASH_LOG_ADDR + i * FM_LOG_SLOT_SIZE);
}

static bool report_valid(const fault_report_t *r)
{
    return r->magic == FM_REPORT_MAGIC &&
           fm_crc32(0, r, offsetof(fault_report_t, crc32)) == r->crc32;
}

void fm_bb_init(void)
{
    s_next_slot = FM_LOG_SLOTS;
    s_count = 0;
    s_next_seq = 1;
    for (uint32_t i = 0; i < FM_LOG_SLOTS; i++) {
        const fault_report_t *r = slot_ptr(i);
        if (r->magic == 0xFFFFFFFFUL) {     /* erased = free, log ends here */
            s_next_slot = i;
            break;
        }
        if (report_valid(r)) {
            s_count++;
            if (r->seq >= s_next_seq) {
                s_next_seq = r->seq + 1U;
            }
        }
    }
}

uint32_t fm_bb_count(void)    { return s_count; }
uint32_t fm_bb_next_seq(void) { return s_next_seq; }

static bool erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef e = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Sector       = sector,
        .NbSectors    = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
    };
    uint32_t bad = 0;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &bad);
    HAL_FLASH_Lock();
    return st == HAL_OK;
}

static bool program_words(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    bool ok = true;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    for (uint32_t off = 0; off < len && ok; off += 4U) {
        uint32_t w = 0xFFFFFFFFUL;
        memcpy(&w, p + off, (len - off) >= 4U ? 4U : (len - off));
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + off, w) == HAL_OK;
    }
    HAL_FLASH_Lock();
    return ok;
}

bool fm_bb_append(fault_report_t *r, bool *erased)
{
    if (erased) {
        *erased = false;
    }
    if (s_next_slot >= FM_LOG_SLOTS) {
        if (!erase_sector(FM_FLASH_LOG_SECTOR)) {
            return false;
        }
        s_next_slot = 0;
        s_count = 0;
        if (erased) {
            *erased = true;
        }
    }
    r->magic = FM_REPORT_MAGIC;
    r->seq   = s_next_seq;
    r->crc32 = fm_crc32(0, r, offsetof(fault_report_t, crc32));
    bool ok = program_words(FM_FLASH_LOG_ADDR + s_next_slot * FM_LOG_SLOT_SIZE,
                            r, sizeof(*r));
    s_next_slot++;              /* even a failed slot is used up */
    if (ok) {
        s_count++;
        s_next_seq++;
    }
    return ok;
}

bool fm_bb_read(uint32_t i, fault_report_t *out)
{
    if (i >= s_next_slot) {
        return false;
    }
    const fault_report_t *r = slot_ptr(i);
    if (!report_valid(r)) {
        return false;
    }
    memcpy(out, r, sizeof(*out));
    return true;
}

bool fm_bb_clear(void)
{
    bool ok = erase_sector(FM_FLASH_LOG_SECTOR);
    s_next_slot = 0;
    s_count = 0;
    /* keep s_next_seq: sequence numbers never go backwards */
    return ok;
}

bool fm_bb_load_baseline(baseline_t *out, uint32_t core_hz)
{
    const baseline_t *f = (const baseline_t *)FM_FLASH_BASELINE_ADDR;
    if (f->magic != FM_BASELINE_MAGIC || f->version != FM_IFACE_VERSION ||
        f->core_hz != core_hz ||
        fm_crc32(0, f, offsetof(baseline_t, crc32)) != f->crc32) {
        return false;
    }
    memcpy(out, f, sizeof(*out));
    return true;
}

bool fm_bb_save_baseline(baseline_t *bl)
{
    bl->crc32 = fm_crc32(0, bl, offsetof(baseline_t, crc32));
    if (!erase_sector(FM_FLASH_BASELINE_SECTOR)) {
        return false;
    }
    return program_words(FM_FLASH_BASELINE_ADDR, bl, sizeof(*bl));
}

bool fm_bb_erase_baseline(void)
{
    return erase_sector(FM_FLASH_BASELINE_SECTOR);
}

#endif /* FM_ENABLE_MONITOR && FM_ENABLE_DETECT */
