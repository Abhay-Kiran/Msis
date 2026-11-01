/*
 * fm_blackbox.h -- Non-volatile storage: the "black box". Owner: Arfan.
 *
 * Answers the examiners' question "where and how are faults stored?":
 *
 *   Flash sector 7 (0x08060000, 128 KB) -- fault log. Append-only: each
 *     fault_report_t (164 bytes) goes into the next free 256-byte slot,
 *     512 slots in all. A slot is free while its first word still reads
 *     0xFFFFFFFF (erased flash). Every report carries a CRC-32, so a
 *     slot half-written during a power cut is recognised and skipped.
 *     When the sector is full it is erased and the log starts over.
 *   Flash sector 6 (0x08040000, 128 KB) -- the learned baseline, with
 *     magic, version, the core clock it was learned at, and a CRC-32.
 *
 * Both survive reset AND power-off. The linker script stops the firmware
 * at 256 KB, so a re-flash from CubeIDE (which erases only the sectors
 * it programs) keeps both -- use 'C' / 'L' to clear them on purpose.
 *
 * Flash programming stalls the CPU (no code can run from flash while a
 * sector erases: ~1-2 s). So erases only happen at moments the monitor
 * chooses (learn commit, clear, log full) and the monitor ignores the
 * windows around them.
 */
#ifndef FM_BLACKBOX_H
#define FM_BLACKBOX_H

#include <stdint.h>
#include <stdbool.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM_LOG_SLOTS   (FM_FLASH_SECTOR_SIZE / FM_LOG_SLOT_SIZE)   /* 512 */

void     fm_bb_init(void);                 /* scan the log once at boot  */
uint32_t fm_bb_count(void);                /* valid reports stored       */
uint32_t fm_bb_next_seq(void);
/* Fills seq + crc32 and appends. Erases the sector first if it is full
 * (returns true in *erased so the caller can grant a grace period). */
bool     fm_bb_append(fault_report_t *r, bool *erased);
/* i = 0 is the oldest. Returns false for a free or corrupt slot. */
bool     fm_bb_read(uint32_t i, fault_report_t *out);
bool     fm_bb_clear(void);

bool     fm_bb_load_baseline(baseline_t *out, uint32_t core_hz);
bool     fm_bb_save_baseline(baseline_t *bl);
bool     fm_bb_erase_baseline(void);

#ifdef __cplusplus
}
#endif

#endif /* FM_BLACKBOX_H */
