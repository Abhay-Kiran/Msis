/*
 * app_config.h -- the knobs of the application, in one place.
 * Owner: Abhishek.
 *
 * Changing a period, priority or stack size changes what "normal" looks
 * like, so after changing anything here re-learn the baseline ('L').
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---- UART -------------------------------------------------------------
 * Nucleo-F446RE routes USART2 (PA2 TX / PA3 RX) to the ST-LINK virtual
 * COM port: the same USB cable used to flash the board. 115200-8-N-1. */
#define FM_UART_HANDLE        huart2
#define FM_UART_BAUD           115200U
#define FM_UART_RX_QUEUE_LEN    96U     /* bytes, FreeRTOS queue depth   */
#define FM_UART_TX_BUF_LEN     256U     /* one line of formatted output  */
#define FM_UART_TX_TIMEOUT_MS  500U     /* give up on a stuck UART       */

/* ---- Task periods ---------------------------------------------------- */
/* The control task has no period of its own: the ADC (100 Hz, TIM2)
 * wakes it. See APP_ADC_HZ in app_periph.h. */
#define PERIOD_REPORT_MS       100U

/* ---- FreeRTOS priorities (CMSIS-RTOS2 names; higher = more urgent) ----
 *   wdg (framework)      Realtime
 *   monitor (framework)  High        must outrank every app task
 *   ctrl                 AboveNormal
 *   report               Normal
 *   shell, sigout        Low
 */
#define PRIO_CTRL              (osPriorityAboveNormal)
#define PRIO_REPORT            (osPriorityNormal)
#define PRIO_SHELL             (osPriorityLow)

/* ---- Stack sizes (words = 4 bytes) ------------------------------------ */
#define STACK_CTRL             384U
#define STACK_REPORT           512U
#define STACK_SHELL            512U

/* ---- Phase boundary --------------------------------------------------
 * Phase 4 build: expose the full implemented fault-injection catalogue.
 * Several tests are intentionally destructive (memory faults, watchdog
 * reset, ISR storm). Run them only on a bench board with a known-good
 * firmware image and after saving any reports you need.
 */
#ifndef FM_PHASE3_ONLY
#define FM_PHASE3_ONLY          0
#endif

/* ---- Build switch: see FM_ENABLE_MONITOR in Core/Inc/fm_types.h ------- */

#endif /* APP_CONFIG_H */
