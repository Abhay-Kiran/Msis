################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/detect/fault_threshold.c \
../Core/Src/detect/fm_blackbox.c \
../Core/Src/detect/fm_detect.c \
../Core/Src/detect/fm_learn.c \
../Core/Src/detect/fm_monitor.c \
../Core/Src/detect/fm_recover.c \
../Core/Src/detect/fm_watchdog.c 

OBJS += \
./Core/Src/detect/fault_threshold.o \
./Core/Src/detect/fm_blackbox.o \
./Core/Src/detect/fm_detect.o \
./Core/Src/detect/fm_learn.o \
./Core/Src/detect/fm_monitor.o \
./Core/Src/detect/fm_recover.o \
./Core/Src/detect/fm_watchdog.o 

C_DEPS += \
./Core/Src/detect/fault_threshold.d \
./Core/Src/detect/fm_blackbox.d \
./Core/Src/detect/fm_detect.d \
./Core/Src/detect/fm_learn.d \
./Core/Src/detect/fm_monitor.d \
./Core/Src/detect/fm_recover.d \
./Core/Src/detect/fm_watchdog.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/detect/%.o Core/Src/detect/%.su Core/Src/detect/%.cyclo: ../Core/Src/detect/%.c Core/Src/detect/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Core/Src/monitor -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/BSP/STM32F4xx-Nucleo -I../Core/Src/app -I../Core/Src/inject -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-detect

clean-Core-2f-Src-2f-detect:
	-$(RM) ./Core/Src/detect/fault_threshold.cyclo ./Core/Src/detect/fault_threshold.d ./Core/Src/detect/fault_threshold.o ./Core/Src/detect/fault_threshold.su ./Core/Src/detect/fm_blackbox.cyclo ./Core/Src/detect/fm_blackbox.d ./Core/Src/detect/fm_blackbox.o ./Core/Src/detect/fm_blackbox.su ./Core/Src/detect/fm_detect.cyclo ./Core/Src/detect/fm_detect.d ./Core/Src/detect/fm_detect.o ./Core/Src/detect/fm_detect.su ./Core/Src/detect/fm_learn.cyclo ./Core/Src/detect/fm_learn.d ./Core/Src/detect/fm_learn.o ./Core/Src/detect/fm_learn.su ./Core/Src/detect/fm_monitor.cyclo ./Core/Src/detect/fm_monitor.d ./Core/Src/detect/fm_monitor.o ./Core/Src/detect/fm_monitor.su ./Core/Src/detect/fm_recover.cyclo ./Core/Src/detect/fm_recover.d ./Core/Src/detect/fm_recover.o ./Core/Src/detect/fm_recover.su ./Core/Src/detect/fm_watchdog.cyclo ./Core/Src/detect/fm_watchdog.d ./Core/Src/detect/fm_watchdog.o ./Core/Src/detect/fm_watchdog.su

.PHONY: clean-Core-2f-Src-2f-detect

