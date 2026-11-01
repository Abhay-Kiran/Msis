/*
 * app_periph.h -- the application's hardware, programmed at register level.
 *
 * Application: a real-time SENSOR MONITOR with an over-limit alarm.
 *
 *   TIM2 (1 kHz) --TRGO--> ADC1 (channel 0, pin PA0 = Arduino A0)
 *                              |
 *                              +--DMA2 Stream0--> 10-sample buffer in RAM
 *                                                   |
 *                              DMA "transfer complete" interrupt, every 10 ms
 *                                                   |
 *                                         wakes the sensor task
 *
 *   PA5 (LED LD2) is driven directly through the GPIO registers.
 *
 * No HAL is used for GPIO, TIM2, ADC or DMA: every register write is in
 * app_periph.c with a comment saying what it does (RM0390 chapters:
 * 6 RCC, 8 GPIO, 13 ADC, 9 DMA, 18 TIM2).
 *
 * Each peripheral also has
 *   - a re-init function: the framework's recovery action for it
 *   - a register-check table: the registers the framework checksums to
 *     notice a corrupted configuration ("peripheral status")
 */
#ifndef APP_PERIPH_H
#define APP_PERIPH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SAMPLE_HZ    1000U                          /* ADC trigger rate   */
#define APP_BLOCK_LEN    10U                            /* samples per block  */
#define APP_BLOCK_HZ     (APP_SAMPLE_HZ / APP_BLOCK_LEN) /* 100 blocks/s=10 ms */

/* Peripheral error flags (the "extra" argument of fm_periph_error) */
#define APP_PERR_ADC_OVR 0x0001U    /* ADC result overwritten before DMA read it */
#define APP_PERR_DMA_TE  0x0002U    /* DMA transfer error                         */
#define APP_PERR_TIMEOUT 0x0008U    /* no sample block within 5 block periods     */
#define APP_PERR_SENSOR  0x0010U    /* sensor stuck at a rail (open/short), 'Q 1' */

/* One block of raw ADC counts, copied out of the DMA buffer by the ISR. */
typedef struct {
    uint16_t s[APP_BLOCK_LEN];
    uint32_t seq;                   /* increments with every completed block */
} app_block_t;

void app_periph_init(void);         /* GPIO, ADC, DMA, TIM2: before the RTOS  */
void app_periph_register(void);     /* tell the framework about them          */

app_block_t app_adc_block(void);    /* latest complete block (safe copy)      */
void        app_led_set(bool on);   /* LED LD2 on PA5, via GPIOA->BSRR        */

/* recovery functions (fm_recover_fn_t signature) */
void app_adc_reinit(uint8_t comp_id);
void app_tim_reinit(uint8_t comp_id);
void app_gpio_reinit(uint8_t comp_id);
void app_tim6_reinit(uint8_t comp_id);

/* TIM6 1 kHz application tick (called from the TIM6 interrupt in main.c) */
void     app_tick_isr(void);
uint32_t app_ms(void);

/* ISR storm fault: TIM6 at 10 kHz with extra work in the ISR */
void app_tim6_storm(bool on);

/* The DMA ISR wakes this task when a block is ready */
void app_set_ctrl_task(void *task_handle);

#ifdef __cplusplus
}
#endif
#endif /* APP_PERIPH_H */
