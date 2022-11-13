#pragma once

#include "project.h"
#include "scheduler.h"

typedef enum {
  TASK_GYRO,
  TASK_MAIN,
  TASK_RX,
  TASK_USB,
  TASK_OSD,
#ifdef ENABLE_BLACKBOX
  TASK_BLACKBOX,
#endif
  TASK_VTX,

  TASK_MAX

} task_id_t;

extern task_t tasks[TASK_MAX];