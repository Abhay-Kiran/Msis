# Verified References — Autonomous Firmware Fault Localization Using Runtime Execution Signatures

Board: STM32 Nucleo-F446RE (Arm Cortex-M4) · FreeRTOS via CMSIS-RTOS v2 · STM32CubeIDE

Every link below was seen in a live web search on 2026-10-19, and some were also opened to confirm their content (see the note at the end). Entries marked **(video)** or **(playlist)** are on YouTube. Start with the first one or two entries in each section. The rest are for going deeper.

---

## 1. What a firmware framework is: layered architecture, portability, HAL vs bare metal

- [Embedded Artistry's Software Layering Strategy](https://embeddedartistry.com/fieldatlas/16737/) — Embedded Artistry — Explains why firmware is split into layers (hardware, drivers, middleware, application) so you can change one layer without rewriting the others.
- [Hardware Abstraction Layer (glossary entry)](https://embeddedartistry.com/fieldmanual-terms/hardware-abstraction-layer/) — Embedded Artistry — Short definition of a HAL. Handy when an examiner asks "what is a HAL?"
- [Playbook: Writing Portable Embedded Software (PDF)](https://embeddedartistry.com/wp-content/uploads/2020/08/Playbook-Writing-Portable-Software.pdf) — Embedded Artistry — Practical checklist for writing code that can move to a different chip with little rework.
- [How to Write Epic Hardware Abstraction Layers (HAL) in C](https://www.beningo.com/how-to-write-epic-hardware-abstraction-layers-hal-in-c/) — Jacob Beningo — Shows how to design your own HAL interface using function pointers and structs.
- [Introduction to STM32 Bare Metal Programming: Register Level vs. HAL](https://www.youtube.com/watch?v=wVbD3Y4-GgA) — STM32World (video) — Shows the same task written with ST's HAL and with direct register access, so you can see the trade-off.
- [STM32 Bare Metal Tutorial](https://www.youtube.com/playlist?list=PLVfOnriB1RjUO4qv9lQcYVliGpt8T3gOR) — STM32World (playlist) — Step-by-step register-level STM32 projects, for when you want to know what HAL does underneath.
- [Modern Embedded Systems Programming Course](https://www.youtube.com/playlist?list=PLPW8O6W-1chwyTzI3BHwBLbGQoPFxPAPM) — Quantum Leaps / Miro Samek (playlist) — Free course that starts from "what does the CPU really do" and works up to RTOS and event-driven frameworks. Companion page: [state-machine.com/video-course](https://www.state-machine.com/video-course).

## 2. STM32F446RE / Nucleo-F446RE official documents and STM32CubeIDE

- [STM32F446RE product page and datasheet](https://www.st.com/en/microcontrollers-microprocessors/stm32f446re.html) — STMicroelectronics — Pinout, memory sizes and electrical limits for the chip. Direct PDF: [stm32f446re.pdf](https://st.com/resource/en/datasheet/stm32f446re.pdf).
- [RM0390 Reference Manual — STM32F446xx (PDF)](https://www.st.com/resource/en/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — STMicroelectronics — The authoritative description of every peripheral register (ADC, DMA, TIM, IWDG, Flash, CRC). Search it by chapter, don't read it cover to cover.
- [UM1724 — STM32 Nucleo-64 boards user manual (PDF)](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf) — STMicroelectronics — Board-level details: which pins go to the headers, the on-board ST-LINK, the LED and button, and solder bridges.
- [NUCLEO-F446RE board page](https://www.st.com/en/evaluation-tools/nucleo-f446re.html) — STMicroelectronics — Board overview, schematics and all related documents in one place.
- [PM0214 — STM32 Cortex-M4 MCUs and MPUs Programming Manual (PDF)](https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf) — STMicroelectronics — The CPU core itself: registers, exceptions, NVIC, MPU, SCB fault registers and the instruction set.
- [Introduction to STM32CubeIDE (ST wiki)](https://wiki.st.com/stm32mcu/wiki/STM32CubeIDE:STM32CubeIDE_introduction) — ST wiki — Official walkthrough of the IDE. See also [Step 1: Tools installation](https://wiki.st.com/stm32mcu/wiki/STM32StepByStep:Step1_Tools_installation).
- [Getting Started with STM32 and Nucleo Part 1: Introduction to STM32CubeIDE and Blinky](https://www.youtube.com/watch?v=hyZS2p1tW-g) — DigiKey / Shawn Hymel (video) — Your first project from blank workspace to blinking LED, for beginners.

## 3. Arm Cortex-M architecture basics: registers, exceptions, NVIC, EXC_RETURN, stacking

- [A Practical Guide to ARM Cortex-M Exception Handling](https://interrupt.memfault.com/blog/arm-cortex-m-exceptions-and-nvic) — Interrupt (Memfault) blog — Clear explanation of the NVIC, priorities, what the CPU pushes on the stack, and what EXC_RETURN means.
- [ARM Cortex-M RTOS Context Switching](https://interrupt.memfault.com/blog/cortex-m-rtos-context-switching) — Interrupt (Memfault) blog — How PendSV, SysTick, MSP/PSP and stacking let FreeRTOS swap tasks. Ties sections 3 and 6 together.
- [Cortex-M4 Devices Generic User Guide (DUI0553)](https://developer.arm.com/docs/dui0553/b) — Arm Developer — Arm's own reference for the programmer's model, exception model and core peripherals.
- [Modern Embedded Systems Programming Course](https://www.youtube.com/playlist?list=PLPW8O6W-1chwyTzI3BHwBLbGQoPFxPAPM) — Quantum Leaps / Miro Samek (playlist) — The lessons on interrupts and the stack show exactly what happens in the debugger, register by register.

## 4. Cortex-M fault handling: HardFault, MemManage, BusFault, UsageFault, CFSR

- [How to debug a HardFault on an ARM Cortex-M MCU](https://interrupt.memfault.com/blog/cortex-m-hardfault-debug) — Interrupt (Memfault), Chris Coleman — Covers CFSR/HFSR/MMFAR/BFAR, rebuilding the call stack from stacked registers, and safe recovery. The single most useful article for this project.
- [Using Cortex-M3/M4/M7 Fault Exceptions — Application Note AN209 (PDF)](https://documentation-service.arm.com/static/61084e4a3d73a34b640e31eb) — Arm/Keil — Official description of each fault type and its status bits, with handler examples.
- [Debugging Hard Fault & Other Exceptions](https://freeRTOS.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html) — FreeRTOS.org — The well-known `prvGetRegistersFromStack()` handler that pulls PC/LR out of the fault stack frame.
- [Analyzing Cortex-M Faults using SEGGER's Ozone Debugger](https://www.youtube.com/watch?v=oL8qVAVMA0o) — SEGGER Microcontroller (video) — Watch a professional tool decode a fault, so you know what your own fault decoder is imitating.
- [How to Debug HardFaults on ARM Cortex-M MCUs — webinar recording](https://go.memfault.com/how-to-debug-hardfaults-on-arm-cortex-m-mcus-webinar-recording) — Memfault — Video version of the Interrupt article. [Slides (PDF)](https://go.memfault.com/hubfs/How%20to%20Debug%20HardFaults%20on%20ARM%20Cortex-M%20MCUs%20Memfault%20Webinar%20Presentation.pdf).

## 5. MPU (Memory Protection Unit), stack guards, null-pointer protection

- [Fix Bugs and Secure Firmware with the MPU](https://interrupt.memfault.com/blog/fix-bugs-and-secure-firmware-with-the-mpu) — Interrupt (Memfault) — Uses the MPU to catch null-pointer dereferences and stack overflows as MemManage faults.
- [AN4838 — Managing memory protection unit in STM32 MCUs (PDF)](https://www.st.com/resource/en/application_note/dm00272912-managing-memory-protection-unit-stm32-mcus-stmicroelectronics.pdf) — STMicroelectronics — ST's own guide to MPU regions, attributes and setup.
- [How to configure the MPU of an STM32 using STM32CubeMX](https://community.st.com/stm32-mcus-60/how-to-configure-the-mpu-of-an-stm32-using-stm32cubemx-269) — ST Community knowledge article — Point-and-click MPU setup in the CubeMX tool built into CubeIDE.
- [Measuring Stack Usage the Hard Way](https://interrupt.memfault.com/blog/measuring-stack-usage) — Interrupt (Memfault) — Stack painting and other ways to find out how close a task is to overflowing.

## 6. FreeRTOS fundamentals and CMSIS-RTOS v2

- [Introduction to RTOS](https://www.youtube.com/playlist?list=PLEBQazB0HUyQ4hAPU1cJED6t3DU0h34bz) — DigiKey / Shawn Hymel (playlist) — The best beginner RTOS series. Individual parts:
  - [Part 1 — What is a Real-Time Operating System?](https://www.youtube.com/watch?v=F321087yYy4) (video)
  - [Part 3 — Task Scheduling](https://www.youtube.com/watch?v=95yUbClyf3E) (video) — priorities and preemption
  - [Part 5 — Queue](https://www.youtube.com/watch?v=pHJ3lxOoWeI) (video)
  - [Part 6 — Mutex](https://www.youtube.com/watch?v=I55auRpbiTs) (video)
  - [Part 11 — Priority Inversion](https://www.youtube.com/watch?v=C2xKhxROmhA) (video)
  - Written companion: [What is a Real-Time Operating System (RTOS)?](https://www.digikey.com/en/maker/projects/what-is-a-realtime-operating-system-rtos/28d8087f53844decafa5000d89608016) — DigiKey Maker.io
- [Mastering the FreeRTOS Real Time Kernel — the official FreeRTOS book](https://www.freertos.org/Documentation/02-Kernel/07-Books-and-manual/01-RTOS_book) — FreeRTOS.org — Free official tutorial book. An up-to-date Markdown version is on [GitHub: FreeRTOS-Kernel-Book](https://github.com/FreeRTOS/freertos-kernel-book).
- [FreeRTOS scheduling](https://freertos.org/Documentation/02-Kernel/02-Kernel-features/01-Tasks-and-co-routines/04-Task-scheduling) — FreeRTOS.org — How priorities, preemption and time slicing decide which task runs.
- [FreeRTOS mutexes (priority inheritance)](https://freertos.org/Documentation/02-Kernel/02-Kernel-features/02-Queues-mutexes-and-semaphores/04-Mutexes) — FreeRTOS.org — Why a mutex temporarily raises the holder's priority.
- [Direct-to-task notifications](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/03-Direct-to-task-notifications/01-Task-notifications) — FreeRTOS.org — The fastest, lightest way to wake one task from an ISR.
- [Stack usage and stack overflow checking](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/09-Memory-management/02-Stack-usage-and-stack-overflow-checking) — FreeRTOS.org — `configCHECK_FOR_STACK_OVERFLOW` methods 1 and 2, and `vApplicationStackOverflowHook`.
- [Trace Hook Macros](https://freertos.org/Documentation/02-Kernel/02-Kernel-features/09-RTOS-trace-feature) — FreeRTOS.org — Documents `traceTASK_SWITCHED_IN()` and the other hooks this project uses to build execution signatures.
- [FreeRTOSConfig.h customization](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/02-Customization) — FreeRTOS.org — What every `config...` option in FreeRTOSConfig.h does.
- [CMSIS-RTOS2 API overview](https://arm-software.github.io/CMSIS_6/main/RTOS2/index.html) — Arm (CMSIS) — The `osThreadNew`/`osMessageQueue...` API that STM32CubeIDE wraps around FreeRTOS. See also [Using the CMSIS-RTOS2 interface](https://arm-software.github.io/CMSIS_6/main/RTOS2/usingOS2.html).
- [Mutex, Priority Inversion & Priority Inheritance Explained (FreeRTOS with Animation)](https://www.youtube.com/watch?v=6DQVJbrK7ZM) — Embedded DeepDive (video) — Animated explanation of a classic interview topic.
- [STM32 CMSIS-RTOS (Part 5): How to use Mutex to Fix Priority Inversion with Priority Inheritance](https://www.youtube.com/watch?v=ExquwgiF3sw) — ControllersTech (video) — The same idea shown on STM32 with the CMSIS-RTOS v2 API you are using.

## 7. DWT cycle counter (DWT->CYCCNT) for timing

- [Cycle Counting on ARM Cortex-M with DWT](https://mcuoneclipse.com/2017/01/30/cycle-counting-on-arm-cortex-m-with-dwt/) — MCU on Eclipse (Erich Styger) — The three lines of code that enable CYCCNT, and how to turn cycles into microseconds.
- [Measuring Code Execution Time on ARM Cortex-M MCUs](https://iar.com/knowledge/learn/programming/measuring-code-execution-time-on-arm-cortex-m-mcus) — IAR — Compares timing methods (GPIO toggle, SysTick, DWT) and their accuracy.
- [Measuring the embOS Context Switch Time with Cortex-M and the DWT Cycle Counter](https://kb.segger.com/Measuring_the_embOS_Context_Switch_Time_with_Cortex-M_and_the_DWT_Cycle_Counter) — SEGGER Knowledge Base — A real-world example of timing RTOS context switches with CYCCNT, which is close to what this project does.
- [Profiling Firmware on Cortex-M](https://interrupt.memfault.com/blog/profiling-firmware-on-cortex-m) — Interrupt (Memfault) — Uses DWT and ITM to find where CPU time goes.

## 8. Watchdog timers (IWDG on STM32, good watchdog design)

- [Great Watchdog Timers For Embedded Systems](https://www.ganssle.com/watchdogs.htm) — Jack Ganssle — The classic essay on why naive watchdogs fail and what a robust one looks like.
- [A Guide to Watchdog Timers for Embedded Systems](https://interrupt.memfault.com/blog/firmware-watchdog-best-practices) — Interrupt (Memfault) — Modern take on the same topic, including task-level "check-in" watchdogs with an RTOS.
- [A Designer's Guide to Watchdog Timers](https://www.digikey.com/en/articles/a-designers-guide-to-watchdog-timers) — DigiKey — Easy introduction to internal and external watchdogs.
- [IWDG and WWDG in STM32](https://controllerstech.com/iwdg-and-wwdg-in-stm32/) — ControllersTech — CubeMX setup and HAL code for both STM32 watchdogs.
- [STM32 Tutorial #29 — Taming the DOG: Using the Independent Watchdog (IWDG)](https://www.youtube.com/watch?v=wMfDr_AQFko) — STM32World (video) — Hands-on IWDG demo. [Written notes](https://stm32world.com/wiki/TAMING_the_Dog_(IWDG)).

## 9. STM32 ADC + DMA, timer-triggered ADC, DAC, PWM, UART interrupts

- [Getting started with ADC](https://wiki.st.com/stm32mcu/wiki/Getting_started_with_ADC) — ST wiki — Official beginner lab for the ADC.
- [Getting Started With STM32 & Nucleo Part 4: Working with ADC and DMA](https://www.youtube.com/watch?v=EsZLgqhqfO0) — DigiKey / Maker.io (video) — Explains why DMA frees the CPU when you sample an ADC.
- [STM32H7 ADC + DMA + Timer Firmware Tutorial — Phil's Lab #138](https://www.youtube.com/watch?v=_K3GvQkyarg) — Phil's Lab (video) — Timer-triggered ADC sampling into a DMA buffer. The chip is an H7, but the idea carries straight over to the F446.
- [STM32 ADC Multiple Channels with Circular DMA](https://controllerstech.com/stm32-adc-multiple-channels-circular-dma/) — ControllersTech — CubeMX and HAL code for continuous multi-channel ADC.
- [STM32 ADC Timer Trigger Example & External Trigger Sources](https://deepbluembedded.com/stm32-adc-timer-trigger-example-external-trigger-sources/) — DeepBlueEmbedded — Fixed-rate sampling with a timer TRGO.
- [Getting started with DAC](https://wiki.st.com/stm32mcu/wiki/Getting_started_with_DAC) — ST wiki — Producing an analog voltage, used here to emulate a plant output.
- [Getting started with TIM (timers and PWM)](https://wiki.st.com/stm32mcu/wiki/Getting_started_with_TIM) — ST wiki — Timer basics, including PWM output.
- [STM32 PWM Output: Generate PWM Signal with & without DMA](https://controllerstech.com/pwm-with-dma-in-stm32) — ControllersTech — Practical PWM setup.
- [Getting started with UART](https://wiki.st.com/stm32mcu/wiki/Getting_started_with_UART) — ST wiki — Official UART lab.
- [How to Receive UART Data in Blocking & Interrupt mode](https://controllerstech.com/stm32-uart-3-receive-data-in-blocking-interrupt-mode/) — ControllersTech — `HAL_UART_Receive_IT` and the receive-complete callback.

## 10. Writing to STM32 internal flash from firmware

- [Flash Programming in STM32](https://controllerstech.com/flash-programming-in-stm32) — ControllersTech — Unlock, erase a sector, program words, lock, using HAL on F4-series chips.
- [RM0390 Reference Manual (Embedded Flash memory chapter)](https://www.st.com/resource/en/reference_manual/rm0390-stm32f446xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — STMicroelectronics — Sector map of the F446 (sectors of different sizes) and the program/erase rules.
- [ST example: FLASH_EraseProgram (STM324xG_EVAL main.c)](https://sourcevu.sysprogs.com/stm32/examples/STM324xG_EVAL/Examples/FLASH/FLASH_EraseProgram/files/Src/main.c) — ST example code (Sysprogs mirror) — ST's official erase-and-program sample for STM32F4.

## 11. CRC-32

- [Cyclic redundancy check](https://en.wikipedia.org/wiki/Cyclic_redundancy_check) — Wikipedia — Plain introduction: a CRC is a "fingerprint" that changes if any bit of the data changes. Also see [Computation of CRCs / CRC-32](https://en.wikipedia.org/wiki/CRC-32).
- [How do CRCs work?](https://www.youtube.com/watch?v=izG7qT0EpBw) — Ben Eater (video) — Visual, from-first-principles explanation of polynomial division.
- [A Painless Guide to CRC Error Detection Algorithms (Ross Williams)](https://pic.hallikainen.org/techref/method/math/crcguide.html) — mirror of the classic 1993 text — The standard reference for how table-driven CRC-32 is implemented.
- [AN4187 — Using the CRC peripheral in the STM32 family (PDF)](https://www.st.com/resource/en/application_note/an4187-using-the-crc-peripheral-in-the-stm32-family-stmicroelectronics.pdf) — STMicroelectronics — The F446's hardware CRC unit, and how its result relates to the standard CRC-32 used on a PC.

## 12. PID control basics, first-order plant models, hardware-in-the-loop (HIL)

- [Understanding PID Control](https://www.youtube.com/playlist?list=PLn8PRpmsu08pQBgjxYFXSsODEF3Jqmm-y) — MATLAB / Brian Douglas (playlist) — Very clear, intuition-first PID series. Start with [Part 1: What Is PID Control?](https://www.youtube.com/watch?v=wkfEZmsQqiA) (video).
- [PID Without a PhD](https://dsprelated.com/showarticle/943/pid-without-a-phd) — Tim Wescott (DSPRelated) — The classic, code-first article on writing a PID loop in C.
- [First Order Systems | Control Systems 2.2](https://www.circuitbread.com/tutorials/first-order-systems-2-2) — CircuitBread — Time constant and step response of a first-order plant, explained simply.
- [What Is Hardware-in-the-Loop (HIL)?](https://in.mathworks.com/discovery/hardware-in-the-loop-hil.html) — MathWorks — What HIL testing is and why industry uses it, which is the idea behind emulating the plant on the board.

## 13. Runtime tracing and observability tools used in industry

- [SEGGER SystemView](https://www.segger.com/products/development-tools/systemview/) — SEGGER — Free real-time recording of task switches and interrupts over the debug probe. [Knowledge-base page](https://kb.segger.com/SystemView).
- [How to integrate FreeRTOS and SystemView inside a project | Part 1](https://www.youtube.com/watch?v=D4FTyJrGyoI) — Learn embedded systems with George (video) — Step-by-step SystemView on STM32 + FreeRTOS.
- [Percepio Tracealyzer for FreeRTOS](https://percepio.com/tracealyzer/freertostrace/) — Percepio — Commercial trace visualizer built on the same FreeRTOS trace hooks this project uses.
- [Getting started with Tracealyzer for FreeRTOS](https://www.youtube.com/watch?v=nw3sEkfbTI0) — Percepio AB (video) — Short demo of what a trace looks like.
- [Percepio DevAlert](https://percepio.com/devalert/) — Percepio — Remote fault and anomaly alerts from deployed devices, the industry version of "detect and report a fault automatically".
- [Memfault MCU — Introduction](https://docs.memfault.com/docs/mcu/introduction) and [Coredump Collection](https://docs.memfault.com/docs/mcu/coredumps) — Memfault docs — How a commercial platform captures crash data and metrics from many devices.
- [Embedded Trace Visualizers (Tracealyzer / SystemView)](https://www.beningo.com/embedded-technology-atlas/2026/pages/045-embedded-trace-visualizers-tracealyzer-systemview.html) — Beningo Embedded Group — Side-by-side comparison: SystemView is free with SEGGER probes, Tracealyzer is licensed but supports more RTOSes and deeper analysis.
- [A Deep Dive into ARM Cortex-M Debug Interfaces](https://interrupt.memfault.com/blog/a-deep-dive-into-arm-cortex-m-debug-interfaces) — Interrupt (Memfault) — Explains SWD, SWO, ITM, DWT and ETM, i.e. what hardware trace exists on the chip.
- [ETM trace (Arm Learning Path, Keil µVision debug)](https://learn.arm.com/learning-paths/embedded-and-microcontrollers/uv_debug/5_etm_trace/) — Arm Learning Paths — What full instruction trace (ETM) gives you, and why it needs extra hardware.

## 14. Statistics and ML for anomaly detection

- [Accurately computing sample variance online](https://www.johndcook.com/blog/standard_deviation/) — John D. Cook — Welford's running mean/variance in a few lines. This is the algorithm for building a baseline on the MCU without storing every sample.
- [Algorithms for calculating variance (Welford's online algorithm)](https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance) — Wikipedia — Formulas and pseudo-code, plus why the naive method loses precision.
- [Standard Deviations and Z-Scores Explained](https://www.youtube.com/watch?v=HWCwBtUUWKY) (video) — Beginner explanation of "how many standard deviations away is this value?" (channel name not shown in search results).
- [Mahalanobis Distance — Understanding the math with examples (python)](https://machinelearningplus.com/statistics/mahalanobis-distance/) — Machine Learning Plus — A z-score for several correlated features at once, with worked examples.
- [IsolationForest — scikit-learn documentation](https://scikit-learn.org/stable/modules/generated/sklearn.ensemble.IsolationForest.html) — scikit-learn — API reference. Also the [IsolationForest example](https://scikit-learn.org/stable/auto_examples/ensemble/plot_isolation_forest.html).
- [Isolation Forest (Liu, Ting, Zhou — IEEE ICDM 2008)](https://ieeexplore.ieee.org/abstract/document/4781136) — IEEE Xplore — The original paper. The key idea: anomalies are easier to isolate with random splits.
- [Isolation Forests: Identify Outliers in Data](https://www.youtube.com/watch?v=Y1x51i1936M) — Elder Research (video) — Intuitive walkthrough of the isolation idea.
- [Decision Trees — scikit-learn User Guide](https://scikit-learn.org/stable/modules/tree.html) — scikit-learn — How trees split data. Small trees can be exported as if/else code for an MCU.
- [Decision and Classification Trees, Clearly Explained!!!](https://www.youtube.com/watch?v=_L39rN6gz7Y) — StatQuest with Josh Starmer (video) — The friendliest decision-tree explanation available.
- [Tiny Machine Learning (TinyML) Professional Certificate](https://www.edx.org/professional-certificate/harvardx-tiny-machine-learning) — HarvardX / edX — Free-to-audit course on running ML on microcontrollers. Related: [TinyMLedu courses](http://tinyml.seas.harvard.edu/courses/).

## 15. Research on fault localization and anomaly detection in embedded systems

- [A Survey on Software Fault Localization (Wong, Gao, Li, Abreu, Wotawa — IEEE TSE 2016)](https://ieeexplore.ieee.org/document/7390282/) — IEEE Xplore — The standard survey of spectrum-based and other fault-localization techniques.
- [Time-Based Intrusion Detection in Cyber-Physical Systems (Zimmer, Bhat, Mueller, Mohan — ICCPS 2010)](https://impact.ornl.gov/en/publications/time-based-intrusion-detection-in-cyber-physical-systems/) — ORNL record — Uses execution-time bounds of real-time tasks to detect abnormal behaviour. Closest classic paper to "timing signatures".
- [Statistical Time-based Intrusion Detection in Embedded Systems (Carreón, Gilbreath, Lysecky — DATE 2020)](https://past.date-conference.com/proceedings-archive/2020/html/0337.html) — DATE conference — Models normal timing with distributions and flags deviations. [PDF](https://past.date-conference.com/proceedings-archive/2020/pdf/0337.pdf).
- [Data-driven anomaly detection with timing features for embedded systems (Lu, Lysecky — ACM TODAES 2019)](https://experts.arizona.edu/en/publications/data-driven-anomaly-detection-with-timing-features-for-embedded-s/) — University of Arizona record — Uses execution-time breakdowns as ML features (one-class SVM).
- [Machine Learning Enhanced Real-Time Intrusion Detection Using Timing Information (Xu, Mueller, Acharya, Kucheria) (PDF)](https://arcb.csc.ncsu.edu/~mueller/ftp/pub/mueller/papers/trec4cps18.pdf) — NC State (author copy) — Combines timing bounds with ML predictions on a separate monitor board.
- [Anomaly Detection: A Survey (Chandola, Banerjee, Kumar — ACM Computing Surveys 2009)](https://www-users.cse.umn.edu/~kumar001/papers/anomaly-survey.php) — University of Minnesota — The most-cited general survey of anomaly detection methods.
- [Basic Concepts and Taxonomy of Dependable and Secure Computing (Avižienis, Laprie, Randell, Landwehr) (PDF)](https://www.inf.ed.ac.uk/teaching/courses/seoc/2006_2007/resources/BasicConcepts.pdf) — Univ. of Edinburgh course copy — Defines fault vs error vs failure precisely. Worth quoting in your report.

## 16. MPU6050 IMU, I2C basics, STM32 I2C, potentiometer as a voltage divider

- [MPU-6000/MPU-6050 Register Map and Descriptions (PDF)](https://shop.cpu.com.tw/upload/2013/03/RM-MPU-6000A.pdf) — InvenSense document (distributor-hosted copy) — Register addresses (WHO_AM_I 0x75, PWR_MGMT_1 0x6B, ACCEL_XOUT_H 0x3B…).
- [MPU-6050 Product Specification / datasheet (PDF)](https://alliance.seas.upenn.edu/~medesign/wiki/uploads/Guides/MPU6050_datasheet.pdf) — InvenSense datasheet (UPenn course copy) — Electrical specs, I2C timing and ranges. (TDK no longer hosts an obvious public MPU-6050 page.)
- [Interface MPU6050 (GY-521) with STM32](https://controllerstech.com/how-to-interface-mpu6050-gy-521-with-stm32/) — ControllersTech — HAL I2C code for reading accelerometer and gyro data.
- [I2C — SparkFun Learn](https://learn.sparkfun.com/tutorials/i2c/all) — SparkFun — Friendly introduction to SDA/SCL, addresses, ACK and pull-ups.
- [Understanding the I2C Bus (SLVA704)](https://ti.com/lit/SLVA704) — Texas Instruments — Short, authoritative app note on open-drain signalling and the I2C frame format.
- [Understanding I2C](https://www.youtube.com/watch?v=CAvawEcxoPU) — Rohde & Schwarz (video) — I2C explained with real oscilloscope captures.
- [Getting started with I2C](https://wiki.st.com/stm32mcu/wiki/Getting_started_with_I2C) — ST wiki — Official STM32 I2C lab.
- [Getting Started With STM32 and Nucleo Part 2: How to Use I2C to Read Temperature Sensor TMP102](https://www.youtube.com/watch?v=isOekyygpR8) — DigiKey / Shawn Hymel (video) — HAL I2C read on a Nucleo, step by step.
- [Voltage Dividers](https://learn.sparkfun.com/tutorials/voltage-dividers/all) — SparkFun — Why a potentiometer between 3.3 V and GND gives an ADC a 0–3.3 V signal.

## 17. Interview prep: embedded C and RTOS

- [A 'C' Test: The 0x10 Best Questions for Would-be Embedded Programmers (Nigel Jones)](https://www.microcontrollertips.com/0x10-best-questions-embedded-programmers/) — Microcontroller Tips — The classic list: `volatile`, `const`, bit manipulation, ISRs, pointers.
- [Interview Question Breakdown: Bad C Analysis](https://embeddedartistry.com/blog/2017/06/05/interview-question-breakdown-bad-c-analysis/) — Embedded Artistry — How to reason out loud about buggy C code, a common interview format.
- [For Beginners](https://embeddedartistry.com/beginners/) — Embedded Artistry — A curated learning path and reading list for new embedded engineers.
- [BARR-C:2018 Embedded C Coding Standard (free PDF)](https://barrgroup.com/embedded-c-coding-standard) — Barr Group — Industry coding rules. Quoting them in an interview shows you write production-quality C.
- [Introduction to RTOS Part 11 — Priority Inversion](https://www.youtube.com/watch?v=C2xKhxROmhA) and [Part 7 — Semaphore](https://www.youtube.com/watch?v=5JcMtbA9QEE) — DigiKey / Shawn Hymel (videos) — Covers the mutex vs semaphore and priority inversion questions that come up in almost every RTOS interview.
- [Mutex, Priority Inversion & Priority Inheritance Explained](https://www.youtube.com/watch?v=6DQVJbrK7ZM) — Embedded DeepDive (video) — Quick animated revision before an interview.
- [How to debug a HardFault on an ARM Cortex-M MCU](https://interrupt.memfault.com/blog/cortex-m-hardfault-debug) — Interrupt (Memfault) — Be ready for "what happens when your MCU crashes and how do you find out why?"

---

## How these were checked

- All links were found with live web searches on **2026-10-19**. Every URL here appeared in a search result, and none was typed from memory or guessed.
- For YouTube, a video or playlist is listed only if its exact URL showed up in search results. For most videos the title and channel were also confirmed by opening the page. One video's channel could not be confirmed; its entry says so.
- Some documents were opened to confirm the title and content: Arm AN209, the Interrupt HardFault article, the Ganssle watchdog essay, the Beningo comparison page, and the research-paper records. The rest were checked from search-result titles and URLs.
- A few documents are hosted by third parties (universities or distributors) because the vendor no longer hosts them publicly, for example the MPU-6050 datasheet and register map and the Ross Williams CRC guide. These are labeled.
- Links change over time. ST and FreeRTOS.org reorganize their sites now and then. If a link breaks, search for the document number (RM0390, UM1724, PM0214, AN4838, AN4187, AN209, SLVA704) or the exact title.
