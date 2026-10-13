/*
 * stm32f446xx.h
 *
 *  Created on: Aug 22, 2026
 *      Author: raghudathesh.gp
 */

#ifndef INC_STM32F446XX_H_
#define INC_STM32F446XX_H_

#include<stdint.h>

// #define __vo volatile //You can use __vo in place of volatile


/*
* base addresses of Flash and SRAM memories
*/

#define FLASH_BASEADDR     0x08000000U  // Base address of main Flash memory
#define SRAM1_BASEADDR     0x20000000U  // Base address of SRAM1 (112 KB)
#define SRAM2_BASEADDR     0x2001C000U  // Base address of SRAM2 (16 KB)
#define ROM_BASEADDR       0x1FFF0000U  // Base address of system memory (bootloader ROM)
#define SRAM               SRAM1_BASEADDR  // Alias for the start of SRAM

/*
* AHBx and APBx Bus Peripheral base addresses
*/

#define PERIPH_BASE         0x40000000U  // Base address of peripheral memory

#define APB1PERIPH_BASE     PERIPH_BASE  // Base address of APB1 peripherals
#define APB2PERIPH_BASE     0x40010000U  // Base address of APB2 peripherals
#define AHB1PERIPH_BASE     0x40020000U  // Base address of AHB1 peripherals
#define AHB2PERIPH_BASE     0x50000000U  // Base address of AHB2 peripherals

/*
* Base addresses of peripherals which are hanging on AHB1 bus
* TODO : Complete for all other peripherals
*/

#define GPIOA_BASEADDR     (AHB1PERIPH_BASE + 0x0000U)  // GPIO Port A
#define GPIOB_BASEADDR     (AHB1PERIPH_BASE + 0x0400U)  // GPIO Port B
#define GPIOC_BASEADDR     (AHB1PERIPH_BASE + 0x0800U)  // GPIO Port C
#define GPIOD_BASEADDR     (AHB1PERIPH_BASE + 0x0C00U)  // GPIO Port D
#define GPIOE_BASEADDR     (AHB1PERIPH_BASE + 0x1000U)  // GPIO Port E
#define GPIOF_BASEADDR     (AHB1PERIPH_BASE + 0x1400U)  // GPIO Port F
#define GPIOG_BASEADDR     (AHB1PERIPH_BASE + 0x1800U)  // GPIO Port G
#define GPIOH_BASEADDR     (AHB1PERIPH_BASE + 0x1C00U)  // GPIO Port H

#define CRC_BASEADDR       (AHB1PERIPH_BASE + 0x3000U)  // CRC unit
#define RCC_BASEADDR       (AHB1PERIPH_BASE + 0x3800U)  // Reset and Clock Control
#define FLASH_R_BASEADDR   (AHB1PERIPH_BASE + 0x3C00U)  // Flash interface
#define BKPSRAM_BASEADDR   (AHB1PERIPH_BASE + 0x4000U)  // Backup SRAM
#define DMA1_BASEADDR      (AHB1PERIPH_BASE + 0x6000U)  // DMA controller 1
#define DMA2_BASEADDR      (AHB1PERIPH_BASE + 0x6400U)  // DMA controller 2

/*
* Base addresses of peripherals which are hanging on APB1 bus
* TODO : Complete for all other peripherals
*/

/* Peripherals hanging on APB1 bus */

#define TIM2_BASEADDR       (APB1PERIPH_BASE + 0x0000U)  // Timer 2
#define TIM3_BASEADDR       (APB1PERIPH_BASE + 0x0400U)  // Timer 3
#define TIM4_BASEADDR       (APB1PERIPH_BASE + 0x0800U)  // Timer 4
#define TIM5_BASEADDR       (APB1PERIPH_BASE + 0x0C00U)  // Timer 5
#define TIM6_BASEADDR       (APB1PERIPH_BASE + 0x1000U)  // Basic Timer 6
#define TIM7_BASEADDR       (APB1PERIPH_BASE + 0x1400U)  // Basic Timer 7

#define TIM12_BASEADDR      (APB1PERIPH_BASE + 0x1800U)  // Timer 12
#define TIM13_BASEADDR      (APB1PERIPH_BASE + 0x1C00U)  // Timer 13
#define TIM14_BASEADDR      (APB1PERIPH_BASE + 0x2000U)  // Timer 14

#define RTC_BASEADDR        (APB1PERIPH_BASE + 0x2800U)  // Real-Time Clock
#define WWDG_BASEADDR       (APB1PERIPH_BASE + 0x2C00U)  // Window Watchdog
#define IWDG_BASEADDR       (APB1PERIPH_BASE + 0x3000U)  // Independent Watchdog

#define SPI2_BASEADDR       (APB1PERIPH_BASE + 0x3800U)  // SPI2 / I2S2
#define SPI3_BASEADDR       (APB1PERIPH_BASE + 0x3C00U)  // SPI3 / I2S3
#define SPDIFRX_BASEADDR    (APB1PERIPH_BASE + 0x4000U)  // S/PDIF receiver

#define USART2_BASEADDR     (APB1PERIPH_BASE + 0x4400U)  // USART2
#define USART3_BASEADDR     (APB1PERIPH_BASE + 0x4800U)  // USART3
#define UART4_BASEADDR      (APB1PERIPH_BASE + 0x4C00U)  // UART4
#define UART5_BASEADDR      (APB1PERIPH_BASE + 0x5000U)  // UART5

#define I2C1_BASEADDR       (APB1PERIPH_BASE + 0x5400U)  // I2C1
#define I2C2_BASEADDR       (APB1PERIPH_BASE + 0x5800U)  // I2C2
#define I2C3_BASEADDR       (APB1PERIPH_BASE + 0x5C00U)  // I2C3
#define FMPI2C1_BASEADDR    (APB1PERIPH_BASE + 0x6000U)  // Fast-mode Plus I2C1

#define CAN1_BASEADDR       (APB1PERIPH_BASE + 0x6400U)  // CAN controller 1
#define CAN2_BASEADDR       (APB1PERIPH_BASE + 0x6800U)  // CAN controller 2
#define CEC_BASEADDR        (APB1PERIPH_BASE + 0x6C00U)  // HDMI-CEC controller

#define PWR_BASEADDR        (APB1PERIPH_BASE + 0x7000U)  // Power controller
#define DAC_BASEADDR        (APB1PERIPH_BASE + 0x7400U)  // Digital-to-Analog Converter


/*
* Base addresses of peripherals which are hanging on APB2 bus
* TODO : Complete for all other peripherals
*/

/* Peripherals hanging on APB2 bus */

#define TIM1_BASEADDR       (APB2PERIPH_BASE + 0x0000U)  // Advanced-control Timer 1
#define TIM8_BASEADDR       (APB2PERIPH_BASE + 0x0400U)  // Advanced-control Timer 8

#define USART1_BASEADDR     (APB2PERIPH_BASE + 0x1000U)  // USART1
#define USART6_BASEADDR     (APB2PERIPH_BASE + 0x1400U)  // USART6

#define ADC1_BASEADDR       (APB2PERIPH_BASE + 0x2000U)  // ADC1
#define ADC2_BASEADDR       (APB2PERIPH_BASE + 0x2100U)  // ADC2
#define ADC3_BASEADDR       (APB2PERIPH_BASE + 0x2200U)  // ADC3
#define ADC_COMMON_BASEADDR (APB2PERIPH_BASE + 0x2300U)  // ADC common registers

#define SDIO_BASEADDR       (APB2PERIPH_BASE + 0x2C00U)  // SDIO/SDMMC interface

#define SPI1_BASEADDR       (APB2PERIPH_BASE + 0x3000U)  // SPI1
#define SPI4_BASEADDR       (APB2PERIPH_BASE + 0x3400U)  // SPI4

#define SYSCFG_BASEADDR     (APB2PERIPH_BASE + 0x3800U)  // System configuration controller
#define EXTI_BASEADDR       (APB2PERIPH_BASE + 0x3C00U)  // External interrupt/event controller

#define TIM9_BASEADDR       (APB2PERIPH_BASE + 0x4000U)  // Timer 9
#define TIM10_BASEADDR      (APB2PERIPH_BASE + 0x4400U)  // Timer 10
#define TIM11_BASEADDR      (APB2PERIPH_BASE + 0x4800U)  // Timer 11

#define SAI1_BASEADDR       (APB2PERIPH_BASE + 0x5800U)  // Serial Audio Interface 1
#define SAI2_BASEADDR       (APB2PERIPH_BASE + 0x5C00U)  // Serial Audio Interface 2

/*
* Note : Registers of a peripheral are specific to MCU
* e.g : Number of Registers of SPI peripheral of STM32F4x family of MCUs may be different(more or less)
* Compared to number of registers of SPI peripheral of STM32Lx or STM32F0x family of MCUs
* Please check your Device RM (our case Table 26. GPIO register map and reset values)
* Below is a peripheral Registers definition structure for GPIO
*/

typedef struct
{
    volatile uint32_t MODER;      // Offset 0x00: Mode register  uint32_t = #include<stdint.h>
    volatile uint32_t OTYPER;     // Offset 0x04: Output type register
    volatile uint32_t OSPEEDR;    // Offset 0x08: Output speed register
    volatile uint32_t PUPDR;      // Offset 0x0C: Pull-up/Pull-down register
    volatile uint32_t IDR;        // Offset 0x10: Input data register
    volatile uint32_t ODR;        // Offset 0x14: Output data register
    volatile uint32_t BSRR;       // Offset 0x18: Bit set/reset register
    volatile uint32_t LCKR;       // Offset 0x1C: Configuration lock register
    volatile uint32_t AFR[2];     // Offset 0x20, 0x24: Alternate function registers

} GPIO_RegDef_t;

/*
* Below is a peripheral Registers definition structure for RCC
*/
typedef struct
{
    volatile uint32_t CR;            // Offset 0x00: RCC clock control register
    volatile uint32_t PLLCFGR;       // Offset 0x04: RCC PLL configuration register
    volatile uint32_t CFGR;          // Offset 0x08: RCC clock configuration register
    volatile uint32_t CIR;           // Offset 0x0C: RCC clock interrupt register

    volatile uint32_t AHB1RSTR;      // Offset 0x10: AHB1 peripheral reset register
    volatile uint32_t AHB2RSTR;      // Offset 0x14: AHB2 peripheral reset register
    volatile uint32_t AHB3RSTR;      // Offset 0x18: AHB3 peripheral reset register
    uint32_t RESERVED0;              // Offset 0x1C: Reserved

    volatile uint32_t APB1RSTR;      // Offset 0x20: APB1 peripheral reset register
    volatile uint32_t APB2RSTR;      // Offset 0x24: APB2 peripheral reset register
    uint32_t RESERVED1[2];           // Offset 0x28-0x2C: Reserved

    volatile uint32_t AHB1ENR;       // Offset 0x30: AHB1 peripheral clock enable register
    volatile uint32_t AHB2ENR;       // Offset 0x34: AHB2 peripheral clock enable register
    volatile uint32_t AHB3ENR;       // Offset 0x38: AHB3 peripheral clock enable register
    uint32_t RESERVED2;              // Offset 0x3C: Reserved

    volatile uint32_t APB1ENR;       // Offset 0x40: APB1 peripheral clock enable register
    volatile uint32_t APB2ENR;       // Offset 0x44: APB2 peripheral clock enable register
    uint32_t RESERVED3[2];           // Offset 0x48-0x4C: Reserved

    volatile uint32_t AHB1LPENR;     // Offset 0x50: AHB1 clock enable in low-power mode
    volatile uint32_t AHB2LPENR;     // Offset 0x54: AHB2 clock enable in low-power mode
    volatile uint32_t AHB3LPENR;     // Offset 0x58: AHB3 clock enable in low-power mode
    uint32_t RESERVED4;              // Offset 0x5C: Reserved

    volatile uint32_t APB1LPENR;     // Offset 0x60: APB1 clock enable in low-power mode
    volatile uint32_t APB2LPENR;     // Offset 0x64: APB2 clock enable in low-power mode
    uint32_t RESERVED5[2];           // Offset 0x68-0x6C: Reserved

    volatile uint32_t BDCR;          // Offset 0x70: Backup domain control register
    volatile uint32_t CSR;           // Offset 0x74: Clock control & status register
    uint32_t RESERVED6[2];           // Offset 0x78-0x7C: Reserved

    volatile uint32_t SSCGR;         // Offset 0x80: Spread spectrum clock generation register
    volatile uint32_t PLLI2SCFGR;    // Offset 0x84: PLLI2S configuration register
    volatile uint32_t PLLSAICFGR;    // Offset 0x88: PLLSAI configuration register
    volatile uint32_t DCKCFGR;       // Offset 0x8C: Dedicated clocks configuration register
    volatile uint32_t CKGATENR;      // Offset 0x90: Clocks gated enable register
    volatile uint32_t DCKCFGR2;      // Offset 0x94: Dedicated clocks configuration register 2

} RCC_RegDef_t;



/*
* Below is a peripheral Registers definition structure for EXTI
*/
typedef struct
{
    volatile uint32_t IMR;           // Offset 0x00: Interrupt mask register
    volatile uint32_t EMR;           // Offset 0x04: Event mask register
    volatile uint32_t RTSR;          // Offset 0x08: Rising trigger selection register
    volatile uint32_t FTSR;          // Offset 0x0C: Falling trigger selection register
    volatile uint32_t SWIER;         // Offset 0x10: Software interrupt event register
    volatile uint32_t PR;            // Offset 0x14: Pending register

} EXTI_RegDef_t;


/*
* Below is a peripheral Registers definition structure for SYSCFG
*/
typedef struct
{
    volatile uint32_t MEMRMP;        // Offset 0x00: Memory remap register
    volatile uint32_t PMC;           // Offset 0x04: Peripheral mode configuration register
    volatile uint32_t EXTICR[4];     // Offset 0x08-0x14: External interrupt configuration registers 1-4
    uint32_t RESERVED0[2];           // Offset 0x18-0x1C: Reserved
    volatile uint32_t CMPCR;         // Offset 0x20: Compensation cell control register
    uint32_t RESERVED1[2];           // Offset 0x24-0x28: Reserved
    volatile uint32_t CFGR;          // Offset 0x2C: Configuration register

} SYSCFG_RegDef_t;


/*
*peripheral definitions ( Peripheral base addresses typecasted to xxx_RegDef_t)
*/

#define GPIOA   ((GPIO_RegDef_t*)GPIOA_BASEADDR)  // GPIOA @ 0x40020000
#define GPIOB   ((GPIO_RegDef_t*)GPIOB_BASEADDR)  // GPIOB @ 0x40020400
#define GPIOC   ((GPIO_RegDef_t*)GPIOC_BASEADDR)  // GPIOC @ 0x40020800
#define GPIOD   ((GPIO_RegDef_t*)GPIOD_BASEADDR)  // GPIOD @ 0x40020C00
#define GPIOE   ((GPIO_RegDef_t*)GPIOE_BASEADDR)  // GPIOE @ 0x40021000
#define GPIOF   ((GPIO_RegDef_t*)GPIOF_BASEADDR)  // GPIOF @ 0x40021400
#define GPIOG   ((GPIO_RegDef_t*)GPIOG_BASEADDR)  // GPIOG @ 0x40021800
#define GPIOH   ((GPIO_RegDef_t*)GPIOH_BASEADDR)  // GPIOH @ 0x40021C00

#define RCC    ((RCC_RegDef_t *)RCC_BASEADDR)  // RCC @ 0x40023800

#define EXTI   ((EXTI_RegDef_t *)EXTI_BASEADDR)      // EXTI @ 0x40013C00
#define SYSCFG ((SYSCFG_RegDef_t *)SYSCFG_BASEADDR)  // SYSCFG @ 0x40013800


// ----------------------- Enable MAcros Starts ------------------------------------

/*
 * Clock Enable Macros for GPIOx peripherals - AHB1
 */
#define GPIOA_PCLK_EN()     (RCC->AHB1ENR |= (1U << 0))   // Enable GPIOA clock
#define GPIOB_PCLK_EN()     (RCC->AHB1ENR |= (1U << 1))   // Enable GPIOB clock
#define GPIOC_PCLK_EN()     (RCC->AHB1ENR |= (1U << 2))   // Enable GPIOC clock
#define GPIOD_PCLK_EN()     (RCC->AHB1ENR |= (1U << 3))   // Enable GPIOD clock
#define GPIOE_PCLK_EN()     (RCC->AHB1ENR |= (1U << 4))   // Enable GPIOE clock
#define GPIOF_PCLK_EN()     (RCC->AHB1ENR |= (1U << 5))   // Enable GPIOF clock
#define GPIOG_PCLK_EN()     (RCC->AHB1ENR |= (1U << 6))   // Enable GPIOG clock
#define GPIOH_PCLK_EN()     (RCC->AHB1ENR |= (1U << 7))   // Enable GPIOH clock


/*
 * Clock Enable Macros for other AHB1 peripherals
 */
#define CRC_PCLK_EN()       (RCC->AHB1ENR |= (1U << 12))  // Enable CRC clock
#define BKPSRAM_PCLK_EN()   (RCC->AHB1ENR |= (1U << 18))  // Enable Backup SRAM clock
#define DMA1_PCLK_EN()      (RCC->AHB1ENR |= (1U << 21))  // Enable DMA1 clock
#define DMA2_PCLK_EN()      (RCC->AHB1ENR |= (1U << 22))  // Enable DMA2 clock


/*
 * Clock Enable Macros for AHB2 peripherals
 */
#define OTGFS_PCLK_EN()     (RCC->AHB2ENR |= (1U << 7))   // Enable USB OTG FS clock


/*
 * Clock Enable Macros for TIMx peripherals - APB1
 */
#define TIM2_PCLK_EN()      (RCC->APB1ENR |= (1U << 0))   // Enable TIM2 clock
#define TIM3_PCLK_EN()      (RCC->APB1ENR |= (1U << 1))   // Enable TIM3 clock
#define TIM4_PCLK_EN()      (RCC->APB1ENR |= (1U << 2))   // Enable TIM4 clock
#define TIM5_PCLK_EN()      (RCC->APB1ENR |= (1U << 3))   // Enable TIM5 clock
#define TIM6_PCLK_EN()      (RCC->APB1ENR |= (1U << 4))   // Enable TIM6 clock
#define TIM7_PCLK_EN()      (RCC->APB1ENR |= (1U << 5))   // Enable TIM7 clock
#define TIM12_PCLK_EN()     (RCC->APB1ENR |= (1U << 6))   // Enable TIM12 clock
#define TIM13_PCLK_EN()     (RCC->APB1ENR |= (1U << 7))   // Enable TIM13 clock
#define TIM14_PCLK_EN()     (RCC->APB1ENR |= (1U << 8))   // Enable TIM14 clock


/*
 * Clock Enable Macro for WWDG peripheral - APB1
 */
#define WWDG_PCLK_EN()      (RCC->APB1ENR |= (1U << 11))  // Enable Window Watchdog clock


/*
 * Clock Enable Macros for SPIx peripherals - APB1
 */
#define SPI2_PCLK_EN()      (RCC->APB1ENR |= (1U << 14))  // Enable SPI2 clock
#define SPI3_PCLK_EN()      (RCC->APB1ENR |= (1U << 15))  // Enable SPI3 clock


/*
 * Clock Enable Macro for SPDIFRX - APB1
 */
#define SPDIFRX_PCLK_EN()   (RCC->APB1ENR |= (1U << 16))  // Enable S/PDIF receiver clock


/*
 * Clock Enable Macros for USART/UART peripherals - APB1
 */
#define USART2_PCLK_EN()    (RCC->APB1ENR |= (1U << 17))  // Enable USART2 clock
#define USART3_PCLK_EN()    (RCC->APB1ENR |= (1U << 18))  // Enable USART3 clock
#define UART4_PCLK_EN()     (RCC->APB1ENR |= (1U << 19))  // Enable UART4 clock
#define UART5_PCLK_EN()     (RCC->APB1ENR |= (1U << 20))  // Enable UART5 clock


/*
 * Clock Enable Macros for I2Cx peripherals - APB1
 */
#define I2C1_PCLK_EN()      (RCC->APB1ENR |= (1U << 21))  // Enable I2C1 clock
#define I2C2_PCLK_EN()      (RCC->APB1ENR |= (1U << 22))  // Enable I2C2 clock
#define I2C3_PCLK_EN()      (RCC->APB1ENR |= (1U << 23))  // Enable I2C3 clock
#define FMPI2C1_PCLK_EN()   (RCC->APB1ENR |= (1U << 24))  // Enable FM+ I2C1 clock


/*
 * Clock Enable Macros for CANx peripherals - APB1
 */
#define CAN1_PCLK_EN()      (RCC->APB1ENR |= (1U << 25))  // Enable CAN1 clock
#define CAN2_PCLK_EN()      (RCC->APB1ENR |= (1U << 26))  // Enable CAN2 clock


/*
 * Clock Enable Macro for CEC peripheral - APB1
 */
#define CEC_PCLK_EN()       (RCC->APB1ENR |= (1U << 27))  // Enable HDMI-CEC clock


/*
 * Clock Enable Macro for PWR peripheral - APB1
 */
#define PWR_PCLK_EN()       (RCC->APB1ENR |= (1U << 28))  // Enable Power Controller clock


/*
 * Clock Enable Macro for DAC peripheral - APB1
 */
#define DAC_PCLK_EN()       (RCC->APB1ENR |= (1U << 29))  // Enable DAC clock


/*
 * Clock Enable Macros for TIMx peripherals - APB2
 */
#define TIM1_PCLK_EN()      (RCC->APB2ENR |= (1U << 0))   // Enable TIM1 clock
#define TIM8_PCLK_EN()      (RCC->APB2ENR |= (1U << 1))   // Enable TIM8 clock
#define TIM9_PCLK_EN()      (RCC->APB2ENR |= (1U << 16))  // Enable TIM9 clock
#define TIM10_PCLK_EN()     (RCC->APB2ENR |= (1U << 17))  // Enable TIM10 clock
#define TIM11_PCLK_EN()     (RCC->APB2ENR |= (1U << 18))  // Enable TIM11 clock


/*
 * Clock Enable Macros for USARTx peripherals - APB2
 */
#define USART1_PCLK_EN()    (RCC->APB2ENR |= (1U << 4))   // Enable USART1 clock
#define USART6_PCLK_EN()    (RCC->APB2ENR |= (1U << 5))   // Enable USART6 clock


/*
 * Clock Enable Macros for ADCx peripherals - APB2
 */
#define ADC1_PCLK_EN()      (RCC->APB2ENR |= (1U << 8))   // Enable ADC1 clock
#define ADC2_PCLK_EN()      (RCC->APB2ENR |= (1U << 9))   // Enable ADC2 clock
#define ADC3_PCLK_EN()      (RCC->APB2ENR |= (1U << 10))  // Enable ADC3 clock


/*
 * Clock Enable Macro for SDIO peripheral - APB2
 */
#define SDIO_PCLK_EN()      (RCC->APB2ENR |= (1U << 11))  // Enable SDIO clock


/*
 * Clock Enable Macros for SPIx peripherals - APB2
 */
#define SPI1_PCLK_EN()      (RCC->APB2ENR |= (1U << 12))  // Enable SPI1 clock
#define SPI4_PCLK_EN()      (RCC->APB2ENR |= (1U << 13))  // Enable SPI4 clock


/*
 * Clock Enable Macro for SYSCFG peripheral - APB2
 */
#define SYSCFG_PCLK_EN()    (RCC->APB2ENR |= (1U << 14))  // Enable SYSCFG clock


/*
 * Clock Enable Macros for SAIx peripherals - APB2
 */
#define SAI1_PCLK_EN()      (RCC->APB2ENR |= (1U << 22))  // Enable SAI1 clock
#define SAI2_PCLK_EN()      (RCC->APB2ENR |= (1U << 23))  // Enable SAI2 clock

// ----------------- Disable Macros Starts --------------------------------------------

/*
 * Clock Disable Macros for GPIOx peripherals - AHB1
 */
#define GPIOA_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 0))   // Disable GPIOA clock
#define GPIOB_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 1))   // Disable GPIOB clock
#define GPIOC_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 2))   // Disable GPIOC clock
#define GPIOD_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 3))   // Disable GPIOD clock
#define GPIOE_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 4))   // Disable GPIOE clock
#define GPIOF_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 5))   // Disable GPIOF clock
#define GPIOG_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 6))   // Disable GPIOG clock
#define GPIOH_PCLK_DI()     (RCC->AHB1ENR &= ~(1U << 7))   // Disable GPIOH clock


/*
 * Clock Disable Macros for other AHB1 peripherals
 */
#define CRC_PCLK_DI()       (RCC->AHB1ENR &= ~(1U << 12))  // Disable CRC clock
#define BKPSRAM_PCLK_DI()   (RCC->AHB1ENR &= ~(1U << 18))  // Disable Backup SRAM clock
#define DMA1_PCLK_DI()      (RCC->AHB1ENR &= ~(1U << 21))  // Disable DMA1 clock
#define DMA2_PCLK_DI()      (RCC->AHB1ENR &= ~(1U << 22))  // Disable DMA2 clock


/*
 * Clock Disable Macros for AHB2 peripherals
 */
#define OTGFS_PCLK_DI()     (RCC->AHB2ENR &= ~(1U << 7))   // Disable USB OTG FS clock


/*
 * Clock Disable Macros for TIMx peripherals - APB1
 */
#define TIM2_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 0))   // Disable TIM2 clock
#define TIM3_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 1))   // Disable TIM3 clock
#define TIM4_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 2))   // Disable TIM4 clock
#define TIM5_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 3))   // Disable TIM5 clock
#define TIM6_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 4))   // Disable TIM6 clock
#define TIM7_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 5))   // Disable TIM7 clock
#define TIM12_PCLK_DI()     (RCC->APB1ENR &= ~(1U << 6))   // Disable TIM12 clock
#define TIM13_PCLK_DI()     (RCC->APB1ENR &= ~(1U << 7))   // Disable TIM13 clock
#define TIM14_PCLK_DI()     (RCC->APB1ENR &= ~(1U << 8))   // Disable TIM14 clock


/*
 * Clock Disable Macro for WWDG peripheral - APB1
 */
#define WWDG_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 11))  // Disable Window Watchdog clock


/*
 * Clock Disable Macros for SPIx peripherals - APB1
 */
#define SPI2_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 14))  // Disable SPI2 clock
#define SPI3_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 15))  // Disable SPI3 clock


/*
 * Clock Disable Macro for SPDIFRX peripheral - APB1
 */
#define SPDIFRX_PCLK_DI()   (RCC->APB1ENR &= ~(1U << 16))  // Disable S/PDIF receiver clock


/*
 * Clock Disable Macros for USART/UART peripherals - APB1
 */
#define USART2_PCLK_DI()    (RCC->APB1ENR &= ~(1U << 17))  // Disable USART2 clock
#define USART3_PCLK_DI()    (RCC->APB1ENR &= ~(1U << 18))  // Disable USART3 clock
#define UART4_PCLK_DI()     (RCC->APB1ENR &= ~(1U << 19))  // Disable UART4 clock
#define UART5_PCLK_DI()     (RCC->APB1ENR &= ~(1U << 20))  // Disable UART5 clock


/*
 * Clock Disable Macros for I2Cx peripherals - APB1
 */
#define I2C1_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 21))  // Disable I2C1 clock
#define I2C2_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 22))  // Disable I2C2 clock
#define I2C3_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 23))  // Disable I2C3 clock
#define FMPI2C1_PCLK_DI()   (RCC->APB1ENR &= ~(1U << 24))  // Disable FM+ I2C1 clock


/*
 * Clock Disable Macros for CANx peripherals - APB1
 */
#define CAN1_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 25))  // Disable CAN1 clock
#define CAN2_PCLK_DI()      (RCC->APB1ENR &= ~(1U << 26))  // Disable CAN2 clock


/*
 * Clock Disable Macro for CEC peripheral - APB1
 */
#define CEC_PCLK_DI()       (RCC->APB1ENR &= ~(1U << 27))  // Disable HDMI-CEC clock


/*
 * Clock Disable Macro for PWR peripheral - APB1
 */
#define PWR_PCLK_DI()       (RCC->APB1ENR &= ~(1U << 28))  // Disable Power Controller clock


/*
 * Clock Disable Macro for DAC peripheral - APB1
 */
#define DAC_PCLK_DI()       (RCC->APB1ENR &= ~(1U << 29))  // Disable DAC clock


/*
 * Clock Disable Macros for TIMx peripherals - APB2
 */
#define TIM1_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 0))   // Disable TIM1 clock
#define TIM8_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 1))   // Disable TIM8 clock
#define TIM9_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 16))  // Disable TIM9 clock
#define TIM10_PCLK_DI()     (RCC->APB2ENR &= ~(1U << 17))  // Disable TIM10 clock
#define TIM11_PCLK_DI()     (RCC->APB2ENR &= ~(1U << 18))  // Disable TIM11 clock


/*
 * Clock Disable Macros for USARTx peripherals - APB2
 */
#define USART1_PCLK_DI()    (RCC->APB2ENR &= ~(1U << 4))   // Disable USART1 clock
#define USART6_PCLK_DI()    (RCC->APB2ENR &= ~(1U << 5))   // Disable USART6 clock


/*
 * Clock Disable Macros for ADCx peripherals - APB2
 */
#define ADC1_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 8))   // Disable ADC1 clock
#define ADC2_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 9))   // Disable ADC2 clock
#define ADC3_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 10))  // Disable ADC3 clock


/*
 * Clock Disable Macro for SDIO peripheral - APB2
 */
#define SDIO_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 11))  // Disable SDIO clock


/*
 * Clock Disable Macros for SPIx peripherals - APB2
 */
#define SPI1_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 12))  // Disable SPI1 clock
#define SPI4_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 13))  // Disable SPI4 clock


/*
 * Clock Disable Macro for SYSCFG peripheral - APB2
 */
#define SYSCFG_PCLK_DI()    (RCC->APB2ENR &= ~(1U << 14))  // Disable SYSCFG clock


/*
 * Clock Disable Macros for SAIx peripherals - APB2
 */
#define SAI1_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 22))  // Disable SAI1 clock
#define SAI2_PCLK_DI()      (RCC->APB2ENR &= ~(1U << 23))  // Disable SAI2 clock


//------------ MAcros Definition Start -------------------------------

/*
 * Generic Macros
 */
#define ENABLE                  1U   // Enable peripheral/function
#define DISABLE                 0U   // Disable peripheral/function

#define SET                     ENABLE
#define RESET                   DISABLE

#define GPIO_PIN_SET            SET  // GPIO pin HIGH
#define GPIO_PIN_RESET          RESET // GPIO pin LOW

#define NUM_GPIO_PINS			16U


/*
 * GPIO Pin Number Macros
 */
#define GPIO_PIN_NO_0           0U   // GPIO pin 0
#define GPIO_PIN_NO_1           1U   // GPIO pin 1
#define GPIO_PIN_NO_2           2U   // GPIO pin 2
#define GPIO_PIN_NO_3           3U   // GPIO pin 3
#define GPIO_PIN_NO_4           4U   // GPIO pin 4
#define GPIO_PIN_NO_5           5U   // GPIO pin 5
#define GPIO_PIN_NO_6           6U   // GPIO pin 6
#define GPIO_PIN_NO_7           7U   // GPIO pin 7
#define GPIO_PIN_NO_8           8U   // GPIO pin 8
#define GPIO_PIN_NO_9           9U   // GPIO pin 9
#define GPIO_PIN_NO_10          10U  // GPIO pin 10
#define GPIO_PIN_NO_11          11U  // GPIO pin 11
#define GPIO_PIN_NO_12          12U  // GPIO pin 12
#define GPIO_PIN_NO_13          13U  // GPIO pin 13
#define GPIO_PIN_NO_14          14U  // GPIO pin 14
#define GPIO_PIN_NO_15          15U  // GPIO pin 15


/*
 * @GPIO_PIN_MODES
 * GPIO Pin Mode Macros
 */
#define GPIO_MODE_IN            0U   // GPIO input mode taken from refe manual
#define GPIO_MODE_OUT           1U   // GPIO output mode
#define GPIO_MODE_ALTFN         2U   // GPIO alternate-function mode
#define GPIO_MODE_ANALOG        3U   // GPIO analog mode

#define GPIO_MODE_IT_FT         4U   // Interrupt on falling edge
#define GPIO_MODE_IT_RT         5U   // Interrupt on rising edge
#define GPIO_MODE_IT_RFT        6U   // Interrupt on rising/falling edge


/*
 * GPIO Output Type Macros
 */
#define GPIO_OP_TYPE_PP         0U   // Push-pull output
#define GPIO_OP_TYPE_OD         1U   // Open-drain output


/*
 * @GPIO_PIN_SPEED
 * GPIO Output Speed Macros
 */
#define GPIO_SPEED_LOW          0U   // Low-speed output
#define GPIO_SPEED_MEDIUM       1U   // Medium-speed output
#define GPIO_SPEED_FAST         2U   // Fast-speed output
#define GPIO_SPEED_HIGH         3U   // High-speed output


/*
 * GPIO Pull-up / Pull-down Macros
 */
#define GPIO_NO_PUPD            0U   // No pull-up/pull-down
#define GPIO_PIN_PU             1U   // Internal pull-up
#define GPIO_PIN_PD             2U   // Internal pull-down


/*
 * GPIO Alternate Function Macros
 */
#define GPIO_AF0                0U   // Alternate Function 0
#define GPIO_AF1                1U   // Alternate Function 1
#define GPIO_AF2                2U   // Alternate Function 2
#define GPIO_AF3                3U   // Alternate Function 3
#define GPIO_AF4                4U   // Alternate Function 4
#define GPIO_AF5                5U   // Alternate Function 5
#define GPIO_AF6                6U   // Alternate Function 6
#define GPIO_AF7                7U   // Alternate Function 7
#define GPIO_AF8                8U   // Alternate Function 8
#define GPIO_AF9                9U   // Alternate Function 9
#define GPIO_AF10               10U  // Alternate Function 10
#define GPIO_AF11               11U  // Alternate Function 11
#define GPIO_AF12               12U  // Alternate Function 12
#define GPIO_AF13               13U  // Alternate Function 13
#define GPIO_AF14               14U  // Alternate Function 14
#define GPIO_AF15               15U  // Alternate Function 15


/*
 * Macros to reset GPIOx peripherals (set the reset bit, then clear it)
 */
#define GPIOA_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 0)); (RCC->AHB1RSTR &= ~(1U << 0)); }while(0)
#define GPIOB_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 1)); (RCC->AHB1RSTR &= ~(1U << 1)); }while(0)
#define GPIOC_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 2)); (RCC->AHB1RSTR &= ~(1U << 2)); }while(0)
#define GPIOD_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 3)); (RCC->AHB1RSTR &= ~(1U << 3)); }while(0)
#define GPIOE_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 4)); (RCC->AHB1RSTR &= ~(1U << 4)); }while(0)
#define GPIOF_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 5)); (RCC->AHB1RSTR &= ~(1U << 5)); }while(0)
#define GPIOG_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 6)); (RCC->AHB1RSTR &= ~(1U << 6)); }while(0)
#define GPIOH_REG_RESET()   do{ (RCC->AHB1RSTR |= (1U << 7)); (RCC->AHB1RSTR &= ~(1U << 7)); }while(0)


/*
 * Returns the port code (0..7) for a given GPIO base address.
 * Used to program SYSCFG_EXTICR during interrupt configuration.
 */
#define GPIO_BASEADDR_TO_CODE(x)   ( (x == GPIOA) ? 0U :\
                                     (x == GPIOB) ? 1U :\
                                     (x == GPIOC) ? 2U :\
                                     (x == GPIOD) ? 3U :\
                                     (x == GPIOE) ? 4U :\
                                     (x == GPIOF) ? 5U :\
                                     (x == GPIOG) ? 6U :\
                                     (x == GPIOH) ? 7U : 0U )


/*
 * ARM Cortex-M4 Processor NVIC ISERx register addresses (Interrupt Set-Enable)
 */
#define NVIC_ISER0          ((volatile uint32_t*)0xE000E100U)
#define NVIC_ISER1          ((volatile uint32_t*)0xE000E104U)
#define NVIC_ISER2          ((volatile uint32_t*)0xE000E108U)
#define NVIC_ISER3          ((volatile uint32_t*)0xE000E10CU)


/*
 * ARM Cortex-M4 Processor NVIC ICERx register addresses (Interrupt Clear-Enable)
 */
#define NVIC_ICER0          ((volatile uint32_t*)0xE000E180U)
#define NVIC_ICER1          ((volatile uint32_t*)0xE000E184U)
#define NVIC_ICER2          ((volatile uint32_t*)0xE000E188U)
#define NVIC_ICER3          ((volatile uint32_t*)0xE000E18CU)


/*
 * ARM Cortex-M4 Processor NVIC Priority register base address
 */
#define NVIC_PR_BASE_ADDR   ((volatile uint32_t*)0xE000E400U)


/*
 * Number of priority bits implemented in the NVIC_IPR register (STM32F4 : 4)
 */
#define NO_PR_BITS_IMPLEMENTED   4U


/*
 * IRQ Numbers for EXTI - STM32F446xx
 */
#define IRQ_NO_EXTI0            6U   // EXTI line 0 interrupt
#define IRQ_NO_EXTI1            7U   // EXTI line 1 interrupt
#define IRQ_NO_EXTI2            8U   // EXTI line 2 interrupt
#define IRQ_NO_EXTI3            9U   // EXTI line 3 interrupt
#define IRQ_NO_EXTI4            10U  // EXTI line 4 interrupt

#define IRQ_NO_EXTI9_5          23U  // EXTI lines 5-9 interrupt
#define IRQ_NO_EXTI15_10        40U  // EXTI lines 10-15 interrupt

//------------ MAcros Definition End -------------------------------


#include "stm32f446xx_gpio_driver.h" //this help in apps main.c by just including stm32f446.h

#endif /* INC_STM32F446XX_H_ */
