################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/inject/fault_inject.c \
../Core/Src/inject/fm_uart.c \
../Core/Src/inject/uart_shell.c 

OBJS += \
./Core/Src/inject/fault_inject.o \
./Core/Src/inject/fm_uart.o \
./Core/Src/inject/uart_shell.o 

C_DEPS += \
./Core/Src/inject/fault_inject.d \
./Core/Src/inject/fm_uart.d \
./Core/Src/inject/uart_shell.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/inject/%.o Core/Src/inject/%.su Core/Src/inject/%.cyclo: ../Core/Src/inject/%.c Core/Src/inject/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Core/Src/monitor -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/BSP/STM32F4xx-Nucleo -I../Core/Src/app -I../Core/Src/inject -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-inject

clean-Core-2f-Src-2f-inject:
	-$(RM) ./Core/Src/inject/fault_inject.cyclo ./Core/Src/inject/fault_inject.d ./Core/Src/inject/fault_inject.o ./Core/Src/inject/fault_inject.su ./Core/Src/inject/fm_uart.cyclo ./Core/Src/inject/fm_uart.d ./Core/Src/inject/fm_uart.o ./Core/Src/inject/fm_uart.su ./Core/Src/inject/uart_shell.cyclo ./Core/Src/inject/uart_shell.d ./Core/Src/inject/uart_shell.o ./Core/Src/inject/uart_shell.su

.PHONY: clean-Core-2f-Src-2f-inject

