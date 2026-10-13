#include "main.h"

void EXTI0_IRQHandler(void)
{
    GPIO_IRQHandling(GPIO_PIN_NO_0); // Clear the pending bit
    buttonPressed = 1;               // Your application logic
    selectedSequence = 1;
}

void EXTI1_IRQHandler(void)
{
    GPIO_IRQHandling(GPIO_PIN_NO_1); // Clear the pending bit
    buttonPressed = 1;               // Your application logic
    selectedSequence = 2;
}

void EXTI4_IRQHandler(void)
{
    GPIO_IRQHandling(GPIO_PIN_NO_4); // Clear the pending bit
    buttonPressed = 1;               // Your application logic
    selectedSequence = 3;
}
