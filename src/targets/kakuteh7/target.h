
#include "config.h"
#include "config_helper.h"

#define KAKUTEH7

// PORTS
#define SPI_PORTS   \
  SPI1_PA5PA6PA7    \
  SPI2_PB13PB14PB15 \
  SPI4_PE2PE5PE6

#define USART_PORTS \
  USART1_PA10PA9    \
  USART2_PD6PD5     \
  USART3_PD9PD8     \
  USART4_PD0PD1     \
  USART6_PC7PC6     \
  USART7_PE7PE8

// LEDS
#define LED_NUMBER 1
#define LED1PIN PIN_C2
#define LED1_INVERT

#define BUZZER_PIN PIN_C13

// GYRO
#define GYRO_SPI_PORT SPI_PORT4
#define GYRO_NSS PIN_E4
#define GYRO_INT PIN_E1
#define GYRO_ORIENTATION GYRO_ROTATE_90_CCW

// RADIO
#ifdef SERIAL_RX
#define RX_USART USART_PORT2
#endif

// OSD
#define ENABLE_OSD
#define MAX7456_SPI_PORT SPI_PORT2
#define MAX7456_NSS PIN_B12

#define USE_SDCARD
#define SDCARD_SPI_PORT SPI_PORT1
#define SDCARD_NSS_PIN PIN_A4
#define SDCARD_DETECT_PIN PIN_A3
#define SDCARD_DETECT_INVERT

// VOLTAGE DIVIDER
#define VBAT_PIN PIN_C0
#define VBAT_DIVIDER_R1 10000
#define VBAT_DIVIDER_R2 1000

#define IBAT_PIN PIN_C1
#define IBAT_SCALE 168

// MOTOR PINS
// S3_OUT
#define MOTOR_PIN0 MOTOR_PIN_PB3
// S4_OUT
#define MOTOR_PIN1 MOTOR_PIN_PB10
// S1_OUT
#define MOTOR_PIN2 MOTOR_PIN_PB0
// S2_OUT
#define MOTOR_PIN3 MOTOR_PIN_PB1
