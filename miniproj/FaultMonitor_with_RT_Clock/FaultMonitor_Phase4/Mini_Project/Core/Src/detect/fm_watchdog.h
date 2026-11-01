/*
 * fm_watchdog.h -- Independent watchdog (IWDG) supervision. Owner: Arfan.
 *
 * The IWDG is a hardware down-counter clocked by the separate 32 kHz LSI
 * oscillator. If software does not reload ("feed") it within ~4 s, it
 * resets the MCU -- even if the CPU is stuck with interrupts disabled,
 * which nothing written in software can catch.
 *
 * Who feeds it: a small task at the HIGHEST priority, but only while the
 * monitor task keeps checking in (once per window). So the watchdog
 * really answers "is the fault monitor still alive?". If the monitor
 * cannot run (interrupts off, an ISR that never returns, a corrupted
 * scheduler) the checks stop, the feeding stops, and the IWDG resets
 * the board. After the reboot the monitor sees the IWDG reset flag and
 * files a WATCHDOG report blaming the component that was running.
 *
 * Register-level (RM0390 chapter 20): no HAL IWDG driver in this project.
 */
#ifndef FM_WATCHDOG_H
#define FM_WATCHDOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM_WDG_TIMEOUT_MS   4000U   /* IWDG period                        */
#define FM_WDG_FEED_MS       250U   /* feeder task period                 */
#define FM_WDG_STALE_MS     1500U   /* monitor must have checked in since */

/* Starts the IWDG and the feeder task (registered as comp_id). */
void fm_watchdog_init(uint8_t comp_id);
/* Monitor calls this once per window. */
void fm_watchdog_checkin(void);

#ifdef __cplusplus
}
#endif

#endif /* FM_WATCHDOG_H */
