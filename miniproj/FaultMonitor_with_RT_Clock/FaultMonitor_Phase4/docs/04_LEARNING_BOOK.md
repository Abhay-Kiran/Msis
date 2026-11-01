# 04 LEARNING BOOK: from zero to viva-ready

**Project:** Autonomous Firmware Fault Localization Using Runtime Execution Signatures
**Board:** STM32 Nucleo-F446RE (Cortex-M4F, 84 MHz from the internal oscillator through the PLL, 512 KB flash, 128 KB RAM)
**Software:** FreeRTOS through the CMSIS-RTOS v2 wrapper, STM32CubeIDE
**Readers:** Abhay (capture layer), Arfan (detection layer), Abhishek (application, injection, tools)
**Final evaluation:** 27 October 2026

How to use this book:

1. Read Part A slowly, one keyword at a time. Every keyword starts in plain words, then an everyday picture, then the engineering meaning, then where it lives in OUR code. If you can explain the plain-words line to a friend who is not an engineer, you understand it.
2. Read Part B (FreeRTOS) and Part C (the eight observed parameters) with the source code open next to you.
3. Part D is the heart: it tells the story of what happens inside the chip, first in normal operation and then for every fault we can inject. Practise telling these stories out loud.
4. Part E is the decision algorithm with the real numbers from the code.
5. Part F compares us with commercial tools. Part G is 60 viva questions. Part H is your personal 7-day plan.

A note on honesty. The README states that the firmware compiles cleanly and the detection logic passes PC unit tests, but it had not yet been run on a board when this was written. So every number in this book that comes from the code (thresholds, periods, priorities, sizes) is exact, while every number that describes a measurement (for example "the control task uses about 0.5 ms per window") is an estimate and is marked as such. Replace estimates with your own measurements from the `S` and `B` commands as soon as you have the board running. Examiners respect "we measured X" far more than "it should be X".

References for every topic are in `05_REFERENCES.md`. This book contains no web links.

File paths in this book are relative to `Mini_Project/Core/` unless stated otherwise, so `Src/detect/fm_detect.c` means `Mini_Project/Core/Src/detect/fm_detect.c`.

---

## Table of contents

- Part A: Keywords, in plain language first
- Part B: FreeRTOS as used in this project
- Part C: The eight observed parameters
- Part D: Inside the controller, millisecond by millisecond, and every fault story
- Part E: How the chip decides (the localizer algorithms with real thresholds)
- Part F: Our approach versus alternatives and market tools
- Part G: 60 interview and viva questions with model answers
- Part H: 7-day self-study plan for each of you

---

# Part A: Keywords, in plain language first

Format for every keyword:

- **Plain:** what it means in everyday words
- **Picture:** an everyday analogy
- **Technical:** the engineering meaning
- **In our project:** where you can see it in the code

## A.1 The big picture words

### Framework vs application

- **Plain:** The application is the program that does the useful job. The framework is a separate helper that watches the application and steps in when something goes wrong.
- **Picture:** A patient and a doctor. The patient (application) lives their life. The doctor (framework) takes the pulse, knows what "healthy" looks like for this patient, notices when something is off, names the organ that is sick, writes it in the medical record, and gives treatment. The doctor does not need to know the patient's job; the same doctor can treat a different patient.
- **Technical:** The framework (`Src/monitor/`, `Src/detect/`, `Inc/fm_types.h`) knows the application only as numbered components (0 to 15) and numbered stages (0 to 7). The application registers itself at boot with `fm_register_task()`, `fm_register_isr()`, `fm_register_periph()` and `fm_register_stage()`. Nothing in `detect/` mentions "PID" or "ADC": you could replace the temperature controller with a motor controller and only `Src/app/fm_common.h` and the registration calls change.
- **In our project:** `Inc/fm_types.h` (the shared contract), `Src/app/fm_common.h` (what the numbers mean for this application), `Src/monitor/fm_trace.h` (the registration API). The build switch `FM_ENABLE_MONITOR=0` removes the whole doctor and leaves the patient running alone, which is how we measure the doctor's cost.

### Firmware

- **Plain:** The software that is permanently stored inside a device and runs it.
- **Picture:** The habits and reflexes built into a person, as opposed to an app you install and remove.
- **Technical:** The compiled machine code (plus constant data) stored in the microcontroller's flash memory, executed directly by the CPU (central processing unit) after reset, usually without an operating system underneath or with a small real-time kernel.
- **In our project:** Everything under `Mini_Project/Core/` plus the ST HAL (hardware abstraction layer) drivers and FreeRTOS in `Drivers/` and `Middlewares/`. It is built into `Mini_Project.elf` / `.bin` and flashed into sectors 0 to 5 of flash.

### Microcontroller (MCU)

- **Plain:** A whole small computer on one chip: processor, memory and input/output circuits together.
- **Picture:** A studio apartment: kitchen, bed and desk in one room. A PC is a house with separate rooms (CPU chip, RAM sticks, disk, cards).
- **Technical:** Our MCU is the STM32F446RE: an Arm Cortex-M4F core (the F means it has a single-precision floating-point unit, FPU), 512 KB flash, 128 KB SRAM (static RAM), and many peripherals (timers, ADC, DAC, DMA, USART, GPIO, watchdogs) on internal buses.
- **In our project:** Clock, memory sizes and peripheral addresses come from the CMSIS (Cortex Microcontroller Software Interface Standard) device header `stm32f446xx.h`, which our code uses directly (`ADC1->CR2`, `TIM2->PSC`, `RCC->CSR` and so on).

### Register

- **Plain:** A tiny named storage box inside the chip. Writing a number into it changes how the hardware behaves; reading it tells you what the hardware is doing.
- **Picture:** The switches and indicator lights on a control panel. Flip a switch (write a bit) and a machine starts; look at a light (read a bit) and you know it finished.
- **Technical:** A 32-bit location at a fixed address. CPU registers (R0 to R15, PSR) live inside the core. Peripheral registers are memory-mapped: `TIM2->PSC` is simply the 32-bit word at address 0x40000028, which is why the examiner command `W 40000028 53` changes the TIM2 prescaler.
- **In our project:** `Src/app/app_periph.c` configures ADC, DMA, DAC and TIM2 by writing registers directly (no HAL drivers for those). `Src/monitor/fm_capture.c` reads the fault status registers (CFSR, HFSR, MMFAR, BFAR) and the reset register (RCC_CSR).

### Memory map

- **Plain:** The list of which addresses hold what: program, data, hardware controls.
- **Picture:** A city map with districts: "addresses 0x0800xxxx are the library (flash), 0x2000xxxx are the workshop (RAM), 0x4000xxxx are the factory controls (peripherals)".
- **Technical:** On the F446: flash starts at 0x0800_0000 (also aliased at 0x0000_0000 at boot), SRAM at 0x2000_0000, peripherals at 0x4000_0000 and up, Cortex-M system registers (NVIC, SCB, MPU, DWT, SysTick) at 0xE000_0000 and up.
- **In our project:** `Inc/fm_types.h` defines our own flash layout:

```
0x0800_0000  sectors 0-5 (256 KB)  firmware (linker FLASH length cut to 256K)
0x0804_0000  sector 6   (128 KB)   learned baseline      FM_FLASH_BASELINE_ADDR
0x0806_0000  sector 7   (128 KB)   fault black box        FM_FLASH_LOG_ADDR
0x2000_0000  RAM 128 KB: .data, .bss, .noinit, heap (FreeRTOS heap 32 KB inside .bss), main stack
0x0000_0000  1 KB "no access" MPU region: the NULL-pointer trap
```

### Peripheral

- **Plain:** A hardware helper inside the chip that does one job (counting time, measuring voltage, sending serial data) without the CPU doing it step by step.
- **Picture:** Kitchen appliances. You set the oven timer and walk away; it beeps (interrupt) when done.
- **Technical:** A block on the AHB/APB (Advanced High-performance Bus / Advanced Peripheral Bus) controlled through its registers, clocked through RCC (reset and clock control) enable bits, usually able to raise interrupts and DMA requests.
- **In our project:** TIM2 (PWM and ADC trigger), ADC1 (analog input), DMA2 Stream 0 (moves ADC results), DAC1 (analog output), TIM6 (1 kHz application tick), USART2 (console), IWDG (watchdog), TIM1 (the HAL's own 1 ms time base, separate from the FreeRTOS tick). Four of them are monitored components: ADC1 (id 9), TIM2 (id 10), DAC1 (id 11), USART2 (id 12).

### Clock tree

- **Plain:** How the chip makes all its "heartbeats" from one source and divides them for different parts.
- **Picture:** A drummer (oscillator) and gearboxes that turn one beat into faster or slower beats for each musician.
- **Technical:** In `SystemClock_Config()` (`Src/main.c`): HSI (high-speed internal) 16 MHz RC oscillator -> PLL (phase-locked loop) with M=16, N=336, P=4: 16/16 x 336 / 4 = 84 MHz system clock (SYSCLK = HCLK = CPU clock). APB1 = HCLK/2 = 42 MHz, but timers on APB1 get x2 = 84 MHz. APB2 = 84 MHz. ADC clock = PCLK2/4 = 21 MHz (set by `ADC->CCR` ADCPRE in `app_periph.c`). The IWDG runs from the separate LSI (low-speed internal) oscillator, about 32 kHz.
- **In our project:** Every time conversion uses this: `cycles_per_ms = SystemCoreClock/1000 = 84,000`, one 100 ms window = 8,400,000 cycles. TIM2: 84 MHz / (839+1) = 100 kHz, / 1000 = 100 Hz. TIM6: 84 MHz / 8400 / 10 = 1 kHz. The baseline stores `core_hz` and is rejected at load if the clock changed (`fm_bb_load_baseline`).

### GPIO (general-purpose input/output)

- **Plain:** A pin that software can set high/low, read, or hand over to a peripheral.
- **Picture:** A light switch that can be wired either to your hand (software) or to an automatic timer (peripheral).
- **Technical:** Each pin has mode bits in `GPIOx->MODER` (input, output, alternate function, analog) and an alternate-function selector in `GPIOx->AFR`.
- **In our project:** PA4 is analog mode (it is both DAC output and ADC input, which closes our loop), PA5 is alternate function 1 = TIM2 channel 1 (the LED LD2 brightness is the heater power), PA2/PA3 are USART2 TX/RX to the ST-LINK virtual COM port, PC13 is the user button B1 (external interrupt). See `gpio_init()` and `app_pwm_pin_init()` in `Src/app/app_periph.c`.

### Timer and PWM (pulse-width modulation)

- **Plain:** A timer counts clock ticks and does something when it reaches a number. PWM switches a pin on and off very fast; the fraction of "on" time sets the average power.
- **Picture:** A metronome (timer). Dimming a lamp by flicking it on and off faster than the eye sees: 30% on time looks 30% bright (PWM).
- **Technical:** TIM2 counts at 100 kHz (PSC = 839) up to ARR = 999, so it overflows ("update event") 100 times a second. Channel 1 in PWM mode 1 drives PA5 high while the counter is below CCR1 (0 to 1000 = 0 to 100% duty). The update event is also routed to TRGO (trigger output, `CR2.MMS = 2`) which starts the ADC.
- **In our project:** `tim2_config()`, `app_pwm_set()` in `Src/app/app_periph.c`. TIM6 is a basic timer that only interrupts (1 kHz): our application tick and the fault injector's clock (`app_tick_isr()`, `fault_inject_on_tick()`).

### ADC (analog-to-digital converter)

- **Plain:** Turns a voltage into a number.
- **Picture:** A thermometer reader who looks at the mercury and writes down a number.
- **Technical:** ADC1 is 12-bit (0 to 4095 for 0 to VDDA, about 3.3 V). We use scan mode with two conversions: channel 4 (PA4, the plant output) with 84-cycle sampling, and channel 17 (VREFINT, the internal reference, about 1.2 V) with 480-cycle sampling. Conversions start on the rising edge of TIM2 TRGO (`EXTSEL = 6`, `EXTEN = 1`).
- **In our project:** `adc_dma_config()` in `Src/app/app_periph.c`. VREFINT is a health check: a plausible raw value is 1200 to 1800 (`APP_VREFINT_LO/HI`); outside that, the control task reports a peripheral error (`APP_PERR_VREF`).

### DAC (digital-to-analog converter)

- **Plain:** Turns a number into a voltage. The reverse of an ADC.
- **Picture:** A volume knob turned by a robot to an exact position.
- **Technical:** DAC1 channel 1 on PA4, 12-bit, output buffer on, no trigger: writing `DAC->DHR12R1` updates the pin almost immediately.
- **In our project:** `dac_config()`, `app_dac_set()`. The control task writes the simulated plant temperature (0 to 100 C mapped to 0 to 4095). Because PA4 is also ADC channel 4, the next ADC scan measures that real voltage, noise and all. That is our hardware-in-the-loop.

### DMA (direct memory access)

- **Plain:** A hardware mover that copies data between a peripheral and memory without the CPU.
- **Picture:** A conveyor belt from the factory machine to the warehouse; the manager (CPU) is only told when a full box arrives.
- **Technical:** DMA2 Stream 0, channel 0 is wired to ADC1. Configured circular, 2 half-words, memory increment, transfer-complete and transfer-error interrupts on. After both conversions are copied into `s_dma_buf[2]` the stream raises its interrupt.
- **In our project:** `adc_dma_config()` and `DMA2_Stream0_IRQHandler()` in `Src/app/app_periph.c`. A transfer error (`TEIF0`) becomes `fm_periph_error(COMP_PER_ADC, APP_PERR_DMA_TE)`. The DMA registers CR, PAR and M0AR are part of the ADC1 configuration checksum.

### UART (universal asynchronous receiver-transmitter)

- **Plain:** A two-wire serial "telephone line" that sends bytes one bit at a time.
- **Picture:** Morse code at an agreed speed: both sides must use the same speed (baud rate) and the same letter format.
- **Technical:** USART2 (the "S" adds a synchronous mode we do not use) at 115200 baud, 8 data bits, no parity, 1 stop bit (8-N-1). Each byte takes about 87 us. Error flags: ORE (overrun: a new byte arrived before the old one was read), FE (framing: stop bit missing), NE (noise), PE (parity).
- **In our project:** `Src/inject/fm_uart.c`: interrupt-driven receive into a FreeRTOS queue, interrupt-driven transmit with a mutex and a semaphore. `HAL_UART_ErrorCallback()` turns ORE/FE/NE/PE into `fm_comm_error()`. The shell accepts checksummed frames `$<cmd>*hh`; a wrong checksum is also a communication error (`FM_COMM_CRC`).

### Interrupt, ISR, NVIC, priority

- **Plain:** An interrupt is a doorbell: hardware stops the CPU's current work, runs a short handler (the ISR, interrupt service routine), then resumes exactly where it was. The NVIC (nested vectored interrupt controller) is the receptionist who decides which doorbell is answered first.
- **Picture:** You are reading (main program). The doorbell rings (interrupt). You put a bookmark (the CPU saves registers), answer the door (ISR), come back to the same line. If the fire alarm rings while you are at the door, you handle the fire alarm first (higher priority preempts lower).
- **Technical:** On Cortex-M the hardware automatically pushes R0-R3, R12, LR, PC, xPSR (and FPU registers if used) onto the current stack, loads the handler address from the vector table, and on return pops them. The STM32F4 implements 4 priority bits: 0 (most urgent) to 15 (least). A lower number preempts a higher number.
- **In our project:** Priorities (from `stm32f4xx_hal_msp.c`, `app_periph.c`, `FreeRTOSConfig.h`, `stm32f4xx_hal_conf.h`):

| interrupt | priority | handler / file | monitored as |
|---|---|---|---|
| TIM6 (1 kHz app tick, injector) | 5 | `TIM6_DAC_IRQHandler`, `stm32f4xx_it.c` -> `HAL_TIM_PeriodElapsedCallback`, `main.c` | `tim6_isr` (6) |
| USART2 (per byte) | 5 | `USART2_IRQHandler`, `stm32f4xx_it.c` | `uart_isr` (7) |
| DMA2 Stream 0 (ADC done) | 6 | `DMA2_Stream0_IRQHandler`, `app_periph.c` | `adc_isr` (5) |
| EXTI 15-10 (button) | set by the BSP button init (the guidebook lists 15) | `stm32f4xx_it.c` -> `HAL_GPIO_EXTI_Callback`, `controller.c` | `btn_isr` (8) |
| TIM1 (HAL 1 ms time base) | 15 (`TICK_INT_PRIORITY`) | `stm32f4xx_hal_timebase_tim.c` | not monitored |
| SysTick, PendSV (FreeRTOS) | 15 (kernel priority) | FreeRTOS port | not monitored |

Every monitored ISR is wrapped by `FM_ISR_ENTER(id)` / `FM_ISR_EXIT(id)`.

### Exception

- **Plain:** Any event that makes the CPU leave normal code and run a special handler. Interrupts from peripherals are one kind; errors detected by the CPU itself are another.
- **Picture:** Doorbells (interrupts) and smoke alarms (faults) both make you stop reading.
- **Technical:** Cortex-M exceptions 1 to 15 are system exceptions: Reset, NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, DebugMonitor, PendSV, SysTick. Numbers 16 and up are peripheral interrupts (IRQs).
- **In our project:** We hook four fault exceptions in `Src/stm32f4xx_it.c` (HardFault, MemManage, BusFault, UsageFault) and FreeRTOS uses SVCall (to start the first task), PendSV (context switch) and SysTick (the tick).

### Stack and heap

- **Plain:** The stack is a pile of notepads for "what am I doing right now" (local variables, return addresses). The heap is a shared storeroom where you borrow boxes of memory and give them back later.
- **Picture:** Stack: a pile of plates; you can only add or remove the top one. Heap: a library lending books of different sizes.
- **Technical:** On Cortex-M the stack grows downward (towards lower addresses). There are two stack pointers: MSP (main stack pointer, used by main() before the scheduler and by all exceptions) and PSP (process stack pointer, used by FreeRTOS tasks). Each FreeRTOS task has its own stack, allocated from the FreeRTOS heap (`configTOTAL_HEAP_SIZE` = 32 KB) when the task is created.
- **In our project:** Task stack sizes (words of 4 bytes): ctrl 384, report 512, shell 512, sigout 512, monitor 1024, wdg 192. FreeRTOS fills new stacks with 0xA5 bytes; `fm_stack_free_words()` (`fm_capture.c`) counts how many words are still 0xA5A5A5A5 above the guard: the free-stack high-water mark. Big structures in the monitor are `static` on purpose ("far too big for a task stack", `fm_monitor.c`).

### Flash vs RAM

- **Plain:** Flash keeps data when power is off but is slow and awkward to write. RAM is fast and easy to write but forgets everything at power-off.
- **Picture:** Flash is a notebook written in pen (permanent, and to change a page you must tear out the whole chapter). RAM is a whiteboard.
- **Technical:** STM32F4 flash can only change bits from 1 to 0 when programming; going back to 1 needs a sector erase (our sectors 6 and 7 are 128 KB each and take on the order of a second or more to erase, during which code cannot run from flash). Programming here is done 32 bits at a time (voltage range 3).
- **In our project:** `Src/detect/fm_blackbox.c`. The baseline goes to sector 6, fault reports are appended to sector 7 in 256-byte slots. A slot is "free" while its first word reads 0xFFFFFFFF (erased). Every record carries a CRC-32 so a half-written slot (power cut) is ignored. Because erases stall the CPU, they only happen at moments the monitor chooses, followed by a grace period.

### Linker script and sections (.text .data .bss .noinit)

- **Plain:** The linker script is the seating plan that says which part of the program goes where in memory.
- **Picture:** A removal company's plan: "books (code) in the library, today's notes (variables with a starting value) copied from the library to the desk at start, empty folders (zeroed variables) on the desk, and one locked drawer (noinit) that nobody empties when the office reopens."
- **Technical:**
  - `.text`: machine code and constants, in flash.
  - `.data`: initialised global variables; stored in flash, copied to RAM by the startup code.
  - `.bss`: zero-initialised globals; the startup code fills them with zero.
  - `.noinit`: our addition; `NOLOAD`, not copied, not zeroed, so its contents survive a reset (not a power cycle).
- **In our project:** `Mini_Project/STM32F446RETX_FLASH.ld`: FLASH length cut to 256K (so code can never be placed in sectors 6 and 7 that we erase), and the `.noinit` section. Two objects live there: `g_fm_noinit` (the crash record, `fm_capture.c`) and `g_fm_rb` (the 1024-event ring buffer, `fm_trace.c`). After a power-on reset `fm_capture_boot()` sees POR/BOR or a wrong magic number and wipes the record, because RAM content is random after power-up.

## A.2 RTOS words

### RTOS (real-time operating system)

- **Plain:** A small "manager" program that lets several jobs share one CPU and guarantees urgent jobs get the CPU on time.
- **Picture:** A head chef who decides which cook uses the single stove, always giving it first to the most urgent dish.
- **Technical:** "Real-time" means correctness depends on timing, not just on the result. An RTOS provides tasks, a priority-based preemptive scheduler, time services (delays), and synchronisation (queues, semaphores, mutexes, notifications). FreeRTOS is a small open-source kernel; CMSIS-RTOS v2 is Arm's standard API that STM32CubeIDE wraps around it (`osThreadNew`, `osDelay`, `osMutexNew`...).
- **In our project:** `Inc/FreeRTOSConfig.h`, `Middlewares/Third_Party/FreeRTOS`. Our code mostly calls CMSIS-RTOS v2 functions and a few native FreeRTOS ones (`ulTaskNotifyTake`, `vTaskNotifyGiveFromISR`, `vTaskSetTaskNumber`, `vTaskGetInfo`).

### Task

- **Plain:** One independent job with its own loop, its own notepad (stack) and its own priority.
- **Picture:** One cook with one recipe.
- **Technical:** A FreeRTOS task is a C function that never returns plus a TCB (task control block: stack pointer, priority, state, name, a user number) plus its stack. States: Running, Ready, Blocked (waiting for time or an event), Suspended.
- **In our project:** ctrl, report, shell (application), monitor, wdg, sigout (framework). The full table is in Part B.

### Scheduler

- **Plain:** The part of the RTOS that decides which task runs next.
- **Picture:** The head chef pointing at the next cook.
- **Technical:** FreeRTOS always runs the highest-priority task that is Ready. Among equal priorities it rotates at each tick (time slicing, on by default).
- **In our project:** `configUSE_PREEMPTION = 1`, `configMAX_PRIORITIES = 56`. CMSIS priorities map to numbers: Low 8, Normal 24, AboveNormal 32, High 40, Realtime 48.

### Preemption

- **Plain:** A more urgent job can interrupt a less urgent job immediately, without waiting for it to finish.
- **Picture:** An ambulance gets through the junction first even if a car was already moving.
- **Technical:** When a higher-priority task becomes Ready (from an ISR, a tick, or another task), the kernel pends a context switch, and the current task is switched out mid-instruction stream.
- **In our project:** When the DMA ISR gives the control task a notification, ctrl (32) preempts report (24) or shell (8) right away. When the monitor's 100 ms delay expires, the monitor (40) preempts ctrl (32), even if ctrl is stuck in an infinite loop. That is the key to detecting stuck loops.

### Priority

- **Plain:** How urgent a job is.
- **Picture:** Hospital triage colours.
- **Technical:** Two separate priority systems exist and students often mix them up: task priorities (FreeRTOS, higher number = more urgent) and interrupt priorities (NVIC, lower number = more urgent). Any interrupt, even the least urgent, beats any task.
- **In our project:** The detector stores task priorities (`uxTaskPriorityGet`) and gives ISRs and peripherals the value 0xFF ("above every task") in `fm_register_isr` / `fm_register_periph`. Victim suppression uses these numbers.

### Context switch

- **Plain:** Saving everything about the current job and loading everything about the next job, so each job continues as if never interrupted.
- **Picture:** Putting a bookmark in one book and opening another at its bookmark.
- **Technical:** On Cortex-M4 FreeRTOS does it in the PendSV handler: hardware already pushed R0-R3, R12, LR, PC, xPSR on the task stack; the handler pushes R4-R11 and EXC_RETURN (and S16-S31 if the task used the FPU), saves PSP into the TCB, calls `vTaskSwitchContext()` to choose the next task, then restores the new task's registers and PSP. Details in Part B.
- **In our project:** The trace macros `traceTASK_SWITCHED_OUT()` and `traceTASK_SWITCHED_IN()` run inside `vTaskSwitchContext()` and call `fm_hook_task_out()` / `fm_hook_task_in()`. Every context switch is therefore a measurement point.

### Tick

- **Plain:** The RTOS's regular heartbeat, used for delays and time slicing.
- **Picture:** A clock that ticks every millisecond; "wake me in 100 ticks" means 100 ms.
- **Technical:** SysTick interrupts at `configTICK_RATE_HZ = 1000`. Each tick the kernel increments its tick count and moves tasks whose delay expired to Ready.
- **In our project:** `osDelay`, `osDelayUntil`, `osKernelGetTickCount` all use this 1 ms tick. The 100 ms window, the 250 ms watchdog feed period, the 10 s escalation window are all tick-based. Note: we have TWO other 1 ms-ish clocks: TIM1 for the HAL (`HAL_GetTick`) and TIM6 for the application (`app_ms()`). Fine timing uses neither; it uses the DWT cycle counter.

### Queue

- **Plain:** A thread-safe waiting line of messages between jobs.
- **Picture:** A post box: the sender drops letters in, the receiver takes them out in order; if it is empty the receiver can wait by the box.
- **Technical:** A FreeRTOS queue copies items by value into a fixed-size buffer; senders and receivers can block with a timeout; ISRs use the FromISR variant.
- **In our project:** UART receive queue of 96 bytes (`fm_uart.c`: ISR puts, shell task gets with a 100 ms timeout). The signature printer queue of 4 `fm_pub_t` items (`fm_sigout.c`: monitor puts with zero wait and counts drops, the low-priority printer gets).

### Mutex

- **Plain:** A key to a shared room: only the holder may enter; others wait.
- **Picture:** The single bathroom key at a petrol station.
- **Technical:** A binary lock with an owner; FreeRTOS mutexes implement priority inheritance.
- **In our project:** `s_latest_mtx` in `fm_sigout.c` protects the latest window copy used by `S`. The UART's line lock (`s_tx_mutex` in `fm_uart.c`) was a mutex at first and is now a binary semaphore; B.6 explains why.

### Priority inversion and inheritance

- **Plain:** Inversion: an urgent job is stuck waiting for a key held by an unimportant job, while medium jobs keep the unimportant one from ever finishing. Inheritance: while the unimportant job holds the key, it temporarily gets the urgent job's rank so it finishes quickly.
- **Picture:** The CEO waits for the bathroom key that an intern holds, but the intern keeps being sent on errands by managers. Fix: while the CEO is waiting, the intern is treated as the CEO, so no manager can stop them.
- **Technical:** Classic case (and the famous Mars Pathfinder reset story): low task L holds mutex M; high task H blocks on M; medium task K preempts L indefinitely; H misses deadlines. With inheritance, L runs at H's priority until it releases M.
- **In our project:** The report task (24) or the shell (8) may hold the UART line lock when the monitor (40) wants to print a `!FAULT` line. We deliberately use a lock *without* inheritance there (B.6), so ctrl (32) can delay the holder; the wait is bounded by one line (about 22 ms), which is fine for console output. Inheritance matters where the wait is unbounded and a deadline is at stake.

### Task notification

- **Plain:** A lightweight "wake up, there is work" signal sent directly to one specific task.
- **Picture:** Tapping a colleague's shoulder instead of putting a letter in the shared post box.
- **Technical:** Each TCB holds a notification value. `vTaskNotifyGiveFromISR()` increments it and readies the task; `ulTaskNotifyTake(pdTRUE, timeout)` waits for it and clears it. Faster and smaller than a semaphore.
- **In our project:** The DMA ISR wakes the control task this way (`app_periph.c` and `controller.c`). The 50 ms timeout in `ulTaskNotifyTake` (5 missed ADC periods) is itself a fault signal: `fm_periph_error(COMP_PER_ADC, APP_PERR_TIMEOUT)`.

### Semaphore

- **Plain:** A counter of available "tokens"; taking one when none is left means waiting.
- **Picture:** A car park display showing free spaces; at zero you queue at the gate.
- **Technical:** Binary (0/1) or counting. Unlike a mutex there is no owner and no inheritance, so it is the right tool for "event happened" signalling, often from an ISR.
- **In our project:** `s_tx_done` (`fm_uart.c`): the UART transmit-complete ISR releases it, the printing task waits on it. So the task sleeps while the bytes go out instead of busy-waiting, which matters because a busy-wait would look exactly like a stuck loop to our own detector.

### Idle task and idle hook

- **Plain:** The job that runs when nobody else wants the CPU. The hook is a small function we attach to it.
- **Picture:** A cook who sweeps the floor whenever no dish is cooking; counting sweeps tells you how idle the kitchen was.
- **Technical:** FreeRTOS creates the idle task at priority 0. With `configUSE_IDLE_HOOK = 1`, `vApplicationIdleHook()` is called in its loop.
- **In our project:** `main.c` defines `vApplicationIdleHook()` calling `idle_stats_on_idle_hook()`, which increments a counter (`Src/app/idle_stats.c`). The `IDLE,<count>,<uptime_ms>` line of `S` compared between the monitor-ON and monitor-OFF builds estimates CPU overhead: overhead percent is about 100 x (idle_off - idle_on) / idle_off (in rate per ms).

### Trace hooks

- **Plain:** Empty "slots" the RTOS authors left in the kernel where you can plug in your own measuring code.
- **Picture:** A turnstile counter at a stadium gate: the gate works the same, but now every passage is counted.
- **Technical:** FreeRTOS calls macros such as `traceTASK_SWITCHED_IN()` and `traceTASK_SWITCHED_OUT()` at defined points; by default they are empty. Defining them in `FreeRTOSConfig.h` injects code into the kernel without editing kernel files.
- **In our project:** `Inc/FreeRTOSConfig.h`, USER CODE Defines:

```c
#define traceTASK_SWITCHED_IN()   fm_hook_task_in((uint32_t)pxCurrentTCB->uxTaskNumber)
#define traceTASK_SWITCHED_OUT()  fm_hook_task_out((uint32_t)pxCurrentTCB->uxTaskNumber)
```

### DWT cycle counter

- **Plain:** A stopwatch built into the CPU that counts every clock cycle.
- **Picture:** A car's odometer that counts every wheel turn.
- **Technical:** The DWT (data watchpoint and trace) unit has CYCCNT, a 32-bit counter incremented every CPU cycle once enabled (`CoreDebug->DEMCR.TRCENA` and `DWT->CTRL.CYCCNTENA`). At 84 MHz one count is about 11.9 ns, and it wraps every 2^32 / 84e6, about 51 s. Unsigned subtraction (`now - then`) gives the right answer across one wrap.
- **In our project:** Enabled in `fm_trace_init()` and also in `fault_inject_init()` (so the monitor-OFF build still has timestamps). Every event's `cycle_ts`, every task slice, every ISR duration and gap is measured with it.

### MPU (memory protection unit)

- **Plain:** A hardware guard that checks every memory access against rules ("this area is read-only", "nobody may touch this area") and raises an alarm on violation.
- **Picture:** Security guards at certain doors in a building; most doors are open, but a few are "staff only" or "do not enter".
- **Technical:** The Cortex-M4 MPU has 8 regions. Each has a base address, a size (power of two, at least 32 bytes, base aligned to size), access permissions (AP) and an execute-never bit (XN). With `PRIVDEFENA` set, addresses not covered by any region keep the default map. A violation raises MemManage.
- **In our project:** `fm_mpu_init()`, `fm_mpu_protect_ro()`, `fm_mpu_guard_task()` in `Src/monitor/fm_capture.c`:

| region | what | size | permission |
|---|---|---|---|
| 0 | NULL guard at 0x0000_0000 | 1 KB | no access, XN |
| 1 | `s_cfg` controller gains (`controller.c`) | 32 bytes | read-only, XN |
| 2 to 7 | stack guard per registered task | 32 bytes each | no access, XN |

The vector table offset (VTOR) is pointed at flash 0x0800_0000 so the NULL guard cannot interfere with exception entry.

### HardFault, MemManage, BusFault, UsageFault

- **Plain:** The four "smoke alarms" of the Cortex-M4 for software errors.
- **Picture:** MemManage = you walked into a forbidden room. BusFault = the room does not exist or the door is broken. UsageFault = you tried an impossible action (divide by zero, unknown instruction). HardFault = the general alarm when a specific alarm is off or when an alarm happens while handling an alarm.
- **Technical:** MemManage: MPU violation or execute from XN memory. BusFault: error response from the bus (invalid address, peripheral not clocked). UsageFault: undefined instruction, invalid state, unaligned access (if trapping enabled), divide by zero (if `CCR.DIV_0_TRP`). HardFault: escalation target when the specific fault is disabled or cannot be taken.
- **In our project:** `fm_mpu_init()` enables MemManage, BusFault and UsageFault in `SCB->SHCSR` so they do not all become HardFault (so we know which kind it was), and enables divide-by-zero trapping. All four handlers in `stm32f4xx_it.c` call `fm_trap(kind, exc_return)`.

### CFSR, MMFAR, BFAR (and HFSR)

- **Plain:** The "incident report" registers that tell you exactly what went wrong and where.
- **Picture:** The black-box flight recorder's last frame: what failed and the address involved.
- **Technical:** CFSR (configurable fault status register) is three registers in one: MMFSR (bits 0-7: IACCVIOL, DACCVIOL, MUNSTKERR, MSTKERR, MLSPERR, MMARVALID), BFSR (bits 8-15: IBUSERR, PRECISERR, IMPRECISERR, UNSTKERR, STKERR, LSPERR, BFARVALID), UFSR (bits 16-31: UNDEFINSTR, INVSTATE, INVPC, NOCP, UNALIGNED, DIVBYZERO). MMFAR holds the faulting address when MMARVALID is set; BFAR when BFARVALID is set. HFSR (HardFault status) tells whether a fault was escalated (FORCED).
- **In our project:** `fm_trap()` saves CFSR, HFSR, and MMFAR or BFAR into `g_fm_noinit`. A data write into an MPU region typically gives CFSR = 0x82 (DACCVIOL plus MMARVALID). After reboot the monitor prints `!TRAP before reset: trap=... cfsr=... hfsr=... addr=... lr=...`.

### EXC_RETURN

- **Plain:** A special "return ticket" value placed in the link register when an exception starts. It says which stack to go back to and whether floating-point registers were saved.
- **Picture:** A cloakroom ticket that says which cloakroom (main or task) holds your coat and whether it is a big coat (FPU context) or a small one.
- **Technical:** On exception entry LR is set to 0xFFFFFFxx. Bit 2 = 1: return to thread mode using PSP (a task was running). Bit 2 = 0: MSP (main code or another handler). Bit 4 = 0: an extended frame with FPU registers was stacked. Common values: 0xFFFFFFFD (task, no FPU), 0xFFFFFFED (task, FPU), 0xFFFFFFF9 (thread using MSP), 0xFFFFFFF1 (from another handler).
- **In our project:** Each fault handler reads LR with `mov %0, lr` and passes it to `fm_trap()`. If bit 2 is set and there was no stacking error, `fm_trap()` reads the stacked PC and LR from the PSP frame (frame[6], frame[5]): the exact instruction that faulted. `fm_task_pc()` uses the same bit 4 logic on a saved task context.

### Watchdog (IWDG)

- **Plain:** A dead-man's switch: a hardware timer that resets the chip unless software keeps "feeding" it.
- **Picture:** A train driver must press a pedal every few seconds; if they faint, the train stops by itself.
- **Technical:** The IWDG (independent watchdog) is a 12-bit down-counter clocked by the LSI, independent of the main clock and of the CPU. Write 0xCCCC to start, 0x5555 to unlock PR/RLR, 0xAAAA to reload. Once started, software cannot stop it. Nothing in software, not even interrupts disabled forever, can prevent its reset.
- **In our project:** `Src/detect/fm_watchdog.c`: prescaler /64 (PR = 4) gives 500 Hz, reload 2000 gives 4 s. Frozen while the debugger halts the core (`DBGMCU_APB1_FZ_DBG_IWDG_STOP`). The feeder task feeds every 250 ms, but only if the monitor checked in within the last 1500 ms.

### Reset causes (RCC_CSR)

- **Plain:** After a restart, the chip remembers why it restarted.
- **Picture:** Waking up and finding a note: "you fainted" (watchdog), "someone pressed the button", "the power went off".
- **Technical:** `RCC->CSR` flags: LPWRRSTF (low-power), WWDGRSTF (window watchdog), IWDGRSTF (independent watchdog), SFTRSTF (software, `NVIC_SystemReset`), PORRSTF (power-on), PINRSTF (NRST pin; set by every reset because the chip drives the pin), BORRSTF (brown-out). Writing RMVF clears them.
- **In our project:** `fm_capture_boot()` (called right after `HAL_Init()`) decodes and clears them into `FM_RST_*` flags. `fm_reset_cause_str()` reports PIN only when nothing more specific is set. The README's first boot shows `power-on (0x03)` = POR + PIN.

### CRC (cyclic redundancy check)

- **Plain:** A short fingerprint computed from a block of data; if one bit changes, the fingerprint almost certainly changes.
- **Picture:** The check digit on an ID number that catches typing mistakes.
- **Technical:** We use CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), computed bit by bit in software by `fm_crc32()` (`fm_trace.c`). (The F446 also has a hardware CRC unit; we do not use it, which keeps the framework portable.)
- **In our project:** Three uses: (1) the configuration fingerprint of each peripheral (masked registers -> `periph_hash`), (2) the integrity check of the baseline in sector 6, (3) the integrity check of each fault report in sector 7. A separate tiny XOR checksum (NMEA style) protects shell frames and telemetry lines.

## A.3 Detection and statistics words

### Signature, feature, window

- **Plain:** A window is a fixed slice of time (100 ms). A feature is one number measured in that slice (for example "how many cycles did the control task use"). The signature is the full list of features for one window: a fingerprint of how the firmware behaved.
- **Picture:** A fitness tracker summary every 100 ms: steps (runs), active minutes (CPU cycles), longest sprint (max slice), longest rest (max gap), and so on, for every family member (component).
- **Technical:** `sig_vector_t` in `Inc/fm_types.h`: for each of 16 components `comp_sig_t {runs, stack_free_min, cpu_cycles, max_slice, max_gap}`; an 8x8 matrix `trans[i][j]` of stage transitions; error counters; `periph_hash[]` and `periph_err[]` per component. `FM_WINDOW_MS = 100`.
- **In our project:** Counters are updated live by the hooks in `fm_trace.c`; `fm_sig_close_window()` copies them into a `sig_vector_t` and zeroes them, every 100 ms, from the monitor task. `V 1` streams each one as a CSV line (`SIG,...`).

### Baseline

- **Plain:** A learned description of "normal", made by watching the healthy system for a while.
- **Picture:** Your doctor's record of your usual pulse and blood pressure, so they can tell when today's numbers are unusual for YOU.
- **Technical:** `baseline_t`: per component mean, standard deviation and maximum of runs, cpu_cycles and max_slice; the largest gap ever seen; the lowest free stack ever seen; the set of allowed stage transitions (bit masks); the expected configuration CRC of each peripheral; plus magic, version, `core_hz`, number of windows and a CRC.
- **In our project:** Learned on the chip by `fm_learn.c` from 100 windows (10 s) by default (`L [n]`, minimum 20). Saved to flash sector 6 by `fm_bb_save_baseline()`, reloaded at every boot, printed by `B`.

### Mean, standard deviation, Welford

- **Plain:** Mean = the average. Standard deviation (sigma) = how much values usually wander from the average. Welford = a clever way to compute both while the numbers stream in, without storing them.
- **Picture:** Average commute time is 30 minutes, usually give or take 3 minutes (sigma). Welford is updating that average in your head every day instead of keeping a diary of every commute.
- **Technical:** Welford's online algorithm, per new sample x: n = n+1; d = x - mean; mean = mean + d/n; M2 = M2 + d x (x - mean). Sample variance = M2/(n-1). It is numerically stable in 32-bit float (the naive "sum of squares minus square of sum" loses precision badly with numbers in the millions of cycles).
- **In our project:** `fm_welford_add()` and `fm_welford_stat()` in `Src/detect/fm_learn.c`. Memory cost is constant: 16 bytes per feature, whatever the number of windows.

### z-score

- **Plain:** How many "usual wobbles" away from normal a value is.
- **Picture:** Your commute took 45 minutes: that is (45-30)/3 = 5 standard deviations late. Very unusual.
- **Technical:** z = (x - mean) / sigma. |z| below about 3 is common for bell-shaped data; |z| of 4 or more is rare.
- **In our project:** `zsq()` in `fm_detect.c` computes z squared with two protections: sigma is floored at max(sigma, 5% of |mean|, 1.0) so a perfectly steady feature does not produce infinite z, and z is clamped to plus or minus 100. The detector sensitivity `k_sigma` (default 4, `K` command, range 1 to 20) is "how many sigmas count as abnormal".

### Mahalanobis distance

- **Plain:** A distance from "normal" that measures each feature in its own natural units of wobble, then combines them.
- **Picture:** Judging if a person is unusual by height AND weight together, each measured relative to how much people normally vary in it.
- **Technical:** D^2 = (x - mu)^T S^-1 (x - mu) with S the covariance matrix. If you ignore correlations (S diagonal), D^2 is simply the sum of squared z-scores.
- **In our project:** The statistical localizer's score per component is exactly this diagonal form: S = zr^2 + zc^2 + zs^2 + zg (runs, cpu, slice, plus a gap term). Ignoring correlations is a deliberate simplification: it needs no matrix inverse and only mean and sigma per feature, which fits an MCU.

### Threshold and debounce

- **Plain:** A threshold is the line beyond which we call something abnormal. Debounce means "don't believe one strange reading; wait to see it again".
- **Picture:** A smoke alarm that triggers only if smoke stays for a few seconds, so burnt toast does not evacuate the building.
- **Technical:** Each timing rule computes margin = measured / threshold (greater than 1 means violation). A timing violation must persist for 2 consecutive windows, unless margin is 3 or more ("too big to be jitter"). Hard evidence is never debounced.
- **In our project:** `timing_rules()` and `debounce()` in `fm_detect.c`; the streak counters `s_streak_rule[]` and `s_streak_stat[]`.

### False positive, false negative

- **Plain:** False positive = a false alarm (we said "fault" when all was well). False negative = a miss (a real fault went unnoticed).
- **Picture:** A fire alarm going off for toast (false positive) versus not going off in a real fire (false negative).
- **Technical:** There is always a trade-off: lower thresholds catch more faults but raise more false alarms. `K` moves this trade-off.
- **In our project:** `tools/fm_metrics.py` counts false positives as alarms during the fault-free period of a campaign (also per hour), and false negatives as injected trials not detected.

### Detection rate, localization accuracy, latency

- **Plain:** Detection rate: out of all faults injected, how many did we notice? Localization accuracy: of those noticed, how often did we blame the right component? Latency: how long from fault to alarm?
- **Picture:** A detective: how many crimes were noticed, how many times the right person was arrested, and how many days it took.
- **Technical (from `tools/fm_metrics.py`):** detection rate = detected trials / all trials; localization accuracy = trials blamed on the injected component / detected trials; type accuracy = trials with the expected fault type / detected; latency = injection to detection in ms (min, median, 95th percentile, max).
- **In our project:** The firmware itself measures latency when it knows the ground truth: `handle_fault()` computes `(detection cycle_ts - injection cycle_ts) / cycles_per_ms` and prints `latency_ms=` and `!GT ... LOCALIZED CORRECTLY` or `DIFFERENT COMPONENT`.

### Fault, error, failure

- **Plain:** Fault = the root cause (a bug, a flipped bit). Error = the wrong internal state it produces. Failure = when the wrong state becomes visible to the user.
- **Picture:** A loose bolt (fault) makes a wheel wobble (error); then the wheel comes off on the motorway (failure).
- **Technical:** This is the standard dependability chain (fault -> error -> failure) used in the dependability literature (see `05_REFERENCES.md`, dependability taxonomy). Our goal is to catch the error stage and point at the fault's location before it becomes a failure, or at least to record it when it does.
- **In our project:** For example, the injected `TIM2->PSC = 83` is the fault; the PWM and ADC running 10 times too fast is the error; the controller misbehaving would be the failure. We detect at the error stage (configuration CRC differs) within one window.

### Fault localization

- **Plain:** Not only "something is wrong" but "THIS part is wrong".
- **Picture:** The mechanic says "it is the left front brake pad", not just "your car makes a noise".
- **Technical:** Choosing the culprit component among all components whose signatures look abnormal, separating cause from consequences (victims).
- **In our project:** `rule_localize()` and `stat_localize()` in `fm_detect.c` both output a `loc_result_t {comp_id, fault_type, confidence, rules_hit, score[]}`.

### Recovery and escalation

- **Plain:** Recovery = the fix applied after detection. Escalation = if the small fix did not hold, try a bigger fix.
- **Picture:** Restart the app; if it crashes again soon, restart the phone.
- **Technical:** Our ladder, per component, in `fm_recover.c`: level 0 = the component's own recovery (restart the task or re-initialise the peripheral); level 1 = same plus re-initialise every peripheral; level 2 = system reset. The level rises when the same component needs recovery again within `FM_ESCALATE_MS` = 10 s, and returns to 0 otherwise.
- **In our project:** `fm_recover_plan()` decides, `fm_recover_execute()` acts; the recovery functions themselves belong to the application (`controller_restart`, `uart_shell_restart`, `app_adc_reinit`, `app_pwm_reinit`, `app_dac_reinit`, `app_tim6_reinit`, `fm_uart_reinit`).

### Hardware-in-the-loop (HIL)

- **Plain:** Testing control software against a simulated machine while using real hardware signals.
- **Picture:** A flight simulator with a real cockpit: the controls are real, the plane is simulated.
- **Technical:** Usually a separate real-time simulator feeds sensor signals to the controller under test. In our case the plant model runs on the same chip, but the signal goes through a real DAC and a real ADC on a real pin, so the controller sees genuine conversion noise, timing and failure modes.
- **In our project:** The loop in `controller.c`: plant temperature -> DAC on PA4 -> ADC channel 4 on the same pin -> filter -> PID -> PWM on PA5 and plant model -> DAC. Plant: dT/dt = (25 + 70 u - T) / 1.5 s, stepped every 10 ms.

### PID (proportional-integral-derivative)

- **Plain:** A controller that pushes harder the further you are from the target (P), keeps pushing if you have been off for a while (I), and eases off if you are approaching fast (D).
- **Picture:** Adjusting a shower's temperature: big turn when it is very cold, keep nudging if it stays a bit cold, slow down as it gets close.
- **Technical:** u = Kp e + Ki integral(e) dt + Kd de/dt. Ours: Kp = 0.05, Ki = 0.04, Kd = 0, dt = 0.01 s, output clamped to 0..1, with conditional integration (anti-windup: the integral is not updated while the output is saturated).
- **In our project:** `pid_step()` in `controller.c`, stage C. The gains live in `s_cfg`, which the MPU makes read-only.

### Isolation Forest

- **Plain:** A machine-learning method that finds odd data points by how easy they are to separate from the rest with random cuts.
- **Picture:** In a crowd, the person in a bright costume is singled out with one question ("costume?"); ordinary people need many questions.
- **Technical:** An ensemble of random trees; anomalies have short average path lengths. Unsupervised: trained on normal data only. It answers "normal or not", not "which component".
- **In our project:** `tools/fm_ml.py` (offline, on the PC, scikit-learn, 100 trees) trains it on normal windows from the `V` stream and compares its detection with the on-chip localizers. It is not on the chip.

### Decision tree

- **Plain:** A flowchart of yes/no questions on the features that ends in an answer.
- **Picture:** "Is ctrl's CPU above X? yes -> is report silent? no -> blame ctrl".
- **Technical:** A supervised classifier learned from labelled examples. Depth-limited trees are small and can be turned into nested if/else statements.
- **In our project:** `tools/fm_ml.py` trains a depth-5 tree (default `--max-depth 5`) on labelled windows (the `L:` ground-truth field) to predict the faulty component, and `--export-c` writes it as C code. The `localizer_t` interface in `fm_types.h` is designed so such a tree could become a third on-chip localizer; this is a design intention, not yet done in the firmware.

### Overhead

- **Plain:** What the doctor costs: CPU time, memory and storage taken away from the application.
- **Picture:** The weight of the fitness tracker on your wrist.
- **Technical:** CPU overhead (percent of time spent in monitoring code), RAM (bytes of variables and stacks), flash (code size), trace storage (ring buffer size).
- **In our project:** The `OVH` line of `S` prints `cpu_monitor` (the share of the window used by framework tasks, those registered with no recovery function: monitor and wdg), `ram_framework` (`fm_monitor_ram_bytes()`), `trace_storage` (sizeof ring buffer, 8 KB plus indices), `report` (164 bytes). Build sizes from the README: monitor ON about 97.6 KB flash, OFF about 55 KB. A second switch, `FM_ENABLE_DETECT` (in `fm_types.h`), gives a third "capture only" build (signatures, `S`/`V`/`D`, MPU traps, but no learning, detection, black box, recovery or watchdog; a small `window` task in `Src/monitor/fm_window_task.c` closes the windows instead of the monitor), so the overhead can be split into "cost of capture" and "cost of detection". The `prebuilt/` folder has all three builds. Note that the hook time inside other tasks' context switches and ISRs is not in `cpu_monitor`; the idle-counter comparison (ON vs OFF build) captures the total.

---

# Part B: FreeRTOS as used in this project

## B.1 Tasks and priorities (from the code)

| task | created in | CMSIS priority (number) | stack (words) | wakes up | registered as component |
|---|---|---|---|---|---|
| wdg | `fm_watchdog_init()`, `detect/fm_watchdog.c` | Realtime (48) | 192 | every 250 ms (`osDelay`) | 4 `wdg`, no recovery function |
| monitor | `fm_monitor_init()`, `detect/fm_monitor.c` | High (40) | 1024 | every 100 ms (`osDelayUntil`) | 3 `monitor`, no recovery function, excluded from judgement |
| ctrl | `controller_init()`, `app/controller.c` | AboveNormal (32) | 384 | each ADC scan (task notification, 50 ms timeout) | 0 `ctrl`, recovery `controller_restart` |
| report | `controller_init()` | Normal (24) | 512 | every 100 ms (`osDelayUntil`) | 1 `report`, recovery `controller_restart` |
| shell | `uart_shell_init()`, `inject/uart_shell.c` | Low (8) | 512 | each received byte, or 100 ms timeout | 2 `shell`, recovery `uart_shell_restart` |
| sigout | `fm_sigout_init()`, `monitor/fm_sigout.c` | Low (8) | 512 | when the monitor queues a window | not registered (counted as "other") |
| defaultTask | CubeMX code in `main.c` | Normal (24) | 128 | exits at once (`osThreadExit()`) | not registered |
| Timer service | FreeRTOS | 2 | 256 | software timers (none used by us) | not registered |
| Idle | FreeRTOS | 0 | 128 | when nothing else is ready; runs the idle hook | not registered |

Why these priorities (comment in `app/app_config.h` and `detect/fm_monitor.h`):

```
 Realtime 48   wdg       must be able to feed even if everything below is busy
 High     40   monitor   must outrank every application task (it judges them)
 AboveN.  32   ctrl      the real-time control loop, 100 Hz
 Normal   24   report    telemetry, 10 Hz, less urgent
 Low       8   shell     human input, least urgent
 Low       8   sigout    printing must never delay detection
```

## B.2 How a context switch happens on the Cortex-M4

Three exceptions do the work. SVCall starts the very first task. SysTick is the 1 ms tick. PendSV ("pendable service call") does every switch. PendSV runs at the lowest priority (15), so a switch only happens after all other interrupts have finished: no ISR is ever interrupted by a task switch.

Step by step, for "the DMA ISR wakes the control task while the report task is running":

```
time --->
report running (thread mode, PSP)
  | DMA2 Stream0 IRQ (prio 6)
  |   hardware pushes R0-R3,R12,LR,PC,xPSR (+S0-S15,FPSCR if FPU active) on report's stack (PSP)
  |   LR = EXC_RETURN (0xFFFFFFFD or 0xFFFFFFED)
  |   ISR: vTaskNotifyGiveFromISR(ctrl) -> ctrl becomes Ready, woken = pdTRUE
  |        portYIELD_FROM_ISR(woken)    -> sets the PendSV pending bit
  |   ISR returns
  | PendSV (prio 15) now runs, because nothing more urgent is pending
  |   save: PSP -> R0; if FPU used: push S16-S31; push R4-R11 and EXC_RETURN; store R0 into report's TCB
  |   raise BASEPRI to configMAX_SYSCALL_INTERRUPT_PRIORITY
  |   vTaskSwitchContext():
  |        traceTASK_SWITCHED_OUT()  -> fm_hook_task_out(tag of report)
  |        stack overflow check (method 2) for report
  |        pick highest-priority Ready task -> ctrl
  |        traceTASK_SWITCHED_IN()   -> fm_hook_task_in(tag of ctrl)
  |   restore BASEPRI
  |   load ctrl's saved stack pointer; pop R4-R11, EXC_RETURN (and S16-S31 if needed); PSP = result
  |   exception return: hardware pops R0-R3,R12,LR,PC,xPSR -> ctrl continues where it blocked
ctrl running
```

Two consequences we rely on:

1. The saved context of a task that is not running sits on its own stack, at the address in the first word of its TCB (`pxTopOfStack`). `fm_task_pc()` (`fm_capture.c`) reads it: `top[0..7]` = R4-R11, `top[8]` = EXC_RETURN, then 16 FPU words if EXC_RETURN bit 4 is 0, then the hardware frame whose 7th word (index 6) is the PC. That is how a `!FAULT` line for a stuck task can show `pc=` inside `do_stuck_loop`.
2. The trace hooks run inside PendSV with BASEPRI raised. Our hooks additionally set PRIMASK for a few instructions (`enter_critical()` in `fm_trace.c`), because ISR hooks (which can nest) update the same counters.

## B.3 traceTASK_SWITCHED_IN/OUT and why uxTaskNumber is the tag

`traceTASK_SWITCHED_OUT()` is expanded inside `vTaskSwitchContext()` while `pxCurrentTCB` still points to the outgoing task; `traceTASK_SWITCHED_IN()` after it has been changed to the incoming task. Our macros pass `pxCurrentTCB->uxTaskNumber` to the hooks.

Why `uxTaskNumber`:

- It is a spare integer field in every TCB, only present with `configUSE_TRACE_FACILITY = 1` (set in our config), documented in `tasks.c` as "a number specifically for use by third party trace code". FreeRTOS keeps its own creation counter in a different field (`uxTCBNumber`) and never writes `uxTaskNumber` except through `vTaskSetTaskNumber()`, which is what we call.
- `fm_register_task()` writes `comp_id + 1` into it. Zero (or any number above 16) means "not one of ours" and maps to `FM_COMP_OTHER` (0xFE): idle, timer service, sigout. (Careful detail for a viva: FreeRTOS 10.3.1, the version in our project, does not zero a new TCB. Unregistered tasks read 0 because the idle and timer TCBs are static and therefore zeroed by the startup code, and sigout's TCB comes from the never-used, zero-filled heap at boot. The code relies on that.)
- Reading one field of the current TCB is O(1) and needs no lookup table and no string compare inside the scheduler, where every cycle counts.
- Alternatives and why not: the task name (string compare: slow), the task handle (would need a search through a table on every switch), the application tag (`configUSE_APPLICATION_TASK_TAG`, also fine, but not enabled in our config).

What the hooks do (`fm_trace.c`):

- `fm_hook_task_in(tag)`: read DWT, set "current task", copy it to `g_fm_noinit.cur_comp` (survives a watchdog reset), `activate()` = update the longest gap since the previous activation, remember the slice start, `runs++`, record a `TASK_IN` event in the ring buffer.
- `fm_hook_task_out(tag)`: `finish_slice()` = add (now - slice start) to `cpu_cycles`, update `max_slice`, record `TASK_OUT`.

So `cpu_cycles` is the time between switch-in and switch-out, which includes time spent in interrupts that hit while the task was running. ISR time is small here (Part D), but it is a known approximation.

## B.4 Stack overflow checking, method 2, and our MPU guard

FreeRTOS has two optional checks (`configCHECK_FOR_STACK_OVERFLOW`):

- Method 1: at each switch-out, check that the saved stack pointer is still inside the stack.
- Method 2 (ours, set in `FreeRTOSConfig.h` USER CODE Defines): method 1 plus check that the last 16 bytes at the far end of the stack still contain the 0xA5 fill pattern. If not, call `vApplicationStackOverflowHook()`.

Weaknesses of both: they run only at a context switch, so an overflow that crashes before the next switch is never reported, and they detect after the damage.

Our primary detector is the MPU guard (`fm_mpu_guard_task()`): a 32-byte no-access region placed just above those 16 bytes, rounded up to a 32-byte boundary. A write into it traps immediately (MemManage), on the exact instruction, with the address in MMFAR. `fm_trap()` sees that the address is inside a guard (or that CFSR says MSTKERR, a stacking error) and classifies it as `FT_STACK_OVERFLOW`. Method 2 stays as the cheap backup: `vApplicationStackOverflowHook()` in `main.c` calls `fm_trap_stack_hook()`, which records `FM_TRAP_STACK_HOOK` and resets.

```
high address  +------------------+  top of task stack (initial SP)
              |  used stack      |
              |      ...         |  grows downward
              |  0xA5A5A5A5      |  never touched yet = free (counted by fm_stack_free_words)
              |  0xA5A5A5A5      |
              +------------------+  guard + 32
              |  MPU GUARD 32 B  |  no access: any read/write -> MemManage
              +------------------+  guard = (base + 16 + 31) & ~31
              |  (padding)       |
              |  16 bytes 0xA5   |  FreeRTOS method 2 checks these
low address   +------------------+  pxStackBase
```

There are 6 guard regions (MPU regions 2 to 7) and 5 registered tasks (ctrl, report, shell, monitor, wdg), so all of them are guarded. When a task is restarted, its guard is removed before deletion (`fm_mpu_unguard_task()`, because the stack memory goes back to the heap) and placed again at the new stack by `fm_register_task()`.

## B.5 Task notifications from an ISR

In `DMA2_Stream0_IRQHandler()` (`app_periph.c`):

```c
BaseType_t woken = pdFALSE;
vTaskNotifyGiveFromISR(t, &woken);   // ctrl's notification count += 1, ctrl -> Ready
portYIELD_FROM_ISR(woken);           // if ctrl outranks the interrupted task, pend PendSV
```

And in `ctrl_task_fn()` (`controller.c`):

```c
uint32_t got = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));   // wait, then clear to 0
if (got == 0U) fm_periph_error(COMP_PER_ADC, APP_PERR_TIMEOUT); // 5 periods with no data
```

Why a notification and not a semaphore or queue: it is the fastest and smallest FreeRTOS signal for "one producer, one known consumer". The data itself (two 16-bit samples) is copied into `s_samples` by the ISR and read by the task with interrupts briefly disabled (`app_adc_latest()`), so there is no torn read.

During a control-task restart the ISR must not notify a deleted task: `controller_restart()` disables the DMA interrupt, sets the handle to NULL, deletes and re-creates the task, registers it, and re-enables the interrupt.

## B.6 The UART line lock: why not a mutex

`fm_uart_write()` (`fm_uart.c`):

1. Before the scheduler starts: plain blocking `HAL_UART_Transmit` (the boot banner).
2. Otherwise: take the line lock `s_tx_mutex`, a binary semaphore (wait at most 500 ms; if it cannot, drop the line rather than block forever), and remember the holder in `s_tx_owner`.
3. Copy up to 256 bytes into `s_tx_buf`, start an interrupt-driven transmit, sleep on the `s_tx_done` semaphore (released by `HAL_UART_TxCpltCallback`), at most 500 ms.
4. Clear `s_tx_owner` and release the lock.

Why not a mutex: recovery deletes tasks. FreeRTOS lets only a mutex's owner release it, so if the report task were deleted halfway through a line, the mutex would stay taken forever and every later print (including `!FAULT`) would be dropped after 500 ms. With a binary semaphore, the restart functions call `fm_uart_forget_task(handle)` just before deleting a task, and if that task holds the lock it is released on its behalf.

What we give up: priority inheritance. If the shell (8) is printing when the monitor (40) wants to print, ctrl (32) and report (24) can preempt the shell, so the monitor waits a little longer, but never more than the rest of one line (at most 256 bytes, about 22 ms). An earlier version used a mutex for inheritance; a review on 19 Oct found the stranded-lock case and we chose robustness. Both answers are defensible in a viva: say which risk each one removes.

Why the transmit is interrupt-driven: a blocking transmit would keep the calling task "running" while it waits for the UART, which would inflate its `cpu_cycles` and `max_slice` and could look like an `EXEC_OVERRUN` to our own detector.

## B.7 Why the monitor must outrank the application

If a task stuck in an infinite loop had a higher priority than the monitor, the monitor would never run again, so it could never close a window or report anything. (An observer cannot report from a place it cannot reach.) At High (40), the monitor preempts ctrl (32), report (24) and shell (8) every 100 ms whatever they are doing, measures their windows and acts.

The examiner can demonstrate the opposite on purpose with `P 1 48` then `I 1 1` (report raised to Realtime, then stuck): the monitor starves, stops checking in, the feeder stops feeding, and the IWDG resets the board about 4 s later. After reboot the `!WATCHDOG` report blames the task that was running. So the system degrades to a coarser but still correct answer.

One subtle point: the detector copies task priorities into its context once, when the monitor task starts (`configure_detector()`). A priority changed later with `P` is therefore not seen by victim suppression until the next boot.

## B.8 Why the watchdog feeder is Realtime but only feeds when the monitor checked in

Two goals pull in opposite directions:

- The feeder must be able to run even when the CPU is loaded, otherwise a merely busy system would be reset. So it gets the highest task priority (48).
- But a feeder that always feeds proves nothing; it would keep feeding even if the monitor were dead.

The solution (`feeder_task_fn()`, `fm_watchdog.c`): every 250 ms the feeder checks `now - s_last_checkin < 1500 ms`. The monitor calls `fm_watchdog_checkin()` once per window (and around flash erases). So the watchdog answers the question "is the fault monitor still alive?". Things that stop the monitor (interrupts disabled forever, an ISR that never returns, a task above the monitor stuck, a corrupted scheduler) stop the check-ins, then the feeding, then about 4 s later the IWDG resets the chip. Timeline for interrupts disabled at t = 0: the feeder cannot run either (no ticks, no switches), last feed was at most 250 ms earlier, reset at about 3.75 to 4 s.

## B.9 configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 and why the DMA ISR uses 6

FreeRTOS on Cortex-M3/M4 does not disable all interrupts in its critical sections. It raises BASEPRI to `configMAX_SYSCALL_INTERRUPT_PRIORITY`, which masks only interrupts with priority numbers from that value up to 15 (the less urgent ones). In our config the library value is 5 (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`), shifted into the top 4 bits = 0x50.

Rules that follow:

- Interrupts with priority 0 to 4 are never delayed by the kernel (lowest latency) but must NEVER call any FreeRTOS function, because they could interrupt the kernel in the middle of updating its lists.
- Interrupts with priority 5 to 15 may call the `...FromISR` functions.
- The kernel's own interrupts (SysTick, PendSV) are at 15.

Our ISRs: TIM6 (5) calls `fm_uart_inject_rx()` -> `osMessageQueuePut` from ISR during a UART injection; USART2 (5) uses the queue and the semaphore; DMA2 Stream 0 (6) calls `vTaskNotifyGiveFromISR`. All are 5 or higher, so all are legal. The comment in `adc_dma_config()` states the requirement ("numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY (5)"). Why 6 rather than 5 is not explained in the code; one effect is that the 1 kHz tick and the UART (both 5) are not preempted by the ADC interrupt. With FreeRTOS's `configASSERT` enabled (it is, in our config), calling a FromISR function from an interrupt numbered below 5 would be caught by an assert in the port.

---

# Part C: The eight observed parameters

The problem statement asks us to observe eight things. For each one: what it is, how we measure it, what normal and abnormal look like, and which injected fault it catches. Normal values are design expectations from the code; measure the real ones with `S` (live window) and `B` (learned baseline).

## C.1 Execution sequence (function / stage order)

- **What it is:** The order in which the pipeline stages run: A acquire, B filter, C PID, D actuate (in ctrl, every 10 ms) and E report (in report, every 100 ms).
- **How we measure it:** The application calls `FM_STAGE(STAGE_x)` at the start of each stage (`controller.c`). `fm_stage()` (`fm_trace.c`) keeps the last stage run by EACH component (`s_last_stage_by[]`), and counts the transition previous -> current in `s_trans[prev][stage]` (saturating at 255). Per-component tracking matters: E runs in a different task, so the interleaving "D (ctrl) then E (report)" is never counted as a transition. While learning, `fm_learn_add()` ORs every seen transition into `trans_allowed[i]` (bit j set means i -> j seen).
- **What normal looks like:** In a window: A>B, B>C, C>D, D>A about 10 times each, E>E about once. The `B` command prints `allowed stage transitions: A>B B>C C>D D>A E>E` (the guidebook shows this list).
- **What abnormal looks like:** A transition with a non-zero count whose bit is not in the baseline, for example C>C.
- **Which fault it catches:** `FAULT_SEQ_REPEAT` (A -> B -> C -> C -> C). Detected as `FT_SEQUENCE`, hard evidence, blamed on the owner of the stage that ran out of order (stage C is owned by ctrl).

## C.2 ISR timing

- **What it is:** How often each interrupt runs, how long each run takes, and how much CPU the interrupt takes in total.
- **How we measure it:** `FM_ISR_ENTER(id)` / `FM_ISR_EXIT(id)` at the top and bottom of each monitored handler (`stm32f4xx_it.c` for TIM6, USART2, EXTI; `app_periph.c` for DMA2 Stream 0). Enter: `activate()` (runs, gap). Exit: `finish_slice()` (cpu_cycles, max_slice) and an `ISR_EXIT` event whose `extra` is the duration in microseconds. A small ISR stack (`s_isr_stack`, 8 deep) handles nesting, and `fm_current_comp()` returns the innermost ISR. The 1 kHz TIM6 and the per-byte UART interrupt are registered with `history = false`: they update counters but do not flood the ring buffer.
- **What normal looks like (expected):** tim6_isr about 100 runs per window, each a few microseconds; adc_isr 10 runs per window; uart_isr bursts only when text is sent or received (traffic-driven, irregular); btn_isr only on a press.
- **What abnormal looks like:** Rate far above the learned rate, one run far longer than ever, or total ISR time far above normal. For regular ISRs (sigma of runs at most 20% of the mean), also silence beyond the learned gap.
- **Which fault it catches:** `FAULT_ISR_STORM` (TIM6 at 10 kHz plus a 300-iteration busy loop in each run) -> `FT_ISR_RATE` on tim6_isr. Also, indirectly, a peripheral fault that stops the ADC makes adc_isr go silent (but hard evidence takes over, see Part E).

## C.3 Task execution time

- **What it is:** How much CPU each task uses per window, the longest uninterrupted run, how many times it ran, and the longest gap between runs.
- **How we measure it:** The context-switch hooks (Part B.3) with the DWT cycle counter. Per window: `runs` (switch-ins), `cpu_cycles` (sum of slices), `max_slice` (longest single slice), `max_gap` (longest time between two switch-ins, and also the silence from the last switch-in to the window end, so a hung task does not look calm).
- **What normal looks like (expected):** ctrl about 10 runs (more if preempted and resumed), small CPU (well under 5% of the window); report 1 or 2 runs (2 when it prints and sleeps on the UART); monitor 1; wdg 0 or 1 (it runs every 250 ms, so 2 or 3 times per 5 windows); shell about 1 (100 ms poll).
- **What abnormal looks like:** CPU or slice far above baseline (overrun); gap far longer than ever learned (liveness).
- **Which faults it catches:** `FAULT_STUCK_LOOP` -> `FT_EXEC_OVERRUN`; `FAULT_MISSED_DEADLINE` -> `FT_LIVENESS`; also the starvation of lower-priority tasks, which victim suppression then explains away.

## C.4 Stack usage

- **What it is:** How close each task's stack has come to its end.
- **How we measure it:** Three layers, all in `fm_capture.c` except the hook:
  1. `fm_stack_free_words()`: counts 0xA5A5A5A5 words from just above the guard. Called by the monitor for every task at each window close and stored in `stack_free_min`. Because the pattern is never restored, this is the lowest free stack since the task started (a high-water mark).
  2. The MPU guard: immediate trap on the first access into the guard.
  3. FreeRTOS method 2 as backup.
- **What normal looks like:** Free stack stable at some hundreds of words (the baseline stores the lowest value seen while learning).
- **What abnormal looks like:** The guard is hit (trap), or free stack drops below 16 words AND below the lowest learned value (`FM_RULE_STACK`, a "nearly exhausted" warning before the crash).
- **Which fault it catches:** `FAULT_STACK_OVERFLOW` (unbounded recursion, 64 bytes of locals per level) -> trap -> `FT_STACK_OVERFLOW`, confidence 99, reported after the reset.

## C.5 Peripheral status

- **What it is:** Whether each peripheral is still configured as at boot and is not reporting hardware errors.
- **How we measure it:** Two independent signals.
  1. Configuration fingerprint: each peripheral registers a table of `{register address, mask}` (`s_adc_regs`, `s_pwm_regs`, `s_dac_regs` in `app_periph.c`, `s_uart_regs` in `fm_uart.c`). At each window close `periph_hash()` computes CRC-32 over `(register & mask)` for each entry. Masks remove bits that legitimately change (for example ADC CR2 SWSTART, DMA CR CT, USART interrupt enables).
  2. Status flags: the application calls `fm_periph_error(comp, flags)` for ADC overrun (`ADC_SR_OVR`), DMA transfer error (`TEIF0`), implausible VREFINT reading, and ADC timeout (no samples for 50 ms).
- **What normal looks like:** `periph_hash` equal to the learned value every window; `periph_err` zero. (The per-component array `periph_err[]` is what the detector reads. The summary field `periph_err_count`, like `trap_count`, is never filled in the current code, so the middle and last numbers of the `E:` field in the `V` stream are always 0.)
- **What abnormal looks like:** Any CRC mismatch, any error count above zero (no errors are expected at all).
- **Which fault it catches:** `FAULT_PERIPH_CORRUPT` on ADC1 (EXTSEL changed: wrong trigger), TIM2 (PSC = 83: 10 times too fast), DAC1 (channel disabled), USART2 (ONEBIT sampling mode). Also the examiner's `W` command on any checked register. Detected as `FT_PERIPH`, hard evidence.

## C.6 Watchdog events

- **What it is:** Whether the system was reset because the watchdog expired (the "something froze" signal).
- **How we measure it:** `fm_capture_boot()` reads `RCC->CSR` right after reset. The trace hooks continuously write the running task into `g_fm_noinit.cur_comp` and the last stage into `g_fm_noinit.last_stage`; the ring buffer also lives in `.noinit`. After an IWDG reset, `fm_trace_init()` copies the last 16 events from the surviving ring buffer into the crash record before re-initialising it, and `boot_report()` (`fm_monitor.c`) files an `FT_WATCHDOG` report.
- **What normal looks like:** Reset cause power-on, reset button, or software reset requested by our own recovery (`planned_reset = 1`).
- **What abnormal looks like:** IWDGRSTF set.
- **Which fault it catches:** `FAULT_WDG_HANG` (interrupts disabled plus infinite loop inside the target task): the only fault that no software check can see while it happens.

## C.7 Memory-access anomalies

- **What it is:** Accesses to memory the code should never touch: NULL pointers, writes to read-only configuration, invalid addresses, illegal instructions.
- **How we measure it:** The MPU (NULL guard, read-only config, stack guards) plus the fault exceptions with their status registers. `fm_trap()` records trap kind, CFSR, HFSR, fault address (MMFAR/BFAR), stacked PC and LR, the component that was running, and the last 16 events, then resets. After reboot the monitor turns the record into a flash report.
- **What normal looks like:** No traps ever (`trap_count` in the signature is reserved for this and stays 0 in the current code; traps are reported after the reset instead).
- **What abnormal looks like:** Any trap.
- **Which faults it catches:** `FAULT_MEM_RO_WRITE` (`app_cfg()->kp = 1.0f`) and `FAULT_NULL_DEREF` (write to address 0x10) -> `FT_MEM_ACCESS`; the stack guard -> `FT_STACK_OVERFLOW`; any examiner `W` to an unmapped address -> BusFault -> `FT_MEM_ACCESS`.

## C.8 Communication errors

- **What it is:** Errors on the serial link: bytes lost, malformed, or frames with a wrong checksum.
- **How we measure it:** `HAL_UART_ErrorCallback()` maps ORE/FE/NE/PE to `FM_COMM_*` flags and calls `fm_comm_error(COMP_PER_UART, flags)`. The shell's `unframe()` checks the XOR checksum of `$...*hh` frames and calls `fm_comm_error(COMP_PER_UART, FM_COMM_CRC)` on mismatch. USART2 is registered with `is_comm = true`, so its errors become `FT_COMM` (configuration CRC changes on it are still `FT_PERIPH`).
- **What normal looks like:** Zero errors.
- **What abnormal looks like:** Any error.
- **Which fault it catches:** `FAULT_UART_CORRUPT`: the injector pushes `$T 55*00\r` into the receive queue. The real checksum of `T 55` is 0x74, so the frame is rejected (`ERR frame checksum`) and counted -> `FT_COMM` on USART2.

Summary table:

| parameter | signature fields | rule(s) | fault type | demo |
|---|---|---|---|---|
| execution sequence | `trans[i][j]` | `FM_RULE_SEQUENCE` 0x0010 | SEQUENCE | `I 10 0` |
| ISR timing | ISR `runs`, `max_slice`, `cpu_cycles`, `max_gap` | `FM_RULE_ISR_RATE` 0x0004, liveness | ISR_RATE | `I 4 6` |
| task execution time | task `cpu_cycles`, `max_slice`, `max_gap` | `FM_RULE_OVERRUN` 0x0001, `FM_RULE_LIVENESS` 0x0002 | EXEC_OVERRUN, LIVENESS | `I 1 0`, `I 3 1` |
| stack usage | `stack_free_min`, MPU guard | `FM_RULE_STACK` 0x0008, `FM_RULE_TRAP` 0x0100 | STACK_OVERFLOW | `I 2 1` |
| peripheral status | `periph_hash[]`, `periph_err[]` | `FM_RULE_PERIPH_CFG` 0x0020, `FM_RULE_PERIPH_ERR` 0x0040 | PERIPH | `I 7 9/10/11/12` |
| watchdog events | RCC_CSR, `.noinit` | `FM_RULE_WATCHDOG` 0x0200 | WATCHDOG | `I 9 0` |
| memory-access anomalies | CFSR, MMFAR/BFAR, `.noinit` | `FM_RULE_TRAP` 0x0100 | MEM_ACCESS | `I 5 0`, `I 6 2` |
| communication errors | `periph_err[USART2]`, `comm_err_*` | `FM_RULE_COMM` 0x0080 | COMM | `I 8 12` |

---

# Part D: Inside the controller

This part tells stories. Read them aloud until you can tell them without the page. Times marked "about" or "estimated" are design expectations, not measurements.

## D.1 Boot: from power-on to "Monitoring"

```
reset
 |-- startup code: copy .data, zero .bss (NOT .noinit), call main()
 |-- HAL_Init()                         (TIM1 becomes the HAL 1 ms time base)
 |-- fm_capture_boot()                  read + clear RCC_CSR; wipe .noinit after power-on; boot_count++
 |-- SystemClock_Config()               HSI 16 MHz -> PLL -> 84 MHz
 |-- MX_GPIO_Init, MX_USART2, MX_TIM6   (CubeMX generated)
 |-- fm_trace_init()                    start DWT; after an IWDG reset salvage last 16 events; clear registry
 |-- fm_mpu_init()                      VTOR = flash; NULL guard; enable Mem/Bus/Usage faults; /0 trap
 |-- fault_inject_init, idle_stats_init
 |-- fm_uart_init, fm_uart_register     RX queue, TX mutex + semaphore; USART2 + uart_isr registered
 |-- app_periph_init, app_periph_register   GPIO, DAC, ADC+DMA, TIM2; ADC1/TIM2/DAC1/ISRs registered
 |-- controller_init                    ctrl + report created and registered (MPU guards placed);
 |                                      stages A-D -> ctrl, E -> report; s_cfg made read-only
 |-- uart_shell_init + shell commands   shell task created and registered
 |-- fm_trace_register_shell, fm_sigout_init   D, S, V; sigout printer task
 |-- fm_monitor_init(COMP_MONITOR)      monitor task, L B R C M K
 |-- fm_watchdog_init(COMP_WATCHDOG)    wdg task, IWDG started (4 s)
 |-- banner "=== Fault Monitor v2 | ... ===", start TIM6 interrupt
 |-- osKernelInitialize, defaultTask, LED + button init, osKernelStart  -> scheduler runs
monitor task first run:
 |-- configure_detector()               copy kinds, priorities, stage owners into the detector context
 |-- fm_bb_init()                       scan sector 7: count valid reports, find next free slot and seq
 |-- boot_report()                      "BOOT #n reset cause: ..."; turn a crash record or IWDG reset into a stored report
 |-- load baseline from sector 6        valid only if magic, version, core_hz and CRC all match
 |-- warm-up 10 windows (1 s)           ignored: start-up transients
 |-- no baseline -> LEARN 100 windows (10 s) -> save to sector 6 -> grace 3 windows -> MONITOR
 |-- baseline found -> MONITOR immediately after warm-up
```

Why order matters (comment in `main.c`): the registry and MPU must exist before tasks register (registration places the stack guards); shell commands must be registered after `uart_shell_init()` (which resets the table); the monitor and watchdog come last so they see every component.

## D.2 One 10 ms control cycle in normal operation

Let t = 0 be a TIM2 update event.

```
t = 0 us      TIM2 counter wraps 999 -> 0 (update event, 100 Hz)
              - PA5 PWM: the new duty (CCR1, preloaded last cycle) takes effect; LED LD2 brightness = heater power
              - TRGO pulse -> ADC1 starts its regular sequence
t = 0..5 us   ADC channel 4 (PA4): sample 84 ADC clocks + 12 conversion = 96 clocks at 21 MHz, about 4.6 us
              ADC DR ready -> DMA request -> DMA2 Stream0 copies DR to s_dma_buf[0]
t = 5..28 us  ADC channel 17 (VREFINT): 480 + 12 = 492 clocks, about 23.4 us
              DMA copies to s_dma_buf[1]; NDTR reaches 0 -> reload to 2 (circular) -> transfer complete flag
t = ~28 us    DMA2_Stream0_IRQHandler (priority 6)
              FM_ISR_ENTER(adc_isr): runs++, gap since last (about 10 ms), ISR_ENTER event into the ring
              read and clear flags; TEIF0? ADC OVR? (none normally)
              copy pv_raw, vref_raw, seq++
              vTaskNotifyGiveFromISR(ctrl); portYIELD_FROM_ISR -> PendSV pending
              FM_ISR_EXIT(adc_isr): cpu += duration, ISR_EXIT event (extra = microseconds)
t = ~30 us    PendSV: switch out the running task (usually idle: tag 0 -> "other"), switch in ctrl
              fm_hook_task_in(ctrl): runs++, gap, TASK_IN event
              ctrl: ulTaskNotifyTake returns 1
                inject_point(ctrl)      nothing armed: returns at once
                FM_STAGE(A)  transition D->A counted; read samples (IRQs off for a few instructions)
                             VREFINT within 1200..1800? yes
                FM_STAGE(B)  A->B; pv = raw * 100/4095; s_pv_filt += 0.3 * (pv - s_pv_filt)
                FM_STAGE(C)  B->C; PID step (Kp 0.05, Ki 0.04, clamp 0..1, conditional integration)
                FM_STAGE(D)  C->D; CCR1 = u * 1000 (takes effect at the NEXT update, t = 10 ms)
                             plant: T += (25 + 70 u - T) * (0.01 / 1.5)
                             DAC DHR12R1 = T * 40.95 -> PA4 voltage changes within microseconds
              ctrl loops to ulTaskNotifyTake -> Blocked
              PendSV: fm_hook_task_out(ctrl): cpu += slice, max_slice, TASK_OUT event; idle runs
t = every 1 ms    TIM6 ISR (priority 5): FM_ISR_ENTER/EXIT (counters only, history off),
                  app_tick_isr (s_ms++), fault_inject_on_tick (nothing armed: returns)
t = every 1 ms    SysTick (FreeRTOS tick) and TIM1 (HAL tick): not monitored
t = 10 ms     next TIM2 update: the ADC now measures the DAC voltage written ~10 ms ago
```

So the controller always works on a measurement one period old. That is normal for a sampled control loop, and it is a REAL analog round trip through the pin.

Estimated per-cycle costs (to be measured with `S`): the ADC ISR a few microseconds, the control task some tens of microseconds (float maths on the FPU plus four stage hooks and two switch hooks). Over a 100 ms window ctrl should use well under 1% of the CPU.

## D.3 One 100 ms monitor window in normal operation

The monitor's period comes from `osDelayUntil(next)` with `next += 100` ticks; it is not synchronised with TIM2 (that does not matter: features are per window, and the 10 ms loop gives about 10 runs whatever the phase).

```
tick k*100    SysTick: monitor's delay expired -> Ready; PendSV -> monitor (40) preempts whoever runs
step 1  fm_sig_close_window(&s_pub.v)                                   (fm_trace.c)
        - inside a PRIMASK critical section (a few hundred cycles):
          for every component: copy runs (saturated to 65535), cpu_cycles, max_slice, max_gap;
          if the component was ever seen, max_gap = max(max_gap, now - last activation)  (silence counts)
          copy trans[8][8], comm/periph error counts and flags
          zero all live counters; new window starts now
        - outside the critical section: CRC-32 of the masked config registers of ADC1 (10 regs),
          TIM2 (6), DAC1 (1), USART2 (4)
step 2  for each task: stack_free_min = fm_stack_free_words(task)       (fm_capture.c)
step 3  fm_watchdog_checkin()  -> s_last_checkin = now
step 4  track_ground_truth()   -> did the injector fire since last window? start the 10 s label
step 5  shell requests at the window boundary: C (erase log), L (start learning)
step 6  grace windows pending? count down, ignore this window
        WARMUP:  count down 10 windows, then MONITOR (baseline exists) or start LEARN
        LEARN:   fm_learn_add(): Welford update of runs/cpu/slice for 16 components,
                 max gap, min stack, OR in seen transitions, remember config CRCs;
                 after N windows: fm_learn_finish(), save to sector 6 (erase + program),
                 "LEARN done ...", grace 3 windows, MONITOR
        MONITOR: rule_localize() AND stat_localize() on this window (both always run);
                 the selected one (M 0 / M 1) decides; if it names a component -> fault path (D.4)
step 7  fm_sigout_publish(): copy for 'S' (mutex, no wait), queue for 'V' if streaming (no wait, drops counted)
step 8  osDelayUntil(next): Blocked until tick (k+1)*100
```

The wdg task wakes every 250 ms, sees a check-in younger than 1500 ms, writes 0xAAAA to `IWDG->KR` and records a `CHECKIN` event. The report task wakes every 100 ms, runs stage E, and every 10th frame prints a `TEL,...*hh` line (so about one line per second).

What a normal window looks like (expected values; replace with yours from `S`):

| component | runs | cpu | max slice | max gap | notes |
|---|---|---|---|---|---|
| ctrl (0) | about 10 | small | tens of us | about 10 ms | more runs if preempted and resumed |
| report (1) | 1 to 2 | small | short | about 100 ms | 2 when it prints and waits for the UART |
| shell (2) | about 1 | tiny | tiny | about 100 ms | 100 ms receive timeout |
| monitor (3) | 1 | the largest framework cost | | 100 ms | not judged |
| wdg (4) | 0 or 1 | tiny | tiny | about 250 ms | 2 or 3 runs per 5 windows |
| adc_isr (5) | 10 | small | few us | 10 ms | regular |
| tim6_isr (6) | about 100 | small | few us | 1 ms | regular |
| uart_isr (7) | 0 to ~100 | varies | | varies | traffic-driven: no liveness check |
| btn_isr (8) | 0 | 0 | 0 | | event-driven |

## D.4 The fault path (common to every fault detected while running)

When the selected localizer returns a component, `fm_monitor.c` does, in this order:

1. `fm_sigout_publish()` (so the `V` stream also contains the faulty window).
2. Prints `!SHADOW <other localizer> says: <comp> <type> conf=<n>%`: what the localizer NOT in charge concluded for the same window.
3. `handle_fault()`:
   - `fm_recover_plan(comp, type, now)` decides the action and updates the escalation level (D.5).
   - Builds a `fault_report_t` (164 bytes): cycle time, uptime, type, component, confidence, action, rules bitmask, reset cause of this boot, which localizer, and for a task the PC where it currently is (`fm_task_pc()`).
   - Records an `EVT_FAULT` event, then copies the last 16 events of the ring buffer into the report (so the fault event is the last history line).
   - `store()`: checks in with the watchdog, appends to flash sector 7 (`fm_bb_append()`: sets magic, seq, CRC-32, programs 41 words into the next 256-byte slot), checks in again. If the sector was full it is erased first and a grace period is granted.
   - Prints `!FAULT seq=.. type=.. comp=name(id) conf=..% loc=rule|stat action=.. rules=0x.... pc=0x........ uptime=..ms latency_ms=..` followed by 16 `!HIST` lines (time in microseconds before detection, component, event).
   - If an injection is being tracked: `!GT injected=<fault id> on <comp> -> LOCALIZED CORRECTLY` or `DIFFERENT COMPONENT`.
   - For a system reset: `!RECOVERY level <n>: system reset`, waits 20 ms for the UART, sets `planned_reset` in `.noinit`, resets.
   - Otherwise executes the action, resets the window counters and the debounce streaks, grants 3 grace windows, and prints `!RECOVERED comp=.. action=.. level=..`.

The `rules=` field is the OR of every rule that was offered in this window, including candidates that were later removed (victims, or timing rules cleared by hard evidence). So `rules=0x0003` on an `EXEC_OVERRUN` verdict means a liveness rule also fired somewhere and was explained away.

## D.5 The recovery ladder (fm_recover.c)

```
fault on component X at time now
  |
  |-- type MEM_ACCESS or WATCHDOG?           -> LOG_ONLY (those were already fixed by a reset)
  |-- X recovered before AND now - last < 10 s -> level = min(level + 1, 2)
  |   else                                    -> level = 0
  |-- level 2, or X has no recovery function -> SYSTEM_RESET
  |-- X is a task                             -> RESTART_TASK
  |-- X is an ISR or peripheral               -> REINIT_PERIPH
execute:
  RESTART_TASK / REINIT_PERIPH: EVT_RECOVERY event; for a task remove its MPU guard first;
      call X's recovery function; at level 1 ALSO re-initialise every other peripheral
  SYSTEM_RESET: planned_reset = 1, suspect = X, NVIC_SystemReset()
after a planned reset the next boot prints "!RECOVERED by system reset (escalation for X)"
```

| level | when | action |
|---|---|---|
| 0 | first time, or more than 10 s since X's last recovery | X's own recovery |
| 1 | second time within 10 s | X's own recovery plus re-init of every peripheral (ADC1 with its DMA, TIM2, DAC1, USART2) |
| 2 | third time within 10 s | system reset |

Components with no recovery function (monitor, wdg, btn_isr) go straight to a system reset. The levels are kept in RAM, so a reset starts the ladder again at 0.

Recovery functions in this application:

| component | function | what it does |
|---|---|---|
| ctrl | `controller_restart` | disable DMA IRQ, forget the handle, delete the task, clear the PID integral, create a new task, register it (new guard), re-enable the IRQ |
| report | `controller_restart` | delete and re-create the report task |
| shell | `uart_shell_restart` | delete and re-create the shell task |
| adc_isr, ADC1 | `app_adc_reinit` | pulse the ADC and DMA2 reset lines in RCC, configure exactly as at boot |
| TIM2 | `app_pwm_reinit` | keep the duty, pulse the TIM2 reset line, configure, re-select PA5's alternate function |
| DAC1 | `app_dac_reinit` | keep the output code, pulse the DAC reset line, enable channel 1, restore the code |
| tim6_isr | `app_tim6_reinit` | storm off: TIM6 back to 1 kHz |
| uart_isr, USART2 | `fm_uart_reinit` | take the TX mutex, abort, de-init, pulse the USART2 reset line, HAL re-init 115200-8-N-1, restart reception |

The RCC reset pulse is important: it returns EVERY register of the peripheral to its reset value, including registers our configuration code never writes, so a corrupted bit anywhere is cleared.

## D.6 Fault 1: stuck loop (`I 1 0`, also targets 1 report, 2 shell)

**What physically happens.** At the control task's next loop, `inject_point(COMP_CTRL)` takes the armed fault, logs the fire record and calls `fm_note_injection()` (ground truth: an `INJECT` event that always enters the ring buffer, and a copy in `.noinit`). Then `do_stuck_loop()` spins on `__NOP()` forever with interrupts enabled. The CPU is now executing ctrl's loop whenever ctrl is the highest Ready task, which is almost always, because ctrl (32) outranks report, shell, sigout and idle. Interrupts still run. The DMA ISR still notifies ctrl (the count just grows, ctrl never takes it). The PWM duty and the DAC output freeze at their last values: the process is now uncontrolled.

Only three things still preempt ctrl: the monitor (40) every 100 ms, the wdg task (48) every 250 ms, and interrupts. Each preemption by a task produces a switch-out and a switch-in for ctrl.

**What changes in the signature.**

| feature | normal (est.) | stuck |
|---|---|---|
| ctrl cpu_cycles | well below 420,000 (5% of the window) | most of the window after the fault started, millions of cycles |
| ctrl max_slice | tens of us | up to about 100 ms (from one monitor preemption to the next) |
| ctrl runs | about 10 | 1 to 3 |
| report, shell max_gap | about 100 ms | growing every window (starved) |
| stage transitions | A>B, B>C, C>D, D>A about 10 each | none (stages no longer run) |
| idle | runs a lot | starved (the `IDLE` counter stops growing) |

**Which rule fires and why.** `timing_rules()`: ctrl is a periodic task, so `cpu_hi = max(mean + 4 sigma, 1.5 x max, 0.05 x W)` and `slice_hi = max(mean + 4 sigma, 2 x max, 1 ms)`. With normal ctrl usage far below these floors, the thresholds are effectively 420,000 cycles and 84,000 cycles. Even if the fault started half-way through the window, the margin is roughly 10 to 50, far above 3, so there is no debounce wait: `FT_EXEC_OVERRUN`, rule 0x0001.

Note that there is no rule for MISSING transitions, only for UNSEEN ones, so the empty transition matrix does not produce a sequence fault.

**Culprit vs victims.** If report or shell already exceed their liveness limits (more likely in a second window), they are candidates too. Victim suppression: ctrl is an aggressor (overrun) with priority 32; every liveness candidate with a lower priority (report 24, shell 8) is removed. So ctrl stays the only survivor.

**Confidence.** Rule localizer: 55 + 15 x log2(margin), capped at 99, minus 10 per other surviving suspect. A margin of 10 already gives 55 + 15 x 3.3 = 105 -> 99. Expect 99 (or 89 if another unexplained suspect survives).

**Stored in flash.** Type EXEC_OVERRUN, comp 0, conf, action RESTART_TASK, rules, `pc` = ctrl's saved PC, which points inside `do_stuck_loop()` (`tools/fm_report.py` with the ELF turns it into file and line), the last 16 events.

**Recovery and escalation.** RESTART_TASK: the guard is removed, ctrl deleted and re-created (PID integral cleared), registered again. Inject again within 10 s: level 1 (restart plus all peripherals re-initialised). Third time within 10 s: level 2, system reset, and after reboot `!RECOVERED by system reset (escalation for ctrl)`.

**Console (shape, values illustrative):**

```
OK armed STUCK_LOOP on ctrl in 0 ms; expect: EXEC_OVERRUN, restart task
!SHADOW stat says: ctrl EXEC_OVERRUN conf=99%
!FAULT seq=4 type=EXEC_OVERRUN comp=ctrl(0) conf=99% loc=rule action=RESTART_TASK rules=0x0001 pc=0x0800xxxx uptime=31250ms latency_ms=62
!HIST   -61980 us  ctrl     INJECT 1
!HIST   ...        adc_isr  ISR_ENTER 0 / ISR_EXIT 3 ...
!HIST   ...        monitor  TASK_IN 0
!HIST        0 us  ctrl     FAULT 1
!GT injected=1 on ctrl -> LOCALIZED CORRECTLY
!RECOVERED comp=ctrl action=RESTART_TASK level=0
```

Targets report (24) and shell (8) behave the same way, except that fewer tasks are below them to starve. A stuck shell shares priority 8 with sigout, so the two are time-sliced each tick.

## D.7 Fault 2: stack overflow (`I 2 1`)

**What physically happens.** In the report task, `do_stack_overflow(0)` calls itself forever; each level reserves a 64-byte local array plus saved registers (roughly 80 bytes per level, estimated), and it is written so the compiler cannot turn it into a loop. The stack pointer marches down towards `pxStackBase`. The first write that lands inside the 32-byte MPU guard is blocked by the MPU: MemManage exception, CFSR shows DACCVIOL and MMARVALID, MMFAR holds the guard address. If instead the hardware cannot even push the exception frame because the stack pointer is already in the guard, CFSR shows MSTKERR (a stacking error) and the handler runs on the main stack.

**In the handler (`fm_trap()`, `fm_capture.c`).** Disable interrupts; read CFSR, HFSR, MMFAR/BFAR; check EXC_RETURN bit 2 (it was a task, PSP); if there was no stacking error, read the stacked PC and LR; classify: MSTKERR or address inside a guard -> `FT_STACK_OVERFLOW`, else `FT_MEM_ACCESS`; component = `fm_current_comp()` = report; write all of this into `g_fm_noinit`; record an `EVT_STACK_FAULT` event (extra = low 16 bits of CFSR); copy the last 16 events into the record; set `crash_valid = 1`; `NVIC_SystemReset()`.

**Signature numbers.** None: the system resets before any window closes. This is a trap-based detection, not a signature-based one. (If the recursion had been slow and stopped just short of the guard, the stack rule could have warned first: free stack below 16 words and below the learned minimum -> `FT_STACK_OVERFLOW`, hard evidence, rule 0x0008.)

**After the reboot.** `fm_capture_boot()` sees a software reset and a valid `.noinit` magic, so the record is kept. `boot_report()` finds `crash_valid`, builds a report with type STACK_OVERFLOW, comp report, confidence 99 (95 for other traps), rules 0x0100 (TRAP), action SYSTEM_RESET, the saved PC and history, stores it in sector 7, prints it, then clears `crash_valid`.

**Recovery.** The reset was the recovery. Traps do not go through the escalation ladder (no counter survives the reset), so a fault that recurs at every boot would reset the board each time it recurs; see Part G, "what would you improve".

**Console:**

```
=== Fault Monitor v2 | process controller | core 84 MHz | monitor ON ===
type H for commands
BOOT #3 reset cause: software reset (0x06)
!TRAP before reset: trap=2 cfsr=0x000000.. hfsr=0x00000000 addr=0x2000.... lr=0x0800....
!FAULT seq=5 type=STACK_OVERFLOW comp=report(1) conf=99% loc=rule action=SYSTEM_RESET rules=0x0100 pc=0x0800.... uptime=0ms latency_ms=-1
!HIST ...  report  INJECT 2
!HIST ...  report  STACK_FAULT ..
!GT injected fault 2 on comp 1 before the reset
black box: 5 stored reports; baseline: loaded from flash
MONITOR running (localizer: rule)
```

`trap=2` is MemManage (1 HardFault, 2 MemManage, 3 BusFault, 4 UsageFault, 5 FreeRTOS stack hook). `pc=0x00000000` would mean a stacking error made the frame untrustworthy. `loc=rule` on boot reports just means "field is 0": no localizer was involved.

## D.8 Fault 3: missed deadline (`I 3 1`, also targets 0 and 2)

**What physically happens.** `inject_point()` calls `osDelay(10 x period)`: the report task (period 100 ms) sleeps for 1000 ms; ctrl (period 10 ms) would sleep 100 ms; shell (treated as 100 ms) 1000 ms. The task is Blocked, not running: no CPU is wasted, it simply does not show up.

**Signature.** The task's `runs` drops to 0 and its `max_gap` (which includes silence until the window end) grows window after window.

**Rule.** Liveness, for periodic tasks: `gap_hi = 2 x max_gap_seen + 2 ms`. For report, with a learned largest gap of about 100 ms, `gap_hi` is about 202 ms (estimate). The margin `max_gap / gap_hi` is just above 1 in the first window where silence exceeds 202 ms, so debounce applies: it must hold for a second window (or reach 3). Expected detection: about 300 to 400 ms after the missed activation. For ctrl, `gap_hi` is about 22 ms, so a 100 ms silence gives a margin above 4: detected at the first window close, no debounce.

When ctrl sleeps, the DMA ISR keeps notifying; when ctrl wakes, `ulTaskNotifyTake(pdTRUE, ...)` returns the accumulated count and clears it, so no ADC-timeout error is raised. The fault is pure timing.

**Culprit vs victims.** No aggressor exists (nobody overruns), so no suppression happens and the silent task itself is the culprit. This is the key difference from fault 1: there the silent tasks were victims of a CPU hog; here the silent task is the cause.

**Confidence.** 55 + 15 x log2(margin): margin 1.5 -> 64; margin 2 -> 70; margin 4.5 -> 87 (minus 10 per extra suspect).

**Recovery.** RESTART_TASK on the silent task (it would have resumed by itself after 1 s; the restart is the generic action and also covers a task blocked forever). Console: `!FAULT ... type=LIVENESS comp=report(1) ... action=RESTART_TASK rules=0x0002 ...`.

## D.9 Fault 4: ISR storm (`I 4 6`)

**What physically happens.** The injector runs inside the TIM6 interrupt itself (`fault_inject_on_tick()`), and calls `app_tim6_storm(true)`: TIM6 prescaler 8399 -> 839, so the interrupt rate goes from 1 kHz to 10 kHz, and each interrupt now also spins a 300-iteration volatile loop (estimated 25 to 30 us per interrupt). That is roughly 25 to 30% of the CPU taken at interrupt level (estimate). Tasks still run, just slower.

**Signature.** tim6_isr: runs about 1000 instead of about 100; max_slice tens of microseconds instead of a few; cpu_cycles millions instead of tens of thousands. Tasks: slightly longer slices (ISR time is counted inside the slice of whatever task was interrupted), but far below their overrun floors (5% of the window for cpu, 1 ms for a slice).

**Rule.** ISR rule (periodic ISR): `runs_hi = max(mean + 4 sigma, 1.5 x max) + 2`, about 153 with a learned 100; `cpu_hi = max(mean + 4 sigma, 2 x max, 2% of W = 168,000 cycles)`. Margins: runs about 6.5, cpu about 10 or more: `FT_ISR_RATE`, rule 0x0004, no debounce (margin above 3).

**Culprit vs victims.** ISRs have priority 0xFF in the detector, higher than any task, so if any task looked starved its liveness candidate would be suppressed as a victim of the storm.

**Confidence.** About 99 (margin above 8).

**Recovery.** tim6_isr is an ISR, so the action is REINIT_PERIPH with its recovery `app_tim6_reinit()`: storm off, TIM6 back to 1 kHz. At level 1 every peripheral is also re-initialised.

**Console.** `!FAULT ... type=ISR_RATE comp=tim6_isr(6) ... action=REINIT_PERIPH rules=0x0004`. The history lines show no tim6 events (it is registered with history off, 1000 events per second would flush the 1024-event buffer in about a second), but the `INJECT` marker is there (ground truth bypasses the history filter).

## D.10 Fault 5: write to read-only configuration (`I 5 0`)

**What physically happens.** In the target task, `app_cfg()->kp = 1.0f` tries to change the PID gain. `s_cfg` is exactly 32 bytes and 32-byte aligned (checked by a `_Static_assert`) and MPU region 1 makes it read-only. The store instruction is blocked: MemManage, CFSR = DACCVIOL + MMARVALID (typically 0x82), MMFAR = the address of `kp` (the start of `s_cfg`). The fault is precise, so the stacked PC is the store instruction itself.

**Classification.** Not in a guard, no stacking error -> `FT_MEM_ACCESS`; component = the running task.

**Flow.** Same as D.7: crash record, reset, boot report (confidence 95, rules 0x0100, action SYSTEM_RESET, pc = the store). `!TRAP ... addr=0x2000....` shows the address of `s_cfg`; checking it against the map file proves it was the protected configuration.

**Why it matters.** Without the MPU the write would succeed silently and the controller would run with a gain 20 times too high: a failure that appears later and far from its cause. With the MPU the error is stopped at the exact instruction.

**Recovery.** The reset. (If such a fault ever reached `fm_recover_plan()`, MEM_ACCESS maps to LOG_ONLY, because the reset is the recovery.)

## D.11 Fault 6: NULL pointer dereference (`I 6 2`)

**What physically happens.** The target task writes 0xDEADBEEF to address 0x00000010 (a NULL pointer plus a field offset). MPU region 0 (1 KB, no access, XN at address 0) blocks it: MemManage, MMFAR = 0x10. Without that region, address 0 on this chip is an alias of the boot memory (flash), so a NULL access would not reliably trap; the region turns a silent bug into an immediate, located fault. VTOR was moved to 0x0800_0000 at init so the region can never interfere with exception entry.

**Flow and console.** As D.10: `MEM_ACCESS comp=shell(2)`, `!TRAP ... addr=0x00000010`, confidence 95, reset.

## D.12 Fault 7: peripheral register corruption (`I 7 9`, `I 7 10`, `I 7 11`, `I 7 12`)

All four are fired from the TIM6 interrupt and all are caught the same way: the configuration CRC of that peripheral differs from the learned one at the next window close. That is hard evidence (rule 0x0020, margin 5, no debounce), and it beats every timing anomaly the corruption causes, because those anomalies are consequences. Confidence 95 (minus 10 per other component with hard evidence, normally none). Action REINIT_PERIPH. Expected latency: less than one window (estimate).

**ADC1 (`I 7 9`): `ADC1->CR2 ^= 1 << EXTSEL_Pos`.** The trigger selection changes from 0110 (TIM2 TRGO) to 0111, a TIM3 event per the reference manual's trigger table; TIM3 is not running, so conversions stop. Consequences: no DMA interrupts (adc_isr goes silent), ctrl's `ulTaskNotifyTake` times out every 50 ms and reports `APP_PERR_TIMEOUT` on ADC1 (a second piece of hard evidence, rule 0x0040, same component), ctrl then runs every 50 ms instead of 10 ms on stale samples (a liveness anomaly), the plant drifts. Hard evidence clears all timing candidates (adc_isr liveness, ctrl liveness), so ADC1 is blamed, not the control task. `app_adc_reinit()` pulses the ADC and DMA2 resets and reconfigures; conversions resume at the next TIM2 trigger.

**TIM2 (`I 7 10`, same as the examiner's `W 40000028 53`): `TIM2->PSC = 83`.** The prescaler is buffered and takes effect at the next update; then TIM2 counts at 1 MHz and updates at 1 kHz. Consequences: PWM at 1 kHz; ADC triggered 1000 times per second; adc_isr rate x10 (an ISR_RATE candidate); ctrl runs 100 times per window (more CPU); the simulated plant advances 10 times faster in real time. The TIM2 CRC (PSC is fully checked) is the hard evidence; adc_isr and ctrl anomalies are cleared as consequences. `app_pwm_reinit()` restores TIM2 and the pin function.

**DAC1 (`I 7 11`): `DAC->CR &= ~DAC_CR_EN1`.** The DAC channel is switched off, so PA4 no longer carries the plant temperature; the ADC reads a voltage unrelated to the model and the PID reacts to wrong data. The DAC CRC is the only evidence (no timing change at all) and it is enough. `app_dac_reinit()` restores the channel and the last output code.

**USART2 (`I 7 12`): `USART2->CR3 |= USART_CR3_ONEBIT`.** The receiver switches from three-sample majority voting to one sample per bit: the link usually still works, but with less noise immunity. A silent degradation that only the configuration checksum can see. Note: a configuration change on USART2 is reported as `PERIPH` (only error FLAGS on a communication peripheral become `COMM`). `fm_uart_reinit()` re-initialises the UART while holding the TX mutex.

Why masks matter: `USART2->CR1` interrupt-enable bits change with every HAL transfer, so they are masked out; only configuration bits (UE, M, WAKE, PCE, PS, TE, RE, OVER8) are checked. Without masks there would be false positives every window.

## D.13 Fault 8: corrupted UART frame (`I 8 12`)

**What physically happens.** From the TIM6 interrupt, `fm_uart_inject_rx("$T 55*00\r")` pushes 9 bytes into the receive queue exactly as if they had arrived on the wire. The shell task (8) wakes, echoes the characters (you see `$T 55*00` appear), and on the carriage return calls `dispatch()`. The line starts with `$`, so `unframe()` computes XOR('T', ' ', '5', '5') = 0x54 ^ 0x20 ^ 0x35 ^ 0x35 = 0x74, which is not the claimed 0x00. It calls `fm_comm_error(COMP_PER_UART, FM_COMM_CRC)` and prints `ERR frame checksum`. The setpoint is NOT changed: the checksum protected the application.

**Signature.** `periph_err[USART2] = 1`, `comm_err_count = 1`, `comm_err_flags = 0x0008`.

**Rule.** Error flags on a component registered with `is_comm` -> `FT_COMM`, hard evidence, rule 0x0080, margin 1 + count = 2, confidence 90.

**Recovery.** REINIT_PERIPH on USART2. Discussion point for the viva: a bad frame may come from the far end (the PC), not from our UART. Blaming USART2 really means "the communication path through USART2"; the report records it so an engineer can decide.

## D.14 Fault 9: watchdog hang with interrupts off (`I 9 0`)

**What physically happens.** In ctrl, `do_wdg_hang()` executes `__disable_irq()` (PRIMASK = 1) and spins. Now nothing can interrupt the CPU: no SysTick (time stops for FreeRTOS), no PendSV (no task switch), no TIM6, no DMA interrupt, no UART. The monitor cannot run, the wdg feeder cannot run, nothing prints. Hardware keeps going on its own: TIM2 still produces PWM at the last duty and still triggers the ADC, the DMA still overwrites `s_dma_buf`, the DAC holds its last value. The IWDG counts down on its own LSI clock and, about 3.75 to 4 s after its last feed, resets the chip.

**What survived.** In `.noinit`: `cur_comp` = ctrl (written by the switch-in hook when ctrl last started running), `last_stage` = the last stage entered (D from the previous cycle, stage number 3, because the hang happens in `inject_point()` before stage A), and the whole ring buffer, including the `INJECT 9` marker.

**After the reboot.** RCC_CSR has IWDGRSTF (cause 0x0A with PIN). `fm_trace_init()` copies the last 16 events from the old ring buffer into the record before clearing it. `boot_report()` builds a WATCHDOG report: comp = `cur_comp`, confidence 80 (40 if the last running thing was not a registered component, for example idle), rules 0x0200, pc 0 (unknown), action SYSTEM_RESET.

**Console:**

```
BOOT #4 reset cause: IWDG watchdog (0x0a)
!WATCHDOG reset: the CPU stopped serving the monitor; last running: ctrl, last stage: 3
!FAULT seq=6 type=WATCHDOG comp=ctrl(0) conf=80% loc=rule action=SYSTEM_RESET rules=0x0200 pc=0x00000000 uptime=0ms latency_ms=-1
!HIST ... ctrl INJECT 9
!GT injected fault 9 on comp 0 before the reset
```

**Why 80 and not 95.** The evidence is circumstantial: "ctrl was the last task switched in". If an interrupt handler had hung, `cur_comp` would still name the task it interrupted, because only task switches update `cur_comp`. The lower confidence is honest about that.

## D.15 Fault 10: sequence repeat A -> B -> C -> C -> C (`I 10 0`)

**What physically happens.** `inject_point()` returns `FAULT_SEQ_REPEAT` to the control task, which acts it out: after the normal stage C it calls `FM_STAGE(C)` + `pid_step()` twice more, then D. Physically, the PID integrates three times in one cycle: a small control glitch, like a real bug where a function is called in a loop by mistake.

**Signature.** That window's transition matrix has A>B, B>C, C>D, D>A about 10 each as usual, plus C>C = 2.

**Rule.** `hard_evidence()`: C>C has a count and bit C is not set in `trans_allowed[C]` -> blame the owner of stage C (ctrl), `FT_SEQUENCE`, margin 1 + 2 = 3, rule 0x0010, hard, confidence 90, detected at the next window close.

**Recovery.** RESTART_TASK on ctrl. Honest note: the fault was one-shot and already over, and the restart clears the PID integral, so the telemetry may show a short dip while the integral rebuilds. A gentler policy for one-shot sequence faults (log only) is a possible improvement.

## D.16 Summary: what you should see for each demo

| demo | fault (id) | detected by | type | blamed | conf (code) | action | when |
|---|---|---|---|---|---|---|---|
| `I 1 0` | stuck loop (1) | overrun rule | EXEC_OVERRUN | ctrl | 55+15log2(m), cap 99 | RESTART_TASK | next window |
| `I 2 1` | stack overflow (2) | MPU guard trap | STACK_OVERFLOW | report | 99 | reset | after reboot |
| `I 3 1` | missed deadline (3) | liveness rule | LIVENESS | report | 55+15log2(m) | RESTART_TASK | 2 to 4 windows |
| `I 4 6` | ISR storm (4) | ISR rule | ISR_RATE | tim6_isr | about 99 | REINIT_PERIPH | next window |
| `I 5 0` | RO write (5) | MPU region 1 trap | MEM_ACCESS | ctrl | 95 | reset | after reboot |
| `I 6 2` | NULL write (6) | MPU region 0 trap | MEM_ACCESS | shell | 95 | reset | after reboot |
| `I 7 9..12` | register scramble (7) | config CRC | PERIPH | that peripheral | 95 | REINIT_PERIPH | next window |
| `I 8 12` | bad frame (8) | frame checksum | COMM | USART2 | 90 | REINIT_PERIPH | next window |
| `I 9 0` | IRQs off + spin (9) | IWDG reset | WATCHDOG | ctrl | 80 | reset | about 4 s, after reboot |
| `I 10 0` | C repeated (10) | unseen transition | SEQUENCE | ctrl | 90 | RESTART_TASK | next window |

---

# Part E: How the chip decides

Both localizers live in `Src/detect/fm_detect.c`. They are pure C (no HAL, no RTOS), take one window `v` and the baseline `bl`, and fill a `loc_result_t`. The monitor runs BOTH every window; `M 0` / `M 1` only chooses which one acts (the other is printed as `!SHADOW`).

Constants used below (from the code): `k = k_sigma = 4` (changeable with `K`, 1 to 20); `ms = cycles_per_ms = 84,000`; `W = v.t_len`, the real window length in cycles (about 8,400,000); "periodic" means `runs.mean > 0` and `0 < max_gap_seen < 5 windows` (500 ms = 42,000,000 cycles). The monitor's own component is marked unused in the detector context, so it is never judged.

## E.1 rule_localize, step by step

```
Step 0  Start: no candidates; result comp = NONE.

Step 1  HARD EVIDENCE (shared with the statistical localizer)
  1a  For every stage pair (a, b) with v.trans[a][b] > 0 and bit b NOT set in bl.trans_allowed[a]:
        owner = stage_owner[b]
        offer(owner, SEQUENCE, hard, margin = 1 + count, rule 0x0010)
  1b  For every PERIPHERAL i with a learned CRC (bl.periph_hash[i] != 0) and v.periph_hash[i] != bl.periph_hash[i]:
        offer(i, PERIPH, hard, margin = 5, rule 0x0020)
  1c  For every component i with v.periph_err[i] > 0:
        offer(i, is_comm[i] ? COMM : PERIPH, hard, margin = 1 + count, rule is_comm ? 0x0080 : 0x0040)
  offer() keeps, per component, the strongest candidate: hard beats timing, then the larger margin wins.
  Every offer ORs its rule bit into that component's rule mask, even if it does not become the kept candidate.

Step 2  TIMING RULES, for every task and ISR
  TASK, periodic:
      cpu_hi   = max(mean_cpu + k*sd_cpu,     1.5 * max_cpu,   0.05 * W)
      slice_hi = max(mean_slice + k*sd_slice, 2.0 * max_slice, 1 ms = 84,000)
  TASK, event-driven:
      cpu_hi   = max(0.5 * W, 1.5 * max_cpu)
      slice_hi = max(0.5 * W, 2.0 * max_slice)
  m = max(cpu / cpu_hi, slice / slice_hi);   m > 1  ->  offer(EXEC_OVERRUN, timing, m, 0x0001)
  STACK (tasks): if free < 16 words AND free < lowest free seen while learning
                 ->  offer(STACK_OVERFLOW, HARD, margin 2, 0x0008)
  ISR:
      runs_hi  = periodic ? max(mean_runs + k*sd_runs, 1.5 * max_runs) + 2 : max(2 * max_runs, 2000)
      slice_hi = max(mean_slice + k*sd_slice, 3 * max_slice, 0.05 ms = 4,200)
      cpu_hi   = max(mean_cpu + k*sd_cpu,     2 * max_cpu,     0.02 * W)
  m = max(runs / runs_hi, slice / slice_hi, cpu / cpu_hi);   m > 1  ->  offer(ISR_RATE, timing, m, 0x0004)
  LIVENESS (tasks always; ISRs only if "regular": sd_runs <= 0.2 * mean_runs), only if periodic:
      gap_hi = 2 * max_gap_seen + 2 ms (168,000)
      max_gap > gap_hi  ->  offer(LIVENESS, timing, max_gap / gap_hi, 0x0002)

Step 3  DEBOUNCE (timing candidates only)
  component with no candidate: streak = 0
  hard candidate: streak unchanged, never debounced
  timing candidate: streak = streak + 1; if streak < 2 AND margin < 3 -> drop it for this window

Step 4  CAUSE vs CONSEQUENCE
  if Step 1 found any hard evidence:
      drop every non-hard candidate (timing anomalies are consequences of the broken thing)
  else (VICTIM SUPPRESSION):
      aggressor_prio = highest priority among EXEC_OVERRUN and ISR_RATE candidates (ISRs count as 255)
      drop every LIVENESS / STARVED candidate whose priority < aggressor_prio

Step 5  CHOOSE AND SCORE
  score(i) = (hard ? 1000 : 0) + margin          winner = highest score
  survivors = number of candidates left
  confidence:
      hard:   90 for SEQUENCE or COMM, 95 otherwise
      timing: 55 + 15 * log2(margin)
      then  - 10 * (survivors - 1), clamped to 30 ... 99
  rules_hit = OR of every component's rule mask
  score[i] * 100 is kept per component for logging
```

Reading the confidence formula: margin 1 (just at the threshold) gives 55; each doubling of the margin adds 15 (margin 2 -> 70, 4 -> 85, 8 -> 100, capped at 99). Each extra unexplained suspect costs 10 points, because "something else also looks wrong" is a reason for doubt. The floor of 30 means "we are reporting it, but do not trust it much".

## E.2 stat_localize, step by step

```
Step 1  Same hard evidence as E.1 Step 1. If there is any, skip Step 2 (hard evidence decides).
Step 2  For every task and ISR i:
          zsq(x, stat) = z^2 with  sd = max(sigma, 0.05 * |mean|, 1.0),  z = (x - mean)/sd clamped to +-100
          zr = zsq(runs), zc = zsq(cpu_cycles), zs = zsq(max_slice)
          zg = 0; if periodic and max_gap_seen > 0:
                g = max_gap / max_gap_seen;  if g > 1:  zg = ((g - 1) * 2k)^2
          S = zr + zc + zs + zg                (a diagonal Mahalanobis distance squared)
          T = 2 * (2k)^2                        (= 128 at k = 4)
          if S > T:
              type = LIVENESS        if zg is the largest term
                   = ISR_RATE        else if i is an ISR
                   = EXEC_OVERRUN    else if cpu >= mean cpu
                   = STARVED         otherwise
              offer(i, type, timing, margin = S / T, rule 0x0400)
Step 3  Same debounce (its own streak counters): 2 windows unless S >= 3T.
Step 4  NO victim suppression, NO stack rule: it has no causal knowledge, by design.
Step 5  Same scoring, but timing confidence = 100 * m / (m + 1) with m = S / T
        (m = 1 -> 50, m = 3 -> 75, m = 9 -> 90), then -10 per extra survivor, clamp 30 ... 99.
```

What T means: with k = 4, T = 128 is the score of two features each 8 sigma out (2 x 64), or of one feature about 11.3 sigma out. For a pure gap anomaly, S > 128 needs (g - 1) x 8 > 11.3, so the silence must exceed about 2.4 times the longest gap ever seen; the rule-based liveness fires at 2 times plus 2 ms. The two localizers have similar sensitivity by construction, so comparing them is fair.

Why two localizers: the rule-based one encodes engineering knowledge (priorities, cause before consequence) and is explainable line by line; the statistical one is generic and needs no knowledge of the application. Running both on the same windows, with ground-truth labels in the stream, gives a direct comparison for the report (`tools/fm_campaign.py --localizer stat`, `tools/fm_metrics.py`).

## E.3 Worked example 1: stuck loop in ctrl, both localizers

The formulas are the real ones; the baseline numbers are illustrative (use `B` on your board to get the real ones).

Baseline for ctrl (illustrative): runs mean 10.2, sd 0.6, max 11; cpu mean 40,000, sd 3,000, max 48,000 cycles; slice mean 6,000, sd 900, max 9,000 cycles; max_gap_seen 900,000 cycles (10.7 ms). Periodic: yes.

Window N (the loop has been spinning for the whole window): W = 8,400,000; ctrl runs 2, cpu 7,980,000, max_slice 7,560,000, max_gap 7,600,000.

Rule-based:

```
cpu_hi   = max(40,000 + 4*3,000 = 52,000 ; 1.5*48,000 = 72,000 ; 0.05*8,400,000 = 420,000) = 420,000
slice_hi = max(6,000 + 4*900 = 9,600   ; 2*9,000 = 18,000     ; 84,000)                  =  84,000
m = max(7,980,000/420,000 = 19.0 ; 7,560,000/84,000 = 90.0) = 90.0      -> EXEC_OVERRUN
liveness: gap_hi = 2*900,000 + 168,000 = 1,968,000 ; 7,600,000/1,968,000 = 3.86 -> LIVENESS offered too,
          but offer() keeps the larger margin for ctrl: EXEC_OVERRUN 90.0 (rule mask 0x0003)
debounce: margin 90 >= 3 -> kept at once
no hard evidence; aggressor_prio = 32; no other candidates
confidence = 55 + 15*log2(90) = 55 + 15*6.49 = 152 -> capped 99 ; survivors 1 -> 99
result: comp 0 (ctrl), EXEC_OVERRUN, 99 %, rules 0x0003
```

Statistical:

```
zr: sd = max(0.6, 0.05*10.2 = 0.51, 1.0) = 1.0  ; z = (2 - 10.2)/1 = -8.2      ; zr = 67.2
zc: sd = max(3,000, 2,000, 1) = 3,000           ; z = 2,647 -> clamped 100     ; zc = 10,000
zs: sd = max(900, 300, 1) = 900                 ; z = 8,393 -> clamped 100     ; zs = 10,000
zg: g = 7,600,000/900,000 = 8.44 ; zg = ((8.44 - 1)*8)^2 = 59.5^2               ; zg = 3,542
S = 23,609 ; T = 128 ; m = 184 -> type: zg not the largest, task, cpu >= mean -> EXEC_OVERRUN
confidence = 100*184/185 = 99.5 -> 99
```

Both agree: `!SHADOW stat says: ctrl EXEC_OVERRUN conf=99%`.

## E.4 Worked example 2: why victim suppression exists

With a strong CPU hog, the aggressor's margin is above 3 in the first window, so it is caught before its victims have been silent long enough (and for two windows in a row) to become candidates. Victim suppression is the safety net for the case where an aggressor is mild (margin between 1 and 3) and its victims are already flagged. This constructed window shows exactly that case.

Constructed window N+1 (the second window of a mild ISR storm):

| component | priority | candidate | margin | streak after this window | after debounce |
|---|---|---|---|---|---|
| tim6_isr | 255 (ISR) | ISR_RATE | 1.5 | 2 | kept |
| report | 24 | LIVENESS | 2.5 | 2 | kept |
| shell | 8 | LIVENESS | 1.2 | 1 | dropped (streak 1, margin < 3) |

Step 4 without suppression: the largest margin wins, report (2.5) beats tim6_isr (1.5). WRONG: report is starved, not guilty.

Step 4 with suppression: aggressor_prio = 255 (the ISR). report (24) < 255 and its candidate is LIVENESS, so it is dropped. Survivor: tim6_isr only.

Confidence = 55 + 15 x log2(1.5) = 55 + 8.8 = 63 (no extra survivors). Rules = 0x0004 | 0x0002 = 0x0006. Output: `ISR_RATE comp=tim6_isr(6) conf=63%`. The low confidence is appropriate: the evidence is mild.

What the statistical localizer would do in the same window: it has no suppression, so if report's score S exceeded T it would remain a survivor, and the larger S / T would win. That is precisely the comparison the project measures: "explainable rules with causal knowledge" versus "generic anomaly score".

## E.5 Worked example 3: hard evidence beats timing (TIM2 prescaler)

After `I 7 10`, window N: TIM2's CRC differs from the baseline -> offer(TIM2, PERIPH, hard, 5). adc_isr runs about 100 instead of 10; with an illustrative learned max of 11 runs, runs_hi = max(10 + 4 x 0.1, 1.5 x 11) + 2 = 18.5, so the ISR_RATE margin is about 5.4; ctrl now runs 100 times per window, so its CPU may also cross its threshold. Step 4: hard evidence exists, so every timing candidate is dropped. Survivor: TIM2 (score 1005). Confidence 95. The faster ADC and the busier ctrl task were consequences; the cause was one register.

---

# Part F: Our approach versus alternatives and market tools

Keep claims generic in the viva: describe categories of tools and what they are designed for, not version-specific features. For each tool the details are in `05_REFERENCES.md`.

| approach | what it is | where it is better than ours | what ours adds |
|---|---|---|---|
| SEGGER SystemView | Records RTOS and application events on the target and streams them (typically over a debug probe's RTT channel) to a PC application that shows a timeline, CPU load and task statistics. | Rich, mature visualisation; very low overhead recording; widely used in industry; great for a developer at a desk. | Ours needs no PC or probe in the field: it judges the execution itself, names a culprit, stores a report and recovers. SystemView shows what happened; a human decides. |
| Percepio Tracealyzer | Trace recorder library plus a PC tool with many views (timeline, CPU load, dependency graphs, statistics) for FreeRTOS and other RTOSes, in snapshot or streaming mode. | Far richer analysis and views; supports many RTOSes; good for performance tuning and finding rare timing bugs during development. | Same difference: autonomous on-chip decision, localization and recovery, rather than offline human analysis. |
| Percepio DevAlert | A service aimed at deployed devices: the firmware reports faults and anomalies (with a trace snapshot) to a cloud back-end for developers to inspect. | Fleet-level view across many devices; professional back-end; the developer sees traces from the field. | Ours works with no connectivity and decides on the device; but it has no fleet view and its anomaly logic is our own, smaller. |
| Memfault | A device observability platform: crash capture (core dumps), metrics, logs, OTA (over-the-air) updates, cloud analysis and grouping of issues across fleets. | Production-grade crash analysis with symbolication, fleet statistics, update management; supports many MCUs and RTOSes. | Ours detects non-crash timing faults (stuck loops, missed deadlines, ISR storms, sequence errors, register corruption) with a learned baseline and acts on them locally. |
| ETM / ITM hardware trace | Arm CoreSight trace: ITM sends software-generated messages and periodic PC samples (over the SWO pin, single wire output); ETM (embedded trace macrocell) streams every executed instruction through a parallel trace port to a capable probe. | Non-intrusive, cycle-accurate, instruction-level visibility; finds problems no software monitor can see. ETM needs extra pins and an expensive probe. | Ours costs no extra hardware and runs in deployment; hardware trace is a lab tool. (On our setup we did not use ETM; ST-LINK on the Nucleo supports SWO, which we also did not use.) |
| Plain watchdog | Hardware timer that resets the chip if not fed. | Simplest possible, catches total hangs, impossible to bypass in software. | We use one too (and feed it only when the monitor is alive), but a plain watchdog says nothing about which component failed or why, and cannot catch a task that is late but not totally hung. |
| FreeRTOS stack check only | `configCHECK_FOR_STACK_OVERFLOW` methods 1/2 at each context switch. | Free, built in, no MPU needed. | Ours traps the exact instruction with the MPU guard, survives the reset with a record, and also watches free stack trends. We keep method 2 as a backup. |
| Offline log analysis | Log events to a PC or to storage and analyse later (scripts, ML). | Unlimited computing power; can use complex models; good for research. | Ours decides in 100 ms on the chip. We also do the offline part (`tools/fm_ml.py`: Isolation Forest, decision tree) to compare. |

Our one-sentence positioning: commercial tracing tools are excellent microscopes for developers; our framework is a small on-board doctor that diagnoses, records and treats without a developer present. They are complementary, not competitors; in industry you would develop with a tracing tool and deploy with a monitor like ours.

Honest weaknesses to admit before the examiner finds them:

- The baseline is learned from 10 s of one operating mode; a legitimate change of mode (for example a much higher telemetry rate) looks like an anomaly until you re-learn.
- Detection granularity is 100 ms (one window); a fault shorter than that is still counted, but its exact time is only known to the history events.
- Watchdog blame is circumstantial (last task switched in), hence confidence 80.
- The framework, like any in-firmware monitor, shares the CPU and memory with the application: a fault that corrupts the framework's own memory can blind it (the watchdog is the last line).
- It has not yet been validated on hardware at the time of writing (README status); all numbers in the report must come from your measurements.

---
# Part G: 60 interview and viva questions with model answers

Answer in two layers: one plain sentence first, then the technical detail. If you do not know, say what you would check and where.

## G.1 Basics

**1. What is the difference between a microcontroller and a microprocessor?**
A microprocessor is only the CPU; memory and I/O are separate chips. A microcontroller puts CPU, flash, RAM and peripherals on one chip, optimised for control tasks, low cost and low power. Ours: Cortex-M4F core with 512 KB flash, 128 KB RAM, timers, ADC, DAC, DMA, USART.

**2. Flash vs RAM: where do your fault reports go and why?**
RAM is fast but forgets at power-off; flash keeps data but must be erased in whole sectors. Reports go to flash sector 7 (append-only, 256-byte slots, CRC-protected), the baseline to sector 6, so both survive power loss. The crash record and trace buffer use `.noinit` RAM, which survives a reset but not a power cycle, because a fault handler cannot safely erase and program flash.

**3. Why do you use `volatile`?**
It tells the compiler that a variable can change outside the normal flow (hardware, an ISR, another task), so every access must really read or write memory. Peripheral registers, `s_armed` (shared by tasks and the TIM6 ISR), `s_samples` and the busy loop counters are volatile. Volatile does not make an access atomic; for that we use short critical sections.

**4. What happens between reset and `main()`?**
The core loads the initial stack pointer and reset vector from the vector table, the startup code (`startup_stm32f446retx.s`) calls `SystemInit`, copies `.data` from flash to RAM, zeroes `.bss`, and calls `main()`. Our `.noinit` section is deliberately skipped, which is how crash data survives.

**5. Polling vs interrupts?**
Polling asks "is it ready yet?" in a loop and wastes CPU; an interrupt lets the hardware call you. We use interrupts for the ADC (via DMA), the UART and the timers, and the task sleeps on a notification or semaphore. A busy-wait would also look like a stuck loop to our own detector.

**6. What is DMA and why use it for the ADC?**
DMA copies data between peripherals and memory without the CPU. The ADC scan produces two results per 10 ms; the DMA stores both into RAM and interrupts once, so the CPU handles one interrupt per scan instead of one per conversion and cannot miss a result.

**7. How do NVIC priorities work on the STM32F4?**
4 implemented priority bits, 0 to 15; a lower number is more urgent and can preempt a higher number. With our grouping all bits are preemption priority. Any interrupt can preempt any task, regardless of task priority.

**8. What is a race condition and how does your ring buffer avoid one?**
Two contexts updating shared data at the same time can leave it inconsistent. Events are pushed from tasks, from nested ISRs and from inside the scheduler; `rb_push()` updates head/tail with interrupts masked for a few instructions, so pushes cannot interleave.

**9. Why save and restore PRIMASK instead of calling `__enable_irq()` at the end?**
The caller may already be running with interrupts disabled (for example inside the context switch or in a fault handler). Blindly re-enabling would break the caller's critical section. Save/restore nests correctly.

**10. What is a linker script and why does your project modify it?**
It tells the linker where each section goes in memory. We cut the FLASH region to 256 KB so code can never be placed in sectors 6 and 7 (which we erase), and we added a `.noinit` NOLOAD section that the startup code neither copies nor zeroes.

**11. What is the DWT cycle counter and what happens when it wraps?**
A 32-bit counter of CPU cycles (11.9 ns per count at 84 MHz). It wraps every about 51 s. All our durations are computed as unsigned differences (`now - start`), which stay correct across one wrap; no interval we measure is close to 51 s.

**12. How long does one UART byte take at 115200 8-N-1?**
10 bits (start, 8 data, stop) / 115200 = about 87 us. A 300-character line takes about 26 ms, which is why printing is done by low-priority tasks with interrupt-driven transmit, never by the monitor in a busy loop.

## G.2 RTOS

**13. What is an RTOS, and what is the difference between hard and soft real-time?**
An RTOS schedules tasks so that urgent work meets its deadlines. Hard real-time: a missed deadline is a failure (airbag). Soft: late results lose value but are not catastrophic (telemetry). Our control loop is firm/hard in spirit (10 ms), telemetry is soft.

**14. Preemptive vs cooperative scheduling?**
Preemptive: the kernel can stop a task at any time when a higher-priority task becomes ready. Cooperative: a task runs until it yields. We use preemptive (`configUSE_PREEMPTION = 1`), which is what allows the monitor to run even when an application task is stuck.

**15. How does a context switch work on Cortex-M4?**
The hardware stacks R0-R3, R12, LR, PC, xPSR on exception entry; the PendSV handler saves R4-R11 and EXC_RETURN (and S16-S31 if the FPU was used) on the task's stack, stores the stack pointer in the TCB, calls `vTaskSwitchContext()` to pick the next task, then restores the new task's registers and returns into it.

**16. Why is PendSV set to the lowest priority?**
So a context switch never interrupts an ISR; it runs only after all interrupts are done. ISRs just pend it with `portYIELD_FROM_ISR`.

**17. Mutex vs semaphore?**
A mutex has an owner and protects a resource; only the owner releases it, and FreeRTOS gives it priority inheritance. A semaphore is a counter for signalling events, any context can give it, including ISRs. We use a mutex for the UART line and a semaphore for "transmit complete".

**18. Explain priority inversion and how your project handles it.**
A high-priority task waits for a mutex held by a low-priority task, which medium tasks keep preempting. FreeRTOS mutexes raise the holder to the waiter's priority. In our UART driver, if the shell (8) holds the TX mutex when the monitor (40) wants to print a fault, the shell is boosted to 40 until it releases the mutex. The 500 ms acquire timeout is the second safety: the monitor drops a line rather than blocks forever.

**19. What is a task notification and why use it in the ADC path?**
A direct-to-task signal stored in the TCB, faster and lighter than a semaphore. The DMA ISR gives one to ctrl; ctrl takes it with a 50 ms timeout. The timeout doubles as a health check: no data for 5 periods is reported as an ADC peripheral error.

**20. What is `configMAX_SYSCALL_INTERRUPT_PRIORITY` and what if an ISR at priority 2 calls a FromISR API?**
It is the most urgent interrupt priority allowed to call FreeRTOS FromISR functions (5 in our config); the kernel masks priorities 5 to 15 with BASEPRI in critical sections. An ISR at priority 2 is never masked, so it could interrupt the kernel mid-update and corrupt its lists. With `configASSERT` defined, the port's interrupt priority check traps it during development. Our DMA ISR uses 6, TIM6 and USART2 use 5.

**21. Why must the monitor have a higher priority than the application tasks?**
A task stuck in a loop starves every task below it. If the monitor were below, it could never run to report the stuck task. At High (40) it preempts ctrl, report and shell every 100 ms. The examiner command `P 1 48` demonstrates the failure mode: the monitor starves and the watchdog has to catch it instead.

**22. Why is the watchdog feeder the highest priority but conditional?**
Highest, so that a heavily loaded but healthy system is not reset. Conditional (feeds only if the monitor checked in within 1500 ms), so the watchdog really tests that the monitor is alive. If the monitor stops (interrupts off, a hung ISR, a starving higher task), feeding stops and the IWDG resets after 4 s.

**23. How do you measure task execution time without modifying the kernel?**
FreeRTOS trace macros: we define `traceTASK_SWITCHED_IN/OUT` in `FreeRTOSConfig.h` to call our hooks with `pxCurrentTCB->uxTaskNumber` as the component tag, and timestamp with DWT CYCCNT. CPU time = sum of (switch-out minus switch-in). The kernel source is untouched.

**24. How is stack overflow detected in FreeRTOS, and in your project?**
FreeRTOS method 1 checks the stack pointer at switch-out; method 2 also checks that the last 16 bytes still hold the fill pattern; both run only at context switches. We add an MPU no-access guard just above those bytes that traps the first overflowing write immediately, plus a free-stack scan every window. Method 2 remains as a backup that also records a crash.

## G.3 Cortex-M faults

**25. Name the Cortex-M4 fault exceptions and when each occurs.**
MemManage: MPU violation or executing from an XN region. BusFault: an error response on the bus (bad address, unclocked peripheral). UsageFault: undefined instruction, invalid state, divide by zero if enabled, unaligned access if enabled. HardFault: when one of the others is disabled, or a fault occurs while handling a fault, or a vector fetch fails.

**26. What are CFSR, MMFAR, BFAR and HFSR?**
CFSR combines the MemManage, BusFault and UsageFault status bits (what kind of fault). MMFAR and BFAR hold the faulting data address when CFSR says they are valid. HFSR says why a HardFault happened (for example FORCED = escalated). We save all of them in the `.noinit` crash record.

**27. What is EXC_RETURN and how do you find the faulting PC?**
The special value in LR on exception entry. Bit 2 says which stack the interrupted code used (1 = PSP, a task). We read the stacked frame from PSP: word 6 is the PC of the faulting instruction (precise faults), word 5 the LR. If CFSR reports a stacking error, the frame is untrustworthy and we store PC = 0.

**28. Why enable MemManage, BusFault and UsageFault separately?**
Otherwise they all escalate to HardFault and you lose the information about what kind of fault occurred. We also turn on divide-by-zero trapping (`CCR.DIV_0_TRP`); by default an integer divide by zero returns 0 silently.

**29. What does the MPU do in your project, and what are its rules?**
Region 0: 1 KB no-access at address 0 (NULL guard). Region 1: the 32-byte PID configuration made read-only. Regions 2 to 7: a 32-byte no-access guard at the bottom of each task's stack. Rules on the M4: size is a power of two of at least 32 bytes, the base is aligned to the size, and the highest-numbered region wins on overlap. `PRIVDEFENA` keeps the default map everywhere else.

**30. How is a NULL dereference caught, and why do you move VTOR?**
Address 0 is an alias of the boot memory, so without the MPU a NULL access does not reliably fault. Region 0 makes the first 1 KB no-access, so `ptr->field` with a NULL `ptr` traps with MMFAR = the offset. VTOR is set to 0x0800_0000 so exception vector fetches never use the guarded alias.

**31. Why reset after a memory fault instead of continuing?**
After an illegal access we cannot know what state was corrupted, and the faulting instruction would fault again on return. The safe action is to save evidence in `.noinit`, reset, and file the report from a clean system. The reset is the recovery for MEM_ACCESS and stack overflow.

**32. What is the difference between a precise and an imprecise BusFault?**
Precise: the fault is tied to the instruction that caused it and BFAR is valid. Imprecise: usually a buffered write that failed later, after the CPU moved on, so the stacked PC is not the culprit and BFAR is not valid. For debugging, disabling the write buffer makes such faults precise, at a performance cost.

## G.4 This project's design decisions

**33. What exactly is a "runtime execution signature" in your project?**
A fixed-size vector computed every 100 ms: per component runs, CPU cycles, longest single run, longest gap and free stack; a stage-transition count matrix; error counts; and a CRC of each peripheral's configuration registers (`sig_vector_t` in `fm_types.h`).

**34. Why 100 ms windows?**
Long enough to contain about 10 control cycles, 100 ticks and at least one run of every periodic task except the 250 ms watchdog feeder (so statistics are stable), short enough that detection latency is about one window and the monitor costs little CPU. It is one constant, `FM_WINDOW_MS`.

**35. Why learn the baseline on the chip instead of on a PC?**
Autonomy: the device commissions itself with `L` in 10 s, without tools, and can re-learn after a configuration change. The baseline is stored with the clock frequency and a CRC and is rejected if they do not match.

**36. Why Welford's algorithm?**
One pass, constant memory, no stored samples, and numerically stable in 32-bit float for values in the millions of cycles, where the naive sum-of-squares formula loses precision.

**37. How do you distinguish the culprit from its victims?**
Two mechanisms. Hard evidence (unseen transition, config CRC change, error flags) wins over any timing anomaly, because timing changes are consequences. Without hard evidence, if a component is hogging the CPU (overrun or ISR rate), silent components of lower priority are its victims and their liveness candidates are removed.

**38. Why does hard evidence get absolute priority?**
It is binary and specific: a register either matches its learned CRC or not, a transition was either seen during learning or not. Timing anomalies have many possible causes and typically spread to other components. Example: a TIM2 prescaler error makes the ADC interrupt and ctrl run 10 times more; the TIM2 CRC points at the cause.

**39. Why debounce, and why the exception at margin 3?**
A single window slightly over a threshold can be jitter (a long print, a flash write); requiring 2 consecutive windows cuts false positives. But a margin of 3 or more is not jitter, and waiting would only add latency, so it is accepted at once.

**40. How is confidence computed? Is it a probability?**
It is a heuristic score, not a calibrated probability. Hard evidence: 95 (90 for sequence and communication errors, which can have external causes). Timing: 55 + 15 log2(margin) for rules, 100 m/(m+1) for the statistical score. Minus 10 per additional surviving suspect, clamped to 30 to 99. Trap reports: 99 for stack overflow, 95 for memory access; watchdog: 80, or 40 if no registered component was running. Calibrating it against campaign data is future work.

**41. Why a CRC of configuration registers, and why masks?**
Comparing a 32-bit CRC per peripheral per window is cheap and catches any changed configuration bit. Masks exclude bits that change legitimately (UART interrupt enables toggled by the HAL, ADC start bits, DMA current-target bit); without masks there would be false alarms every window.

**42. How do you detect a hang with interrupts disabled and blame a component?**
Software cannot see it while it happens, so the IWDG resets the chip. The trace hook writes the currently running task into `.noinit` at every switch, and the trace buffer is in `.noinit` too. After reboot the IWDG reset flag plus that record produce a WATCHDOG report blaming the last running task, with its last 16 events, confidence 80.

**43. How are reports stored and protected? What if power fails during a write?**
Append-only into the next 256-byte slot of sector 7, each with a magic number, a sequence number that keeps growing across resets and clears, and a CRC-32 over the whole record. At boot, a slot whose CRC does not match is skipped; an erased slot (0xFFFFFFFF) marks the end. When all 512 slots are used, the sector is erased and the log restarts.

**44. Why limit the firmware to 256 KB in the linker script?**
So the program can never be placed in sectors 6 and 7, which we erase at run time. Erasing a sector that contained code would destroy the firmware. It also means re-flashing from the IDE (sector erase) keeps the baseline and the log.

**45. How does recovery escalate?**
Per component: level 0 own recovery (restart the task / re-initialise the peripheral), level 1 if it happens again within 10 s (own recovery plus all peripherals re-initialised), level 2 on the third time (system reset). Components without a recovery function go straight to reset. After a planned reset the boot message says which component caused the escalation.

**46. How do you know the detected fault is the injected one? Does the monitor know what was injected?**
The injector logs every arm and fire with a cycle timestamp (`G` command) and marks the trace with an `INJECT` event; the monitor uses this only to label windows and to print `!GT ... LOCALIZED CORRECTLY / DIFFERENT COMPONENT` and the latency. The localizers never read it: they see only the signature, as with a real bug.

## G.5 Evaluation metrics

**47. Define your metrics.**
Detection rate = detected trials / injected trials. Localization accuracy = trials blamed on the injected component / detected trials. Type accuracy = trials with the expected fault type / detected. False negatives = injected but not detected. False positives = alarms during fault-free periods, also per hour. Latency = injection to detection, reported as min, median, 95th percentile and max (`tools/fm_metrics.py`).

**48. How do you measure CPU and RAM overhead?**
CPU: run the monitor-OFF build and the ON build for the same time and compare the idle-hook rate (`IDLE` line of `S`); the drop is the total overhead including hooks. The `OVH` line also reports the monitor and wdg tasks' share of each window. A capture-only build (`FM_ENABLE_DETECT = 0`) separates capture cost from detection cost. RAM: `fm_monitor_ram_bytes()` (ring buffer, crash record, registry, monitor state, monitor stack) and the map file. Flash: build sizes (about 97.6 KB ON vs 55 KB OFF per the README).

**49. How do you estimate the false-positive rate?**
Run long fault-free periods (the campaign script's `--normal` time, and a long idle run) and count `!FAULT` lines. Report it per hour with the total observation time, because "zero false positives in 5 minutes" says little.

**50. How do you compare the rule-based and statistical localizers, and where does ML fit?**
Both run on every window on the chip; `M` chooses which acts, the other is printed as `!SHADOW`, and the `V` stream records both verdicts with the ground-truth label. Campaigns are run with each one active and scored with the same metrics. Offline, `tools/fm_ml.py` trains an Isolation Forest (unsupervised detection from normal windows) and a depth-limited decision tree (supervised localization) on the same windows, and can export the tree as C.

**51. What latency do you expect per fault class?**
Hard evidence and big timing margins: at the next window close, so under about 100 ms plus the window phase. Mild timing anomalies: 2 windows because of debounce. Liveness: the silence must exceed twice the learned gap, so a few hundred ms for a 100 ms task. Traps: immediate, but the report appears after the reboot. Watchdog: about 4 s plus the reboot.

**52. What are the threats to validity of fault injection?**
Injected faults are clean and well-separated; real bugs can be intermittent, combined, or cause effects we did not inject. The injection point and timing are controlled. The ground truth is the injected component, while a real fault might legitimately have several contributing components. Mention them, and argue that the injected set covers every parameter class required.

## G.6 What would you improve, and porting

**53. What would you improve first?**
(1) A crash-loop guard: count trap resets in `.noinit` and enter a safe mode after N in a row. (2) Watchdog blame for ISRs: record the innermost ISR in `.noinit` too. (3) An adaptive or multi-mode baseline instead of one learned mode. (4) Calibrate confidence against campaign data. (5) Run the exported decision tree on the chip as a third `localizer_t`. (6) Gentler action for one-shot faults (log only for a single sequence error). (7) Take stage timing (time per stage) into the signature.

**54. Is it safe to delete a task as a recovery action?**
Only if it does not hold resources. A task deleted while it holds a FreeRTOS mutex leaves it locked forever. That is why the UART line lock is a binary semaphore and every restart function first calls `fm_uart_forget_task()`, which releases the lock if the doomed task held it. Other resources (a half-finished I2C transfer, heap it allocated) need the same audit; a production version may prefer restarting at safe points, or a "please restart" request the task honours.

**55. How would you port the framework to a Cortex-M7 (for example STM32F7/H7)?**
Most of it carries over: DWT CYCCNT, the fault registers, EXC_RETURN and the MPU programming model are the same architecture (ARMv7-M). Changes: the FreeRTOS port (ARM_CM7, including the variant for early core revisions), caches (DMA buffers need cache maintenance or a non-cacheable MPU region, and our MPU regions must set memory attributes accordingly), a double-precision FPU on some parts (longer context frames are handled by the same EXC_RETURN bit), different flash sector sizes and addresses for the black box, and 8 or 16 MPU regions depending on the part. `fm_capture.c` is the file to revisit; `fm_trace.c` and `detect/` stay.

**56. And to a Cortex-M0/M0+?**
Harder (ARMv6-M): no DWT cycle counter (use a free-running hardware timer for timestamps), no MemManage/BusFault/UsageFault and no CFSR/MMFAR/BFAR (everything is a HardFault with little status information), an MPU only on some M0+ parts with fewer regions, no BASEPRI (FreeRTOS masks all interrupts in critical sections), 2 priority bits, no unaligned or divide-by-zero trap (no hardware divide on M0). Detection logic is pure C and ports unchanged; the trap classification would become coarser.

**57. Can the framework monitor a different application?**
Yes: the framework only knows component numbers, kinds, stages and register tables. A new application edits `fm_common.h`, calls the register functions, adds `FM_ISR_ENTER/EXIT` and `FM_STAGE` at the right places, provides recovery functions, and re-learns the baseline. `fm_types.h` and `detect/` do not change.

**58. What if the monitor itself fails?**
The monitor does not judge itself (an observer cannot observe its own absence). The watchdog covers it: no check-in for 1.5 s stops the feeding and the IWDG resets the board. Its own stack has an MPU guard like every registered task.

**59. How far does it scale?**
`FM_MAX_COMP` is 16 and the signature, baseline and report formats are frozen around it; 13 are used. Only 6 MPU regions are left for stack guards. CPU cost of the window close and the localizers grows linearly with components. More components would mean a format change (announced to the whole team, because the Python tools parse the layouts byte for byte) and possibly guards only for critical tasks.

**60. How does this relate to functional-safety practice?**
Safety standards (for example IEC 61508 and ISO 26262) expect run-time diagnostics such as watchdogs, program-flow monitoring, memory protection and plausibility checks, with measured diagnostic coverage. Our framework implements several of those ideas (sequence monitoring, timing supervision, MPU, configuration checks, an independent watchdog) as a student prototype; it is not a certified implementation. See `05_REFERENCES.md`, safety standards.

---

# Part H: 7-day self-study plan

Calendar: Day 1 is Tuesday 20 October, Day 7 is Monday 26 October; the evaluation is Tuesday 27 October.

Daily routine for all three:

- 30 minutes: 8 to 10 keywords of Part A. Close the book and explain each one aloud in plain words.
- 2 to 3 hours: the code for your day (below), with this book open.
- 30 minutes in the evening, together: each of you explains your day's code to the other two in 5 minutes, then 10 random questions from Part G.
- If the board is available: run the demo(s) of the day and write down the real numbers (`S`, `B`, `R`), replacing the estimates in your slides.

## H.1 Abhay: capture layer

| day | read in the code | goal / self-test |
|---|---|---|
| 1 | `Inc/fm_types.h` (all of it), `Src/monitor/ring_buffer.h/.c` | Draw `event_t` and `sig_vector_t` from memory. Why is the buffer size a power of two? Why save/restore PRIMASK? Part A: register, memory map, stack/heap, flash/RAM, linker sections. |
| 2 | `Inc/FreeRTOSConfig.h` USER CODE Defines, `Src/monitor/fm_trace.h`, `fm_trace.c` hooks (`fm_hook_task_in/out`, `fm_isr_enter/exit`, `activate`, `finish_slice`, `fm_stage`) | Explain Part B.2 and B.3 on a whiteboard. Why `uxTaskNumber`? Why per-component last stage? |
| 3 | `fm_trace.c` `fm_sig_close_window`, `periph_hash`, `fm_trace_restart_window`, `cmd_dump_rb`; `Src/app/app_periph.c` register tables | Explain silence counting at window end and the register masks. Part C.1 to C.3, C.5. |
| 4 | `Src/monitor/fm_capture.h/.c`: `fm_capture_boot`, `.noinit`, reset causes; `STM32F446RETX_FLASH.ld` | Explain what survives a reset and what survives power-off. Questions 2, 4, 10, 42. |
| 5 | `fm_capture.c` MPU (`fm_mpu_init`, `fm_mpu_guard_task`, `fm_stack_free_words`), `fm_trap`, `fm_task_pc`; `Src/stm32f4xx_it.c` USER CODE | Tell the stories D.7, D.10, D.11 without notes. Decode a CFSR value. Questions 25 to 31. |
| 6 | `Src/monitor/fm_sigout.h/.c`, `Src/monitor/fm_window_task.c`, `tools/fm_dump.py` | Explain the `OVH` and `IDLE` lines and the three builds. Measure overhead on the board if available. Questions 48, 55, 56. |
| 7 | Rehearsal | Present the capture layer in 5 minutes; answer Part G 1 to 32 aloud; walk D.2 and D.3 timelines. |

## H.2 Arfan: detection layer

| day | read in the code | goal / self-test |
|---|---|---|
| 1 | `Inc/fm_types.h` (baseline, report, localizer, flash map), `Src/detect/fm_learn.h/.c` | Derive Welford on paper for 3 numbers. What is in a baseline and why each field? Part A: mean/sd/Welford, z-score, baseline. |
| 2 | `Src/detect/fm_detect.h`, `fm_detect.c` `hard_evidence`, `timing_rules`, `fm_detect_is_periodic` | Write all thresholds from memory (E.1 Step 2). Why floors like 5% of the window and 1 ms? |
| 3 | `fm_detect.c` `debounce`, `rule_localize` (victim suppression), `finish`, `stat_localize`, `zsq` | Redo worked examples E.3, E.4, E.5 by hand with different numbers. Questions 37 to 40. |
| 4 | `Src/detect/fm_monitor.h/.c`: task loop, `boot_report`, `handle_fault`, `track_ground_truth`, shell commands | Draw the monitor's state machine (warm-up, learn, monitor, grace). Tell D.4 without notes. |
| 5 | `fm_recover.h/.c`, `fm_watchdog.h/.c`, `fm_blackbox.h/.c`, `fault_threshold.h/.c` | Explain the ladder, the IWDG numbers (500 Hz, 2000, 4 s, 250 ms, 1500 ms), the slot format and CRC. Tell D.14. Questions 22, 43 to 45, 58. |
| 6 | `tools/fm_ml.py`, `tools/fm_report.py`, `tools/fm_metrics.py` | Explain Isolation Forest vs decision tree and how the tree could become a third localizer. Questions 47 to 52. |
| 7 | Rehearsal | Present detection in 5 minutes; run `M 1` vs `M 0` demos if the board is available; answer Part G 33 to 60. |

## H.3 Abhishek: application, injection and tools

| day | read in the code | goal / self-test |
|---|---|---|
| 1 | `Src/app/fm_common.h`, `app_config.h`, `Src/main.c` USER CODE (init order, hooks, TIM6 callback) | Recite component ids 0 to 12 and fault ids 1 to 10. Explain why the init order matters (D.1). |
| 2 | `Src/app/app_periph.h/.c`: TIM2, ADC, DMA, DAC, ISR, re-init functions; clock tree in `SystemClock_Config` | Compute 84 MHz, 100 Hz and 1 kHz from the register values. Part A: clock tree, timer/PWM, ADC, DAC, DMA, GPIO. |
| 3 | `Src/app/controller.h/.c`: stages A to E, PID, plant, button, restart; `idle_stats.c` | Tell D.2 (one 10 ms cycle) without notes. Explain anti-windup and why the config is read-only. |
| 4 | `Src/inject/fm_uart.h/.c`, `uart_shell.h/.c` | Explain the mutex + semaphore design and priority inheritance (B.6). Compute the checksum of `T 50` (answer 0x71). |
| 5 | `Src/inject/fault_inject.h/.c` | For each fault 1 to 10: where it fires (task or TIM6 ISR) and what physically happens. Tell D.6, D.8, D.9, D.12, D.13, D.15. |
| 6 | `tools/fm_serial.py`, `fm_dump.py`, `fm_campaign.py`, `fm_metrics.py`; `docs/01_GUIDEBOOK.md` sections 4 and 5 | Run (or dry-run) a campaign and the metrics; prepare the results table and the overhead numbers. |
| 7 | Rehearsal | Run demos 1 to 17 of the guidebook in order (with `Y 0` first); prepare the answer to "what if the examiner types something unexpected" (`W`, `P`). |

## H.4 Last-evening checklist (all three)

- Each of you can explain the project in 1 minute, 5 minutes and 15 minutes.
- Each of you can draw: the system block diagram, the task priority table, the 10 ms timeline, the stack guard picture, the recovery ladder.
- Everyone knows the honest limitations (Part F end) and the top 3 improvements (question 53).
- The board is flashed with the monitor-ON build, the baseline is learned, the fault log is cleared (`C`), telemetry is off (`Y 0`), a terminal at 115200 8-N-1 is ready, and a backup `.bin` is on a USB stick.
