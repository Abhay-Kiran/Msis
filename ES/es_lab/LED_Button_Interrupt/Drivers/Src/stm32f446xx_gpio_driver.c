/*
 * stm32f446xx_gpio_driver.c
 *
 *  Created on: Aug 23, 2026
 *      Author: raghudathesh.gp
 */

#include "stm32f446xx_gpio_driver.h"


/*********************************************************************
 * @fn      		  - GPIO_PeriClockControl
 *
 * @brief             - Enables or disables peripheral clock for GPIO port
 *
 * @param[in]         - Base address of GPIO peripheral
 * @param[in]         - ENABLE or DISABLE
 *
 * @return            - None
 *********************************************************************/
void GPIO_PeriClockControl(GPIO_RegDef_t *pGPIOx, uint8_t EnorDi)
{
    if(EnorDi == ENABLE)
    {
        if(pGPIOx == GPIOA)
        {
            GPIOA_PCLK_EN();
        }
        else if(pGPIOx == GPIOB)
        {
            GPIOB_PCLK_EN();
        }
        else if(pGPIOx == GPIOC)
        {
            GPIOC_PCLK_EN();
        }
        else if(pGPIOx == GPIOD)
        {
            GPIOD_PCLK_EN();
        }
        else if(pGPIOx == GPIOE)
        {
            GPIOE_PCLK_EN();
        }
        else if(pGPIOx == GPIOF)
        {
            GPIOF_PCLK_EN();
        }
        else if(pGPIOx == GPIOG)
        {
            GPIOG_PCLK_EN();
        }
        else if(pGPIOx == GPIOH)
        {
            GPIOH_PCLK_EN();
        }
    }
    else
    {
        if(pGPIOx == GPIOA)
        {
            GPIOA_PCLK_DI();
        }
        else if(pGPIOx == GPIOB)
        {
            GPIOB_PCLK_DI();
        }
        else if(pGPIOx == GPIOC)
        {
            GPIOC_PCLK_DI();
        }
        else if(pGPIOx == GPIOD)
        {
            GPIOD_PCLK_DI();
        }
        else if(pGPIOx == GPIOE)
        {
            GPIOE_PCLK_DI();
        }
        else if(pGPIOx == GPIOF)
        {
            GPIOF_PCLK_DI();
        }
        else if(pGPIOx == GPIOG)
        {
            GPIOG_PCLK_DI();
        }
        else if(pGPIOx == GPIOH)
        {
            GPIOH_PCLK_DI();
        }
    }
}

/*********************************************************************
 * @fn      		  - GPIO_Init
 *
 * @brief             - Initializes a GPIO pin or multiple pins
 *
 * @param[in]         - Pointer to GPIO handle
 *
 * @return            - None
 *********************************************************************/
void GPIO_Init(GPIO_Handle_t *pGPIOHandle)
{
    uint32_t position;
    uint32_t ioposition = 0x00U;
    uint32_t iocurrent = 0x00U;
    uint32_t temp = 0x00U;

    for (position = 0U; position < NUM_GPIO_PINS; position++)
    {
        /* Get the IO position */
        ioposition = 0x01U << position;
        /* Get the current IO position */
        iocurrent = (uint32_t)(pGPIOHandle->GPIO_PinConfig.GPIO_PinNumber) & ioposition;

        if (iocurrent == ioposition)
        {
            uint8_t mode = pGPIOHandle->GPIO_PinConfig.GPIO_PinMode;

            /*--------------------- GPIO Mode Configuration ------------------------*/
            /* In case of Output or Alternate function mode selection */
            if ((mode == GPIO_MODE_OUT) || (mode == GPIO_MODE_ALTFN))
            {
                /* 1. Configure the IO Speed */
                temp = pGPIOHandle->pGPIOx->OSPEEDR;
                temp &= ~(0x3U << (2U * position));
                temp |= (pGPIOHandle->GPIO_PinConfig.GPIO_PinSpeed << (2U * position));
                pGPIOHandle->pGPIOx->OSPEEDR = temp;

                /* 2. Configure the IO Output Type */
                temp = pGPIOHandle->pGPIOx->OTYPER;
                temp &= ~(1U << position);
                temp |= (pGPIOHandle->GPIO_PinConfig.GPIO_PinOPType << position);
                pGPIOHandle->pGPIOx->OTYPER = temp;
            }

            /* 3. Activate the Pull-up or Pull-down resistor for non-analog modes */
            if (mode != GPIO_MODE_ANALOG)
            {
                temp = pGPIOHandle->pGPIOx->PUPDR;
                temp &= ~(0x3U << (2U * position));
                temp |= (pGPIOHandle->GPIO_PinConfig.GPIO_PinPuPdControl << (2U * position));
                pGPIOHandle->pGPIOx->PUPDR = temp;
            }

            /* 4. Configure Alternate function if selected */
            if (mode == GPIO_MODE_ALTFN)
            {
                uint8_t temp1 = position >> 3U;         /* position / 8 */
                uint8_t temp2 = position & 0x07U;       /* position % 8 */

                temp = pGPIOHandle->pGPIOx->AFR[temp1];
                temp &= ~(0xFU << (4U * temp2));
                temp |= ((uint32_t)(pGPIOHandle->GPIO_PinConfig.GPIO_PinAltFunMode) << (4U * temp2));
                pGPIOHandle->pGPIOx->AFR[temp1] = temp;
            }

            /* 5. Configure GPIO Direction mode (MODER) */
            /* Note: Interrupt modes require the pin mode to act as an Input */
            uint32_t base_mode = mode;
            if (mode >= GPIO_MODE_IT_FT)
            {
                base_mode = GPIO_MODE_IN;
            }

            temp = pGPIOHandle->pGPIOx->MODER;
            temp &= ~(0x3U << (2U * position));
            temp |= (base_mode << (2U * position));
            pGPIOHandle->pGPIOx->MODER = temp;


            /*--------------------- EXTI Mode Configuration ------------------------*/
            if (mode >= GPIO_MODE_IT_FT)
            {
                /* Enable SYSCFG Clock */
                SYSCFG_PCLK_EN();

                /* Configure GPIO port selection in SYSCFG_EXTICR */
                uint8_t exticr_idx = position >> 2U;    /* position / 4 */
                uint8_t exticr_pos = position & 0x03U;  /* position % 4 */
                uint8_t portcode   = GPIO_BASEADDR_TO_CODE(pGPIOHandle->pGPIOx);

                temp = SYSCFG->EXTICR[exticr_idx];
                temp &= ~(0xFU << (4U * exticr_pos));
                temp |= ((uint32_t)portcode << (4U * exticr_pos));
                SYSCFG->EXTICR[exticr_idx] = temp;

                /* Configure Falling trigger selection */
                if (mode == GPIO_MODE_IT_FT || mode == GPIO_MODE_IT_RFT)
                {
                    EXTI->FTSR |= (1U << position);
                }
                else
                {
                    EXTI->FTSR &= ~(1U << position);
                }

                /* Configure Rising trigger selection */
                if (mode == GPIO_MODE_IT_RT || mode == GPIO_MODE_IT_RFT)
                {
                    EXTI->RTSR |= (1U << position);
                }
                else
                {
                    EXTI->RTSR &= ~(1U << position);
                }

                /* Enable EXTI interrupt delivery (IMR) */
                EXTI->IMR |= (1U << position);
            }
        }
    }
}


/*********************************************************************
 * @fn      		  - GPIO_DeInit
 *
 * @brief             - Resets GPIO peripheral registers
 *
 * @param[in]         - Base address of GPIO peripheral
 *
 * @return            - None
 *********************************************************************/
void GPIO_DeInit(GPIO_RegDef_t *pGPIOx)
{
    if(pGPIOx == GPIOA)
    {
        GPIOA_REG_RESET();   //refer stm32f446xx.h file line no. 671 to 678
    }
    else if(pGPIOx == GPIOB)
    {
        GPIOB_REG_RESET();
    }
    else if(pGPIOx == GPIOC)
    {
        GPIOC_REG_RESET();
    }
    else if(pGPIOx == GPIOD)
    {
        GPIOD_REG_RESET();
    }
    else if(pGPIOx == GPIOE)
    {
        GPIOE_REG_RESET();
    }
    else if(pGPIOx == GPIOF)
    {
        GPIOF_REG_RESET();
    }
    else if(pGPIOx == GPIOG)
    {
        GPIOG_REG_RESET();
    }
    else if(pGPIOx == GPIOH)
    {
        GPIOH_REG_RESET();
    }
}


/*********************************************************************
 * @fn      		  - GPIO_ReadFromInputPin
 *
 * @brief             - Reads one GPIO input pin
 *
 * @return            - 0 or 1
 *********************************************************************/
uint8_t GPIO_ReadFromInputPin(GPIO_RegDef_t *pGPIOx,
                              uint8_t PinNumber)
{
    uint8_t value;

    value = (uint8_t)((pGPIOx->IDR >> PinNumber) & 0x1U);

    return value;
}


/*********************************************************************
 * @fn      		  - GPIO_ReadFromInputPort
 *
 * @brief             - Reads complete 16-bit GPIO input port
 *********************************************************************/
uint16_t GPIO_ReadFromInputPort(GPIO_RegDef_t *pGPIOx)
{
    return (uint16_t)pGPIOx->IDR;
}


/*********************************************************************
 * @fn      		  - GPIO_WriteToOutputPin
 *
 * @brief             - Writes SET or RESET to one GPIO output pin
 *********************************************************************/
void GPIO_WriteToOutputPin(GPIO_RegDef_t *pGPIOx,
                           uint8_t PinNumber,
                           uint8_t Value)
{
    if(Value == GPIO_PIN_SET)
    {
        /*
         * Set corresponding output bit
         */
        pGPIOx->BSRR = (1U << PinNumber);
    }
    else
    {
        /*
         * Reset corresponding output bit
         * Upper 16 bits of BSRR perform reset
         */
        pGPIOx->BSRR = (1U << (PinNumber + 16U));
    }
}


/*********************************************************************
 * @fn      		  - GPIO_WriteToOutputPort
 *
 * @brief             - Writes 16-bit value to GPIO output port
 *********************************************************************/
void GPIO_WriteToOutputPort(GPIO_RegDef_t *pGPIOx,
                            uint16_t Value)
{
    pGPIOx->ODR = Value;
}


/*********************************************************************
 * @fn      		  - GPIO_ToggleOutputPin
 *
 * @brief             - Toggles GPIO output pin
 *********************************************************************/
void GPIO_ToggleOutputPin(GPIO_RegDef_t *pGPIOx,
                          uint8_t PinNumber)
{
    pGPIOx->ODR ^= (1U << PinNumber);
}


/*********************************************************************
 * @fn      		  - GPIO_IRQConfig
 *
 * @brief             - Enables/disables IRQ and sets interrupt priority
 *********************************************************************/
void GPIO_IRQConfig(uint8_t IRQNumber,
                    uint8_t IRQPriority,
                    uint8_t EnorDi)
{
    if(EnorDi == ENABLE)
    {
        if(IRQNumber <= 31U)
        {
            *NVIC_ISER0 |= (1U << IRQNumber);
        }
        else if(IRQNumber <= 63U)
        {
            *NVIC_ISER1 |=
                    (1U << (IRQNumber % 32U));
        }
        else if(IRQNumber <= 95U)
        {
            *NVIC_ISER2 |=
                    (1U << (IRQNumber % 64U));
        }
    }
    else
    {
        if(IRQNumber <= 31U)
        {
            *NVIC_ICER0 |= (1U << IRQNumber);
        }
        else if(IRQNumber <= 63U)
        {
            *NVIC_ICER1 |=
                    (1U << (IRQNumber % 32U));
        }
        else if(IRQNumber <= 95U)
        {
            *NVIC_ICER2 |=
                    (1U << (IRQNumber % 64U));
        }
    }


    /*
     * Configure IRQ priority
     */
    uint8_t iprx = IRQNumber / 4U;
    uint8_t iprx_section = IRQNumber % 4U;

    uint8_t shift_amount =
            (8U * iprx_section) + (8U - NO_PR_BITS_IMPLEMENTED);

    *(NVIC_PR_BASE_ADDR + iprx) |=
            (IRQPriority << shift_amount);
}


/*********************************************************************
 * @fn      		  - GPIO_IRQHandling
 *
 * @brief             - Clears EXTI pending interrupt
 *********************************************************************/
void GPIO_IRQHandling(uint8_t PinNumber)
{
    /*
     * Check whether interrupt is pending
     */
    if(EXTI->PR & (1U << PinNumber))
    {
        /*
         * Clear pending flag by writing 1
         */
        EXTI->PR = (1U << PinNumber);
    }
}
