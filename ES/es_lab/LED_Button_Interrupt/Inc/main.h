#include <stdint.h>
#include "stm32f446xx_gpio_driver.h"

extern volatile uint8_t buttonPressed;
extern volatile uint8_t selectedSequence;

extern GPIO_Handle_t LED_handle;
extern GPIO_Handle_t Button_handle;

void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI4_IRQHandler(void);
