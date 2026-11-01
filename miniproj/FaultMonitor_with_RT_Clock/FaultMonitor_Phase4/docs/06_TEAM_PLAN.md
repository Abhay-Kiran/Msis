# TEAM PLAN — 19 Oct → 27 Oct 2026 (final evaluation Tue 27 Oct)

The code for every module is written and compiles. The remaining work is: get it
running on the board, measure it, understand it well enough to defend it, and
rehearse. Each person owns one layer and is the one who answers questions on it.

| person | owns | folder | answers questions on |
|---|---|---|---|
| **Abhay** | capture | `Core/Src/monitor`, fm_types.h event/signature, FreeRTOSConfig hooks, linker `.noinit`, fault handlers | how we observe: hooks, DWT, ring buffer, signature, MPU, traps, stack |
| **Arfan** | detection | `Core/Src/detect`, fm_types.h report/baseline/localizer, tools/fm_ml.py, fm_report.py | how we decide: learning, rules, statistics, confidence, black box, recovery, watchdog, ML comparison |
| **Abhishek** | application, injection, tools | `Core/Src/app`, `Core/Src/inject`, main.c, tools/ | what we monitor and how we prove it: controller, peripherals, faults, shell, campaign, metrics |

Rule from the team: every request to Claude starts with your name ("Arfan - ...").

## Week view

| days | goal | done when |
|---|---|---|
| Mon 19 | code complete (done) | both builds compile, unit tests pass, Part 1 + Part 2 delivered |
| Tue 20 – Wed 21 | bring-up on hardware | every demo in GUIDEBOOK §4 works once; bugs fixed |
| Thu 22 – Fri 23 | measure | campaign (rule + stat), FP run, overhead ON/OFF, ML results, tables in the report |
| Sat 24 – Sun 25 | understand + write | each person can explain every line of their folder; report + slides done |
| Mon 26 | rehearse | two full mock evaluations with timing; backup plan tested |
| Tue 27 | evaluation | |

## Day by day

### Mon 19 Oct — code complete ✅
* All: read README and GUIDEBOOK; import the project in CubeIDE; build both configs.
* Achieved today: full firmware (capture, learning, rule + statistical detection,
  flash black box, recovery ladder, watchdog, MPU traps, process controller, 10 fault
  types, examiner commands), PC tools, docs, per-person modules (Part 2).

### Tue 20 Oct — first power-on (all three at one board)
* **Abhishek:** flash `prebuilt/FaultMonitor_monitor_off.bin` first: app alone.
  Check telemetry PV follows setpoint, LED brightness, button, `T`, `Y`, `I` list.
  Then the ON build. GUIDEBOOK §6 checklist steps 1–3.
* **Abhay:** `S` table sane for every component (runs: ctrl 10, adc_isr 10,
  tim6_isr ~100, report 1, shell 1, monitor 1, wdg 0–1 per window); `D` dump parsed
  by `fm_dump.py dump`; `V 1` stream recorded. Checklist steps 4–5.
* **Arfan:** `LEARN done`, `B` baseline plausible; 10 min with no false alarm; then
  demos 2–5.
* Achievement: board runs the application and the monitor; first faults detected.

### Wed 21 Oct — all fault classes
* **Abhay:** traps: demos 9–11 (stack overflow, read-only write, NULL) — check
  `!TRAP` CFSR/address values against PM0214 tables; demo 12 (watchdog).
* **Arfan:** demos 6–8 (peripherals, UART), 15 (`M 1`); tune `K` only if needed and
  write down why.
* **Abhishek:** demo 13–14 (black box survives power-off), 16–17 (examiner).
  Fix anything found; rebuild; re-run the failing demo.
* Achievement: all 17 demos pass, list of fixes committed to git.

### Thu 22 Oct — measurements
* **Abhishek:** `fm_campaign.py --reps 5 --normal 600` for rule and stat →
  `fm_metrics.py` tables.
* **Abhay:** overhead: ON vs OFF idle counts, RAM (`OVH` line, `arm-none-eabi-size`),
  trace storage = 8 KB ring + 164 B/report; per-event hook cost with DWT if time.
* **Arfan:** stream 15 min of windows with hand-injected faults; `fm_ml.py` →
  Isolation Forest + decision tree vs on-chip; export the tree to C (optional
  on-chip third localizer).
* Achievement: every evaluation metric in the problem statement has a number.

### Fri 23 Oct — results into the report
* Each person writes their chapter (design, implementation, results) from
  docs/02 and docs/03; tables from Thursday; screenshots of the console.
* Achievement: report draft complete.

### Sat 24 Oct — understand the code (viva preparation)
* Each person: read docs/03_FILE_BY_FILE.md for your folder and the learning module
  in Part 2 (`<name>/LEARNING.md`); explain each function aloud to the other two.
* Achievement: each can answer "what does this line do" for their files.

### Sun 25 Oct — cross-training + slides
* Each person explains one other person's layer (examiners ask anyone anything).
* docs/04_LEARNING_BOOK.md Part G: quiz each other on the 60 questions.
* Slides: problem → architecture → live demo → results → comparison → limits.
* Achievement: slides done; everyone has answered all 60 questions once.

### Mon 26 Oct — rehearsal
* Two full mock evaluations (20 min each): demo order GUIDEBOOK §4, 1-2-4-5-6-8-9-12-13-15.
* Backup: a second board flashed; a screen recording of a full demo; printed
  `R` output and metrics tables, in case the live demo misbehaves.
* Achievement: demo runs in under 10 minutes without surprises.

### Tue 27 Oct — evaluation
* Arrive early, flash, learn (10 s), `C` to clear old reports, `Y 0`, start.

## Achievement tracker (fill in)

| date | Abhay | Arfan | Abhishek |
|---|---|---|---|
| 19 Oct | capture layer written (fm_trace, fm_capture, ring buffer, sigout) | learn/detect/blackbox/recover/watchdog/monitor written; unit tests pass | app, UART, shell, injector, tools written |
| 20 Oct | | | |
| 21 Oct | | | |
| 22 Oct | | | |
| 23 Oct | | | |
| 24 Oct | | | |
| 25 Oct | | | |
| 26 Oct | | | |
