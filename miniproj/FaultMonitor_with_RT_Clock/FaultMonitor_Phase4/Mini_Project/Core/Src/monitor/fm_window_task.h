/*
 * fm_window_task.h -- capture-only window task. Owner: Abhay.
 *
 * Used when the framework is built with FM_ENABLE_DETECT = 0 (capture
 * merged, detection not yet, or measuring capture overhead alone). Does
 * the capture half of the monitor's job: every FM_WINDOW_MS it closes the
 * window, adds free stack, and publishes it for 'S' and 'V'. No learning,
 * detection, storage or recovery. With detection built in, the real
 * monitor (detect/fm_monitor.c) does all this and this file compiles to
 * nothing.
 */
#ifndef FM_WINDOW_TASK_H
#define FM_WINDOW_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fm_window_task_init(uint8_t self_comp_id);

#ifdef __cplusplus
}
#endif

#endif /* FM_WINDOW_TASK_H */
