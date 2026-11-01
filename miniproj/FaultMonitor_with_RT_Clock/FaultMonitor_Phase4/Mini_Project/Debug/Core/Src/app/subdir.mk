################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app/app_periph.c \
../Core/Src/app/controller.c \
../Core/Src/app/idle_stats.c 

OBJS += \
./Core/Src/app/app_periph.o \
./Core/Src/app/controller.o \
./Core/Src/app/idle_stats.o 

C_DEPS += \
./Core/Src/app/app_periph.d \
./Core/Src/app/controller.d \
./Core/Src/app/idle_stats.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/app/%.o Core/Src/app/%.su Core/Src/app/%.cyclo: ../Core/Src/app/%.c Core/Src/app/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Core/Src/monitor -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/BSP/STM32F4xx-Nucleo -I../Core/Src/app -I../Core/Src/inject -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-app

clean-Core-2f-Src-2f-app:
	-$(RM) ./Core/Src/app/app_periph.cyclo ./Core/Src/app/app_periph.d ./Core/Src/app/app_periph.o ./Core/Src/app/app_periph.su ./Core/Src/app/controller.cyclo ./Core/Src/app/controller.d ./Core/Src/app/controller.o ./Core/Src/app/controller.su ./Core/Src/app/idle_stats.cyclo ./Core/Src/app/idle_stats.d ./Core/Src/app/idle_stats.o ./Core/Src/app/idle_stats.su

.PHONY: clean-Core-2f-Src-2f-app

