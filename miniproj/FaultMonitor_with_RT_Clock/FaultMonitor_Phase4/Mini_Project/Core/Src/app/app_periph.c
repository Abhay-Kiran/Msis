/*
 * app_periph.c -- register-level GPIO, TIM2, ADC1 and DMA2 for the sensor
 * monitor. See app_periph.h for the big picture.
 *
 * The rule for this file: configuration registers are written ONLY in the
 * *_config() functions below (at boot, and again by the recovery code).
 * Nothing else may touch them, because the framework keeps a checksum of
 * these registers and treats any change as a corrupted peripheral.
 */
#include "app_config.h"
#include "app_periph.h"
#include "app_hooks.h"
#include "fm_common.h"
#include "fm_trace.h"

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

static volatile uint16_t      s_dma_buf[APP_BLOCK_LEN]; /* DMA writes here      */
static volatile app_block_t   s_block;                  /* ISR copies it here   */
static volatile TaskHandle_t  s_ctrl_task;

/* ======================================================================= */
/* GPIO                                                                     */
/* ======================================================================= */
/*
 * Every GPIO pin has two bits in MODER:  00 input, 01 output,
 * 10 alternate function, 11 analog. Pin n uses bits [2n+1 : 2n].
 */
static void gpio_config(void)
{
    /* 1. Peripherals are clock-gated after reset: switch on the GPIOA
     *    clock first, then read the register back so the write has
     *    really reached the bus before the next access. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    /* 2. PA0 = ADC1 channel 0. Analog mode (11) disconnects the digital
     *    input buffer, and no pull resistor (a pull-up would act as a
     *    divider against the signal source and shift every reading). */
    GPIOA->MODER |=  (3U << (0U * 2U));
    GPIOA->PUPDR &= ~(3U << (0U * 2U));

    /* 3. PA5 = LED LD2: general purpose output (01), push-pull. */
    GPIOA->MODER   = (GPIOA->MODER & ~(3U << (5U * 2U))) | (1U << (5U * 2U));
    GPIOA->OTYPER &= ~(1U << 5U);
}

/* BSRR: bits 0..15 set a pin, bits 16..31 reset it. One write, no
 * read-modify-write, so an interrupt can never corrupt it halfway. */
void app_led_set(bool on)
{
    GPIOA->BSRR = on ? (1U << 5U) : (1U << (5U + 16U));
}

/* ======================================================================= */
/* TIM2: the 1 kHz sampling clock                                           */
/* ======================================================================= */
/*
 * The timer counts PSC+1 input clocks per tick and rolls over after
 * ARR+1 ticks. Its "update event" is sent out as TRGO, and the ADC is
 * set to start a conversion on that signal. So the sampling rate comes
 * from hardware, not from software delays.
 *
 *   TIM2 clock = 84 MHz (APB1 timers run at 2 x 42 MHz)
 *   84 MHz / (83+1)  = 1 MHz     -> 1 tick = 1 us
 *   1 MHz  / (999+1) = 1 kHz     -> one update event per millisecond
 */
static void tim2_config(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;

    TIM2->CR1  = 0;                          /* stop while configuring      */
    TIM2->PSC  = 83U;
    TIM2->ARR  = (1000000U / APP_SAMPLE_HZ) - 1U;
    TIM2->CR2  = (2U << TIM_CR2_MMS_Pos);    /* MMS=010: TRGO = update event */
    TIM2->EGR  = TIM_EGR_UG;                 /* load PSC/ARR into the timer  */
    TIM2->CR1  = TIM_CR1_ARPE | TIM_CR1_CEN; /* auto-reload preload, start   */
}

/* ======================================================================= */
/* ADC1 + DMA2 Stream0                                                      */
/* ======================================================================= */
static void adc_dma_config(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    /* Quiet everything before changing it */
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    ADC1->CR2 = 0;                           /* ADON=0: ADC switched off     */
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;        /* ask the stream to stop ...   */
    while (DMA2_Stream0->CR & DMA_SxCR_EN) { /* ... and wait until it has    */
    }
    DMA2->LIFCR = 0x3DU;                     /* clear all stream-0 flags     */

    /* ---- DMA: ADC data register -> RAM buffer, no CPU involved ---------
     * DMA2 stream 0, request channel 0 = ADC1 (table in RM0390 DMA chapter).
     * 10 half-words, then wrap around (circular). An interrupt fires after
     * the 10th: that is our 10 ms block boundary. */
    DMA2_Stream0->PAR  = (uint32_t)&ADC1->DR;        /* source: ADC result   */
    DMA2_Stream0->M0AR = (uint32_t)s_dma_buf;        /* destination: RAM     */
    DMA2_Stream0->NDTR = APP_BLOCK_LEN;              /* items per block      */
    DMA2_Stream0->FCR  = 0;                          /* direct mode, no FIFO */
    DMA2_Stream0->CR   = (0U << DMA_SxCR_CHSEL_Pos)  /* channel 0 = ADC1     */
                       | (2U << DMA_SxCR_PL_Pos)     /* priority: high       */
                       | (1U << DMA_SxCR_MSIZE_Pos)  /* memory: 16 bit       */
                       | (1U << DMA_SxCR_PSIZE_Pos)  /* peripheral: 16 bit   */
                       | DMA_SxCR_MINC               /* step through buffer  */
                       | DMA_SxCR_CIRC               /* wrap after 10        */
                       | DMA_SxCR_TCIE               /* interrupt: block done*/
                       | DMA_SxCR_TEIE;              /* interrupt: DMA error */
    DMA2_Stream0->CR  |= DMA_SxCR_EN;                /* start listening      */

    /* ---- ADC ------------------------------------------------------------
     * ADC clock = PCLK2 / 4 = 84 / 4 = 21 MHz (the limit is 36 MHz).
     * CCR.ADCPRE: 00 = /2, 01 = /4, 10 = /6, 11 = /8. */
    ADC->CCR    = (1U << ADC_CCR_ADCPRE_Pos);

    ADC1->SR    = 0;                                 /* clear old flags      */
    ADC1->CR1   = 0;                                 /* 12-bit, no scan      */

    /* Sample time for channel 0 = 84 cycles (= 4 us at 21 MHz). A longer
     * sample time lets the input capacitor charge fully when the signal
     * source has a high impedance. SMPR2 holds channels 0..9. */
    ADC1->SMPR2 = (4U << ADC_SMPR2_SMP0_Pos);

    ADC1->SQR1  = 0;                                 /* L = 0: 1 conversion  */
    ADC1->SQR3  = (0U << ADC_SQR3_SQ1_Pos);          /* 1st (only) = IN0     */

    /* CR2: ADON  = switch the ADC on
     *      DMA   = send a DMA request after every conversion
     *      DDS   = keep sending requests (needed for circular DMA)
     *      EXTEN = 01: start on the RISING edge of the external trigger
     *      EXTSEL= 0110: the trigger is TIM2 TRGO                         */
    ADC1->CR2   = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS
                | (1U << ADC_CR2_EXTEN_Pos)
                | (6U << ADC_CR2_EXTSEL_Pos);

    /* Priority 6: numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY (5),
     * which is what lets this ISR call the FreeRTOS "FromISR" functions. */
    NVIC_SetPriority(DMA2_Stream0_IRQn, 6U);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

void app_periph_init(void)
{
    gpio_config();
    adc_dma_config();
    tim2_config();          /* last: the timer starts the whole chain */
}

/* ======================================================================= */
/* Register checks: the registers the framework checksums                   */
/* ======================================================================= */
/* { address, mask }: only the masked bits are compared. For GPIOA we only
 * look at our own pins (PA0, PA5): other pins belong to the UART and the
 * debugger. */
static const fm_reg_check_t s_adc_regs[] = {
    { &ADC1->CR1,   0xFFFFFFFFUL },
    { &ADC1->CR2,   ~(ADC_CR2_SWSTART | ADC_CR2_JSWSTART) },
    { &ADC1->SMPR2, 0xFFFFFFFFUL },
    { &ADC1->SQR1,  0xFFFFFFFFUL },
    { &ADC1->SQR3,  0xFFFFFFFFUL },
    { &ADC->CCR,    0xFFFFFFFFUL },
    { &DMA2_Stream0->CR,   ~DMA_SxCR_CT },
    { &DMA2_Stream0->PAR,  0xFFFFFFFFUL },
    { &DMA2_Stream0->M0AR, 0xFFFFFFFFUL },
};
static const fm_reg_check_t s_tim_regs[] = {
    { &TIM2->CR1, 0xFFFFFFFFUL },
    { &TIM2->CR2, 0xFFFFFFFFUL },
    { &TIM2->PSC, 0xFFFFFFFFUL },
    { &TIM2->ARR, 0xFFFFFFFFUL },
};
static const fm_reg_check_t s_gpio_regs[] = {
    { &GPIOA->MODER,  (3UL << 0) | (3UL << 10) },   /* PA0 and PA5 modes   */
    { &GPIOA->PUPDR,  (3UL << 0) },                 /* PA0: no pull        */
    { &GPIOA->OTYPER, (1UL << 5) },                 /* PA5: push-pull      */
};
#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

void app_periph_register(void)
{
    fm_register_periph(COMP_PER_ADC,  "ADC1",  s_adc_regs,  N(s_adc_regs),  app_adc_reinit,  false);
    fm_register_periph(COMP_PER_TIM,  "TIM2",  s_tim_regs,  N(s_tim_regs),  app_tim_reinit,  false);
    fm_register_periph(COMP_PER_GPIO, "GPIOA", s_gpio_regs, N(s_gpio_regs), app_gpio_reinit, false);
    fm_register_isr(COMP_ISR_ADC,    "adc_isr",  true,  app_adc_reinit);
    fm_register_isr(COMP_ISR_TIM6,   "tim6_isr", false, app_tim6_reinit);
    fm_register_isr(COMP_ISR_BUTTON, "btn_isr",  true,  NULL);
}

/* ======================================================================= */
/* Recovery: re-initialise one peripheral                                   */
/* ======================================================================= */
/* The ADC and TIM2 first pulse their reset line in RCC, so every register
 * (including ones we never write) is back at its reset value, then they
 * are configured exactly as at boot. GPIOA is NOT reset: it also carries
 * the UART and debugger pins, so only our own pin bits are rewritten. */
void app_adc_reinit(uint8_t comp_id)
{
    (void)comp_id;
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    RCC->APB2RSTR |=  RCC_APB2RSTR_ADCRST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADCRST;
    RCC->AHB1RSTR |=  RCC_AHB1RSTR_DMA2RST;
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_DMA2RST;
    adc_dma_config();
}

void app_tim_reinit(uint8_t comp_id)
{
    (void)comp_id;
    RCC->APB1RSTR |=  RCC_APB1RSTR_TIM2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;
    tim2_config();
}

void app_gpio_reinit(uint8_t comp_id)
{
    (void)comp_id;
    gpio_config();
}

/* ======================================================================= */
/* Runtime                                                                  */
/* ======================================================================= */
app_block_t app_adc_block(void)
{
    app_block_t b;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();                         /* ISR must not change it mid-copy */
    for (uint32_t i = 0; i < APP_BLOCK_LEN; i++) {
        b.s[i] = s_block.s[i];
    }
    b.seq = s_block.seq;
    __set_PRIMASK(pm);
    return b;
}

void app_set_ctrl_task(void *task_handle)
{
    s_ctrl_task = (TaskHandle_t)task_handle;
}

/*
 * DMA2 Stream0 interrupt: 10 conversions have landed in s_dma_buf.
 * Keep it SHORT. It copies the block (the DMA is already refilling
 * s_dma_buf for the next 10 ms), checks the error flags, and wakes the
 * sensor task. All real work happens in the task.
 */
void DMA2_Stream0_IRQHandler(void)
{
    FM_ISR_ENTER(COMP_ISR_ADC);

    uint32_t isr = DMA2->LISR;
    DMA2->LIFCR = 0x3DU;                     /* clear the flags we just read */

    if (isr & DMA_LISR_TEIF0) {
        fm_periph_error(COMP_PER_ADC, APP_PERR_DMA_TE);
    }
    if (ADC1->SR & ADC_SR_OVR) {             /* a result was lost            */
        ADC1->SR &= ~ADC_SR_OVR;
        fm_periph_error(COMP_PER_ADC, APP_PERR_ADC_OVR);
    }
    if (isr & DMA_LISR_TCIF0) {
        for (uint32_t i = 0; i < APP_BLOCK_LEN; i++) {
            s_block.s[i] = s_dma_buf[i];
        }
        s_block.seq++;
        TaskHandle_t t = s_ctrl_task;
        if (t != NULL) {
            BaseType_t woken = pdFALSE;
            vTaskNotifyGiveFromISR(t, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }

    FM_ISR_EXIT(COMP_ISR_ADC);
}

/* ======================================================================= */
/* Fault-injection hooks (app_hooks.h)                                       */
/* ======================================================================= */
bool app_periph_injectable(uint8_t comp)
{
    return comp == COMP_PER_ADC || comp == COMP_PER_TIM ||
           comp == COMP_PER_GPIO || comp == COMP_PER_UART;
}

/* Scrambles ONE configuration register of the peripheral. Called from the
 * TIM6 interrupt by fault_inject_on_tick(). */
void app_corrupt_periph(uint8_t comp)
{
    switch (comp) {
    case COMP_PER_ADC:  ADC1->CR2 ^= (1U << ADC_CR2_EXTSEL_Pos); break; /* wrong trigger  */
    case COMP_PER_TIM:  TIM2->PSC = 8U;                          break; /* 10x too fast   */
    case COMP_PER_GPIO: GPIOA->MODER &= ~(3U << 0);              break; /* PA0 not analog */
    case COMP_PER_UART: USART2->CR3 |= USART_CR3_ONEBIT;         break; /* sampling mode  */
    default: break;
    }
}
