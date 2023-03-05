#include "io/blackbox.h"

#include "driver/time.h"
#include "flight/control.h"
#include "io/data_flash.h"
#include "io/usb_configurator.h"
#include "util/cbor_helper.h"
#include "util/util.h"

#ifdef ENABLE_BLACKBOX

static blackbox_t blackbox;
static uint8_t blackbox_enabled = 0;

cbor_result_t cbor_encode_blackbox_t(cbor_value_t *enc, const blackbox_t *b) {
  CBOR_CHECK_ERROR(cbor_result_t res = cbor_encode_array_indefinite(enc));

  CBOR_CHECK_ERROR(res = cbor_encode_uint32(enc, &b->loop));
  CBOR_CHECK_ERROR(res = cbor_encode_uint32(enc, &b->time));

  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->pid_p_term));
  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->pid_i_term));
  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->pid_d_term));

  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec4_t(enc, &b->rx));
  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec4_t(enc, &b->setpoint));

  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->accel_raw));
  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->accel_filter));

  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->gyro_raw));
  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec3_t(enc, &b->gyro_filter));

  CBOR_CHECK_ERROR(res = cbor_encode_compact_vec4_t(enc, &b->motor));

  CBOR_CHECK_ERROR(res = cbor_encode_uint16(enc, &b->cpu_load));

  CBOR_CHECK_ERROR(res = cbor_encode_array(enc, 4));
  CBOR_CHECK_ERROR(res = cbor_encode_int16(enc, &b->debug[0]));
  CBOR_CHECK_ERROR(res = cbor_encode_int16(enc, &b->debug[1]));
  CBOR_CHECK_ERROR(res = cbor_encode_int16(enc, &b->debug[2]));
  CBOR_CHECK_ERROR(res = cbor_encode_int16(enc, &b->debug[3]));

  CBOR_CHECK_ERROR(res = cbor_encode_end_indefinite(enc));

  return res;
}

void blackbox_init() {
  data_flash_init();
}

void blackbox_set_debug(uint8_t index, int16_t data) {
  if (index >= 4) {
    return;
  }

  blackbox.debug[index] = data;
}

void blackbox_update() {
  data_flash_result_t flash_result = data_flash_update();

  if (flash_result == DATA_FLASH_DETECT || flash_result == DATA_FLASH_STARTING) {
    // flash is still detecting, dont do anything
    return;
  }

  // flash is either idle or writing, do blackbox

  if ((!flags.arm_switch || !rx_aux_on(AUX_BLACKBOX)) && blackbox_enabled == 1) {
    data_flash_finish();
    blackbox_enabled = 0;
    return;
  } else if ((flags.arm_switch && flags.turtle_ready == 0 && rx_aux_on(AUX_BLACKBOX)) && blackbox_enabled == 0) {
    if (data_flash_restart(BLACKBOX_RATE, state.looptime_autodetect)) {
      blackbox_enabled = 1;
    }
    return;
  }

  if (blackbox_enabled == 0) {
    return;
  }

  blackbox.loop = state.loop_counter / BLACKBOX_RATE;
  blackbox.time = time_micros();

  vec3_compress(&blackbox.pid_p_term, &state.pid_p_term, BLACKBOX_SCALE);
  vec3_compress(&blackbox.pid_i_term, &state.pid_i_term, BLACKBOX_SCALE);
  vec3_compress(&blackbox.pid_d_term, &state.pid_d_term, BLACKBOX_SCALE);

  vec4_compress(&blackbox.rx, &state.rx, BLACKBOX_SCALE);

  blackbox.setpoint.axis[0] = state.setpoint.axis[0] * BLACKBOX_SCALE;
  blackbox.setpoint.axis[1] = state.setpoint.axis[1] * BLACKBOX_SCALE;
  blackbox.setpoint.axis[2] = state.setpoint.axis[2] * BLACKBOX_SCALE;
  blackbox.setpoint.axis[3] = state.throttle * BLACKBOX_SCALE;

  vec3_compress(&blackbox.gyro_filter, &state.gyro, BLACKBOX_SCALE);
  vec3_compress(&blackbox.gyro_raw, &state.gyro_raw, BLACKBOX_SCALE);

  vec3_compress(&blackbox.accel_filter, &state.accel, BLACKBOX_SCALE);
  vec3_compress(&blackbox.accel_raw, &state.accel_raw, BLACKBOX_SCALE);

  vec4_compress(&blackbox.motor, &state.motor_mix, BLACKBOX_SCALE);

  blackbox.cpu_load = state.cpu_load;

  if (blackbox_enabled != 0 && (state.loop_counter % BLACKBOX_RATE) == 0) {
    data_flash_write_backbox(&blackbox);
  }
}
#else
void blackbox_set_debug(uint8_t index, int16_t data) {}
#endif