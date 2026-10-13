/*
 * stm32f446xx_gpio_driver.h
 *
 *  Created on: Aug 23, 2026
 *      Author: raghudathesh.gp
 */

#ifndef INC_STM32F446XX_GPIO_DRIVER_H_     //this contains MCU Specific data
#define INC_STM32F446XX_GPIO_DRIVER_H_     //this contains MCU Specific data

#include "stm32f446xx.h"				   //this contains driver Specific data


/*
 * This is a Configuration structure for a GPIO pin
 */

typedef struct
{
uint8_t GPIO_PinNumber;
uint8_t GPIO_PinMode;  //<Possible values from @GPIO_PIN_MODES - stm32f446xx.h line 577 - 580
uint8_t GPIO_PinSpeed; //<Possible values from @GPIO_PIN_SPEED
uint8_t GPIO_PinPuPdControl;
uint8_t GPIO_PinOPType;
uint8_t GPIO_PinAltFunMode;
}GPIO_PinConfig_t;

/*
* This is a Handle structure for a GPIO pin
*/

typedef struct
{

GPIO_RegDef_t *pGPIOx;	// *!< pGPIOx= pGPIObase addr. This holds the base address of the GPIO port to which the pin belongs >*/
GPIO_PinConfig_t GPIO_PinConfig;	// *< This holds GPIO pin configuration settings - just created>*/

}GPIO_Handle_t;

/********************************************************************************
 *                         APIs supported by this driver
 *              For more information about the APIs check the function definitions
 ********************************************************************************/

/* Peripheral Clock Setup */

//void GPIO_Init(void);

/* GPIO Initialization and De-initialization */
//void GPIO_DeInit(void);
//void GPIO_PeriClockControl(void);

/* GPIO Read and Write */
//void GPIO_ReadFromInputPin(void);
//void GPIO_ReadFromInputPort(void);

//void GPIO_WriteToOutputPin(void);
//void GPIO_WriteToOutputPort(void);
//void GPIO_ToggleOutputPin(void);

/* GPIO Interrupt Configuration */
//void GPIO_IRQConfig(void);
//void GPIO_IRQConfig(void);

/* GPIO Driver API Prototypes */

/* Peripheral Clock Setup */
void GPIO_PeriClockControl(GPIO_RegDef_t *pGPIOx, uint8_t EnorDi); // for EnorDi = define macro in stm32f446xx.h

/* GPIO Initialization and De-initialization */
void GPIO_Init(GPIO_Handle_t *pGPIOHandle);
void GPIO_DeInit(GPIO_RegDef_t *pGPIOx);

/* GPIO Read and Write */
uint8_t  GPIO_ReadFromInputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber); // pin state is 0 or 1
uint16_t GPIO_ReadFromInputPort(GPIO_RegDef_t *pGPIOx); //uint16_t = GPIO port is 16 pin

void GPIO_WriteToOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber, uint8_t Value); //Value = 0 (GPIO_PIN_SET) =1 GPIO_PIN_RESET
void GPIO_WriteToOutputPort(GPIO_RegDef_t *pGPIOx, uint16_t Value);
void GPIO_ToggleOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber);

/* GPIO Interrupt Configuration */
void GPIO_IRQConfig(uint8_t IRQNumber, uint8_t IRQPriority, uint8_t EnorDi);
void GPIO_IRQHandling(uint8_t PinNumber);

#endif /* INC_STM32F446XX_GPIO_DRIVER_H_ */

















