/*
 * main.c
 *
 *  Created on: Sep 10, 2026
 *      Author: Vinay Prakash Janewad
 */

#include "main.h"

#define NUM_LEDS	10

GPIO_Handle_t LED_handle;
GPIO_Handle_t Button_handle;

volatile uint8_t buttonPressed = 0;
volatile uint8_t selectedSequence = 0;

/// @brief This function generates delay in milliseconds
/// @param value Value of delay in milliseconds
void delayMilliSeconds(uint32_t value)
{
    volatile uint32_t i, j;

    for(i = 0; i < value; i++) {
        for(j = 0; j < 12000; j++);
    }
}

/// @brief Initializes the PORT to which LEDs are connected
/// @param -None
void LED_Init(void)
{
	LED_handle.pGPIOx = GPIOB;
	LED_handle.GPIO_PinConfig.GPIO_PinNumber = GPIO_PIN_NO_0 | GPIO_PIN_NO_1 | GPIO_PIN_NO_2 | GPIO_PIN_NO_3 | GPIO_PIN_NO_4 | GPIO_PIN_NO_5 | GPIO_PIN_NO_6 | GPIO_PIN_NO_7 | GPIO_PIN_NO_8 | GPIO_PIN_NO_9;
	LED_handle.GPIO_PinConfig.GPIO_PinMode = GPIO_MODE_OUT;
	LED_handle.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
	LED_handle.GPIO_PinConfig.GPIO_PinAltFunMode = 0;
	LED_handle.GPIO_PinConfig.GPIO_PinSpeed = GPIO_SPEED_LOW;
	LED_handle.GPIO_PinConfig.GPIO_PinPuPdControl = GPIO_NO_PUPD;

	GPIO_PeriClockControl(LED_handle.pGPIOx, ENABLE);
	GPIO_Init(&LED_handle);
}

/// @brief Initializes the PORT to which Buttons are connected
/// @param
void BUTTON_Init(void)
{
	Button_handle.pGPIOx = GPIOA;
	Button_handle.GPIO_PinConfig.GPIO_PinNumber = GPIO_PIN_NO_0 | GPIO_PIN_NO_1 | GPIO_PIN_NO_4;
	Button_handle.GPIO_PinConfig.GPIO_PinMode = GPIO_MODE_IT_FT;
	Button_handle.GPIO_PinConfig.GPIO_PinPuPdControl = GPIO_PIN_PU;

	GPIO_PeriClockControl(Button_handle.pGPIOx, ENABLE);
	GPIO_Init(&Button_handle);

}

/// @brief Wrapper function to configure and Enable given IRQ
/// @param IRQNumber Number of the IRQ to be configured
/// @param IRQPriority Priority of the IRQ to be configured
/// @param IRQState State of the IRQ to be configured. (Enable or Disable)
void IRQ_Config(uint8_t IRQNumber, uint8_t IRQPriority, uint8_t IRQState)
{
	GPIO_IRQConfig(IRQNumber, IRQPriority, IRQState);
}

/// @brief Blinks the given number of LEDs connected to PORTB
/// @param count Number of LEDs to blink
void blinkLEDs(uint8_t count)
{
	for (uint8_t i = 0; i < count; i++)	{
		GPIO_WriteToOutputPin(GPIOB, (0x01 << i), GPIO_PIN_SET);
	}

	delayMilliSeconds(200);

	for (uint8_t i = 0; i < count; i++) {
		GPIO_WriteToOutputPin(GPIOB, (0x01 << i), GPIO_PIN_RESET);
	}
}

/// @brief This function displays the given pattern on LEDs
/// @param value The value to be displayed on LEDs
void displayPattern(uint32_t value)
{
	uint32_t temp = 0;

	temp = LED_handle.pGPIOx->ODR;
	temp = temp & ~((0x01 << NUM_LEDS) - 1);
	temp = temp | value;
	LED_handle.pGPIOx->ODR = temp;
}

/// @brief This function generates sequence 1. In this sequence, every term is the sum of its
///			previous 3 terms. Initial 3 terms are 0, 0 and 1
/// @param  -None
void sequence_1(void)
{
  uint32_t term1 = 0;
  uint32_t term2 = 0;
  uint32_t term3 = 1;
  uint32_t nextTerm;
  uint8_t numTerms = 10;

  displayPattern(term1);
  delayMilliSeconds(500);

  displayPattern(term2);
  delayMilliSeconds(500);

  displayPattern(term3);
  delayMilliSeconds(500);

  numTerms = numTerms - 3;

  while (numTerms > 0) {
	if(buttonPressed != 0)
		return;

    nextTerm = term1 + term2 + term3;

    term1 = term2;
    term2 = term3;
    term3 = nextTerm;

    displayPattern(nextTerm);
    delayMilliSeconds(500);

    numTerms--;
  }
}

/// @brief This function generates sequence 2. In this sequence, every term is the sum of its
///			previous 2 terms. (AKA Fibonnacci Series)
/// @param -None
void sequence_2(void)
{
	uint32_t term1 = 0;
	uint32_t term2 = 1;
	uint32_t nextTerm;

	uint16_t numTerms = 10;

	if (buttonPressed != 0)
		return;

	displayPattern(term1);
	delayMilliSeconds(500);

	displayPattern(term2);
	delayMilliSeconds(500);

	numTerms = numTerms - 2;

	while (numTerms > 0) {
		nextTerm = term1 + term2;

		term1 = term2;
		term2 = nextTerm;

		displayPattern(nextTerm);
		delayMilliSeconds(500);

		numTerms--;
	}
}

/// @brief This function displays running pattern on LEDs. LEDS are turned ON one-by-one and turned OFF one-by-one
/// @param -None
void sequence_3(void)
{
	uint8_t i;

	if (buttonPressed != 0)
		return;

	/* Turn ON LEDs one by one */
	for (i = 0; i < NUM_LEDS; i++) {
		GPIO_WriteToOutputPin(GPIOB, (GPIO_PIN_NO_1 << i), GPIO_PIN_SET);
		delayMilliSeconds(50);
	}

	delayMilliSeconds(200);

	/* Turn OFF LEDs one by one */
	for (i = 0; i < NUM_LEDS; i++) {
		GPIO_WriteToOutputPin(GPIOB, (GPIO_PIN_NO_1 << i), GPIO_PIN_RESET);
		delayMilliSeconds(50);
	}
}

int main(void)
{
	/*Configure LEDs*/
	/*LEDs are connected to PORT B*/
	LED_Init();

	/*Configure Buttons*/
	/*Buttons are connected to PORT A*/
	BUTTON_Init();

	/*Configure IRQ Priority and Enable them*/
	IRQ_Config(IRQ_NO_EXTI0, 2, ENABLE);
	IRQ_Config(IRQ_NO_EXTI1, 2, ENABLE);
	IRQ_Config(IRQ_NO_EXTI4, 2, ENABLE);

	while (1) {
		// Check if a button interrupt has triggered an action
		if (buttonPressed) {
			buttonPressed = 0;

			// Handle the specific behavior based on which button triggered it
			switch (selectedSequence) {
				case 1: {
					blinkLEDs(1); // 1 LED blinks once
					sequence_1();  // Run sequence 1 on LEDs

				}
				break;

				case 2: {
					blinkLEDs(2); // 2 LEDs blink once
					sequence_2();  // Run sequence 2 on LEDs
				}
				break;

				case 3: {
					blinkLEDs(3); // 3 LEDs blink once
					sequence_3();  // Run sequence 3 on LEDs
				}
				break;

				default:
					break;
			}
		}
	}

	return 0;
}
