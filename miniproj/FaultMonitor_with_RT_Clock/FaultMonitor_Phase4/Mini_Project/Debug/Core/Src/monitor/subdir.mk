################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/monitor/fm_capture.c \
../Core/Src/monitor/fm_sigout.c \
../Core/Src/monitor/fm_trace.c \
../Core/Src/monitor/fm_window_task.c \
../Core/Src/monitor/ring_buffer.c 

OBJS += \
./Core/Src/monitor/fm_capture.o \
./Core/Src/monitor/fm_sigout.o \
./Core/Src/monitor/fm_trace.o \
./Core/Src/monitor/fm_window_task.o \
./Core/Src/monitor/ring_buffer.o 

C_DEPS += \
./Core/Src/monitor/fm_capture.d \
./Core/Src/monitor/fm_sigout.d \
./Core/Src/monitor/fm_trace.d \
./Core/Src/monitor/fm_window_task.d \
./Core/Src/monitor/ring_buffer.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/monitor/%.o Core/Src/monitor/%.su Core/Src/monitor/%.cyclo: ../Core/Src/monitor/%.c Core/Src/monitor/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_NUCLEO_64 -DUSE_HAL_DRIVER -DSTM32F446xx -c -I../Core/Inc -I../Core/Src/monitor -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -I../Drivers/BSP/STM32F4xx-Nucleo -I../Core/Src/app -I../Core/Src/inject -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src-2f-monitor

clean-Core-2f-Src-2f-monitor:
	-$(RM) ./Core/Src/monitor/fm_capture.cyclo ./Core/Src/monitor/fm_capture.d ./Core/Src/monitor/fm_capture.o ./Core/Src/monitor/fm_capture.su ./Core/Src/monitor/fm_sigout.cyclo ./Core/Src/monitor/fm_sigout.d ./Core/Src/monitor/fm_sigout.o ./Core/Src/monitor/fm_sigout.su ./Core/Src/monitor/fm_trace.cyclo ./Core/Src/monitor/fm_trace.d ./Core/Src/monitor/fm_trace.o ./Core/Src/monitor/fm_trace.su ./Core/Src/monitor/fm_window_task.cyclo ./Core/Src/monitor/fm_window_task.d ./Core/Src/monitor/fm_window_task.o ./Core/Src/monitor/fm_window_task.su ./Core/Src/monitor/ring_buffer.cyclo ./Core/Src/monitor/ring_buffer.d ./Core/Src/monitor/ring_buffer.o ./Core/Src/monitor/ring_buffer.su

.PHONY: clean-Core-2f-Src-2f-monitor

