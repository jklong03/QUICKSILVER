#include "vtx.h"

#include <stddef.h>

#include "control.h"
#include "drv_gpio.h"
#include "drv_serial.h"
#include "drv_serial_vtx_sa.h"
#include "drv_serial_vtx_tramp.h"
#include "drv_time.h"
#include "project.h"
#include "rx.h"
#include "usb_configurator.h"
#include "util.h"
#include "util/cbor_helper.h"

#if defined(FPV_ON) && defined(FPV_PIN)
static int fpv_init = 0;
#endif

#define VTX_DETECT_TRIES 30

vtx_settings_t vtx_settings;
uint8_t vtx_connect_tries = 0;

const uint16_t frequency_table[VTX_BAND_MAX][VTX_CHANNEL_MAX] = {
    {5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725}, // VTX_BAND_A
    {5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866}, // VTX_BAND_B
    {5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945}, // VTX_BAND_E
    {5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880}, // VTX_BAND_F
    {5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917}, // VTX_BAND_R
};

uint16_t vtx_frequency_from_channel(vtx_band_t band, vtx_channel_t channel) {
  return frequency_table[band][channel];
}

int8_t vtx_find_frequency_index(uint16_t frequency) {
  for (uint8_t band = 0; band < VTX_BAND_MAX; band++) {
    for (uint8_t channel = 0; channel < VTX_CHANNEL_MAX; channel++) {
      if (frequency == frequency_table[band][channel]) {
        return band * VTX_CHANNEL_MAX + channel;
      }
    }
  }
  return -1;
}

uint8_t has_vtx_configured() {
  return serial_smart_audio_port == profile.serial.smart_audio && serial_smart_audio_port != USART_PORT_INVALID;
}

#ifdef ENABLE_SMART_AUDIO

extern smart_audio_settings_t smart_audio_settings;
uint8_t smart_audio_detected = 0;

const uint8_t smart_audio_dac_power_level[4] = {
    7,
    16,
    25,
    40,
};

int8_t smart_audio_dac_power_level_index(uint8_t dac) {
  for (uint8_t level = 0; level < VTX_POWER_LEVEL_MAX; level++) {
    if (dac == smart_audio_dac_power_level[level]) {
      return level;
    }
  }
  return -1;
}

void vtx_smart_audio_update() {
  vtx_update_result_t result = serial_smart_audio_update();

  if ((result == VTX_IDLE || result == VTX_ERROR) && smart_audio_settings.version == 0 && vtx_connect_tries <= VTX_DETECT_TRIES) {
    // no smart audio detected, try again
    serial_smart_audio_send_payload(SA_CMD_GET_SETTINGS, NULL, 0);
    vtx_connect_tries++;
    return;
  }

  static vtx_settings_t actual;

  if (result == VTX_SUCCESS) {
    int8_t channel_index = -1;
    if (smart_audio_settings.mode & SA_MODE_FREQUENCY) {
      channel_index = vtx_find_frequency_index(smart_audio_settings.frequency);
    } else {
      channel_index = smart_audio_settings.channel;
    }

    if (channel_index >= 0) {
      actual.band = channel_index / VTX_CHANNEL_MAX;
      actual.channel = channel_index % VTX_CHANNEL_MAX;
    }

    if (smart_audio_settings.version >= 2) {
      actual.power_level = smart_audio_settings.power;
    } else {
      actual.power_level = smart_audio_dac_power_level_index(smart_audio_settings.power);
    }

    if (smart_audio_settings.version >= 3) {
      actual.pit_mode = smart_audio_settings.mode & SA_MODE_PIT ? 1 : 0;
    } else {
      actual.pit_mode = VTX_PIT_MODE_NO_SUPPORT;
    }

    if (smart_audio_settings.version != 0 && smart_audio_detected == 0) {
      quic_debugf("smart audio version: %d", smart_audio_settings.version);
      smart_audio_detected = 1;
      vtx_settings = actual;
      vtx_settings.detected = 1;
      vtx_connect_tries = 0;
    }
    return;
  }

  if ((result == VTX_IDLE || result == VTX_ERROR) && smart_audio_detected) {

    static uint8_t frequency_tries = 0;
    if (actual.band != vtx_settings.band || actual.channel != vtx_settings.channel) {
      if (frequency_tries >= VTX_DETECT_TRIES) {
        // give up
        vtx_settings.band = actual.band;
        vtx_settings.channel = actual.channel;
        frequency_tries = 0;
        return;
      }

      if (smart_audio_settings.mode & SA_MODE_FREQUENCY) {
        const uint16_t frequency = frequency_table[vtx_settings.band][vtx_settings.channel];
        const uint8_t payload[2] = {
            (frequency >> 8) & 0xFF,
            frequency & 0xFF,
        };
        serial_smart_audio_send_payload(SA_CMD_SET_FREQUENCY, payload, 2);
      } else {
        const uint8_t index = vtx_settings.band * VTX_CHANNEL_MAX + vtx_settings.channel;
        const uint8_t payload[1] = {index};
        serial_smart_audio_send_payload(SA_CMD_SET_CHANNEL, payload, 1);
      }
      frequency_tries++;
      return;
    } else {
      frequency_tries = 0;
    }

    static uint8_t power_level_tries = 0;
    if (actual.power_level != vtx_settings.power_level) {
      if (power_level_tries >= VTX_DETECT_TRIES) {
        // give up
        vtx_settings.power_level = actual.power_level;
        power_level_tries = 0;
        return;
      }

      uint8_t level = vtx_settings.power_level;
      if (smart_audio_settings.version < 2) {
        level = smart_audio_dac_power_level[vtx_settings.power_level];
      }
      serial_smart_audio_send_payload(SA_CMD_SET_POWER, &level, 1);
      power_level_tries++;
      return;
    } else {
      power_level_tries = 0;
    }

    static uint8_t pit_mode_tries = 0;
    if (actual.pit_mode != vtx_settings.pit_mode) {
      if (pit_mode_tries >= VTX_DETECT_TRIES) {
        // give up
        vtx_settings.pit_mode = actual.pit_mode;
        pit_mode_tries = 0;
        return;
      }

      if (smart_audio_settings.version >= 3) {
        uint8_t mode = 0x0;

        /* looks like in-range/out-range was dropped for VTXes with SA >= v2.1
        if (smart_audio_settings.mode & SA_MODE_OUT_RANGE_PIT) {
          mode |= 0x02;
        } else {
          // default to SA_MODE_IN_RANGE_PIT
          mode |= 0x01;
        }
        */

        if (vtx_settings.pit_mode == VTX_PIT_MODE_OFF) {
          mode |= 0x04;
        }

        serial_smart_audio_send_payload(SA_CMD_SET_MODE, &mode, 1);
        return;
      } else {
        vtx_settings.pit_mode = VTX_PIT_MODE_NO_SUPPORT;
      }
    } else {
      pit_mode_tries = 0;
    }
  }
}
#endif
#ifdef ENABLE_TRAMP
extern tramp_settings_t tramp_settings;
uint8_t tramp_detected = 0;

const uint16_t tramp_power_level[4] = {
    25,
    100,
    200,
    400,
};

int8_t tramp_power_level_index(uint16_t power) {
  for (uint8_t level = 0; level < VTX_POWER_LEVEL_MAX; level++) {
    if (power >= tramp_power_level[level] && power <= tramp_power_level[level]) {
      return level;
    }
  }
  return -1;
}

void vtx_tramp_update() {
  vtx_update_result_t result = serial_tramp_update();

  if ((result == VTX_IDLE || result == VTX_ERROR) && tramp_settings.freq_min == 0 && vtx_connect_tries <= VTX_DETECT_TRIES) {
    // no tramp detected, try again
    serial_tramp_send_payload('r', 0);
    vtx_connect_tries++;
    return;
  }

  static vtx_settings_t actual;

  if (result == VTX_SUCCESS) {
    if (tramp_settings.frequency == 0) {
      serial_tramp_send_payload('v', 0);
      return;
    }

    int8_t channel_index = vtx_find_frequency_index(tramp_settings.frequency);
    if (channel_index >= 0) {
      actual.band = channel_index / VTX_CHANNEL_MAX;
      actual.channel = channel_index % VTX_CHANNEL_MAX;
    }

    actual.power_level = tramp_power_level_index(tramp_settings.power);
    actual.pit_mode = tramp_settings.pit_mode;

    if (tramp_settings.temp == 0) {
      serial_tramp_send_payload('s', 0);
      return;
    }

    if (tramp_settings.freq_min != 0 && tramp_settings.frequency != 0 && tramp_detected == 0) {
      tramp_detected = 1;
      vtx_settings = actual;
      vtx_settings.detected = 1;
      vtx_connect_tries = 0;
    }
    return;
  }

  if ((result == VTX_IDLE || result == VTX_ERROR) && tramp_detected) {

    const uint16_t frequency = frequency_table[vtx_settings.band][vtx_settings.channel];
    if (frequency_table[actual.band][actual.channel] != frequency) {
      serial_tramp_send_payload('F', frequency);
      tramp_settings.frequency = 0;
      return;
    }

    if (actual.pit_mode != vtx_settings.pit_mode) {
      serial_tramp_send_payload('I', vtx_settings.pit_mode == VTX_PIT_MODE_ON ? 1 : 0);
      tramp_settings.frequency = 0;
      return;
    }

    if (actual.power_level != vtx_settings.power_level) {
      serial_tramp_send_payload('P', tramp_power_level[vtx_settings.power_level]);
      tramp_settings.frequency = 0;
      return;
    }
  }
}
#endif

void vtx_init() {
  vtx_settings.detected = 0;

#ifdef ENABLE_SMART_AUDIO
  if (profile.serial.smart_audio != USART_PORT_INVALID) {
    serial_smart_audio_init();
  }
#endif
#ifdef ENABLE_TRAMP
  if (profile.serial.smart_audio != USART_PORT_INVALID) {
    serial_tramp_init();
  }
#endif
}

void vtx_update() {
  static volatile uint32_t delay_loops = 5000;
#if defined(FPV_ON) && defined(FPV_PIN)
  if (rx_aux_on(AUX_FPV_ON)) {
    // fpv switch on
    if (!fpv_init && flags.rx_mode == RXMODE_NORMAL && flags.on_ground == 1) {
      fpv_init = gpio_init_fpv(flags.rx_mode);
      delay_loops = 1000;
      vtx_connect_tries = 0;
    }
    if (fpv_init) {
      gpio_pin_set(FPV_PIN);
    }
  } else {
    // fpv switch off
    if (fpv_init && flags.on_ground == 1) {
      if (flags.failsafe) {
        //do nothing = hold last state
      } else {
        gpio_pin_reset(FPV_PIN);
      }
    }
  }
#endif

  if (delay_loops > 0) {
    delay_loops--;
    return;
  }

#ifdef ENABLE_SMART_AUDIO
  if (flags.on_ground && has_vtx_configured()) {
    vtx_smart_audio_update();
  }
#endif

#ifdef ENABLE_TRAMP
  if (flags.on_ground && has_vtx_configured()) {
    vtx_tramp_update();
  }
#endif
}

void vtx_set(vtx_settings_t *vtx) {
  if (vtx_settings.pit_mode != VTX_PIT_MODE_NO_SUPPORT)
    vtx_settings.pit_mode = vtx->pit_mode;

  vtx_settings.power_level = vtx->power_level;

  vtx_settings.band = vtx->band;
  vtx_settings.channel = vtx->channel;
}

cbor_result_t cbor_encode_vtx_settings_t(cbor_value_t *enc, const vtx_settings_t *vtx) {
  cbor_result_t res = CBOR_OK;

  CBOR_CHECK_ERROR(res = cbor_encode_map_indefinite(enc));

  CBOR_CHECK_ERROR(res = cbor_encode_str(enc, "detected"));
  CBOR_CHECK_ERROR(res = cbor_encode_uint8(enc, &vtx->detected));

  CBOR_CHECK_ERROR(res = cbor_encode_str(enc, "band"));
  CBOR_CHECK_ERROR(res = cbor_encode_uint8(enc, &vtx->band));

  CBOR_CHECK_ERROR(res = cbor_encode_str(enc, "channel"));
  CBOR_CHECK_ERROR(res = cbor_encode_uint8(enc, &vtx->channel));

  CBOR_CHECK_ERROR(res = cbor_encode_str(enc, "pit_mode"));
  CBOR_CHECK_ERROR(res = cbor_encode_uint8(enc, &vtx->pit_mode));

  CBOR_CHECK_ERROR(res = cbor_encode_str(enc, "power_level"));
  CBOR_CHECK_ERROR(res = cbor_encode_uint8(enc, &vtx->power_level));

  CBOR_CHECK_ERROR(res = cbor_encode_end_indefinite(enc));
  return res;
}

cbor_result_t cbor_decode_vtx_settings_t(cbor_value_t *dec, vtx_settings_t *vtx) {
  cbor_result_t res = CBOR_OK;

  cbor_container_t map;
  CBOR_CHECK_ERROR(res = cbor_decode_map(dec, &map));

  const uint8_t *name;
  uint32_t name_len;
  for (uint32_t i = 0; i < cbor_decode_map_size(dec, &map); i++) {
    CBOR_CHECK_ERROR(res = cbor_decode_tstr(dec, &name, &name_len));

    if (buf_equal_string(name, name_len, "band")) {
      CBOR_CHECK_ERROR(res = cbor_decode_uint8(dec, &vtx->band));
      continue;
    }

    if (buf_equal_string(name, name_len, "channel")) {
      CBOR_CHECK_ERROR(res = cbor_decode_uint8(dec, &vtx->channel));
      continue;
    }

    if (buf_equal_string(name, name_len, "pit_mode")) {
      CBOR_CHECK_ERROR(res = cbor_decode_uint8(dec, &vtx->pit_mode));
      continue;
    }

    if (buf_equal_string(name, name_len, "power_level")) {
      CBOR_CHECK_ERROR(res = cbor_decode_uint8(dec, &vtx->power_level));
      continue;
    }

    CBOR_CHECK_ERROR(res = cbor_decode_skip(dec));
  }
  return res;
}
