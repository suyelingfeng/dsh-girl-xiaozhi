#pragma once
#include <esp_lvgl_port.h>

// Worker-to-display notification only. Event bits coalesce, so slow rendering
// cannot build an unbounded queue. No LVGL object is touched from the worker.
inline void DshAnimationNotifyFrame() { lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr); }
