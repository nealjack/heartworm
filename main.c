#include <stdbool.h>
#include <stdint.h>
#include "led.h"
#include "nordic_common.h"
#include "softdevice_handler.h"
#include "app_gpiote.h"
#include "app_button.h"
#include "led_softblink.h"
#include "nrf_delay.h"

// nrf5x-base libraries
#include "simple_ble.h"
#include "eddystone.h"
#include "simple_adv.h"
#include "multi_adv.h"

// Need pin number for LED
#define LED 24
#define USE_LED 1

// Interrupt pin number
#define BUTTON_PIN 25

#define DEVICE_NAME "squall+heartworm"

// Need this for 15 day timer
//#define ONE_MINUTE APP_TIMER_TICKS(10368000, APP_TIMER_PRESCALER)
//#define ONE_SECOND APP_TIMER_TICKS(8, 4095)

// Define our one-minute timeout timer
APP_TIMER_DEF(timer0);
APP_TIMER_DEF(timer1);

typedef enum state {
    IDLE,
    WAIT,
    MONTH,
    FLASH,
    RESET_PRESS,
    RESET
} State;

typedef struct time {
    uint8_t mins; // current count of days
    uint8_t hours;
    uint8_t days; // current count of days
} Time;

State state = RESET;
Time time = {.mins=0, .hours=0, .days=0};

void ble_error(uint32_t error_code) {
    while(1) {
      led_toggle(LED);
      for(volatile int i = 0; i < 10000; i++) {};
    }
}
void button_handler(uint8_t pin, uint8_t button_action) {
    if (pin != BUTTON_PIN) return;
    if (button_action == APP_BUTTON_PUSH) {
      if (state == FLASH) {
        state = IDLE;
        return;
      }
      else if (state == IDLE || state == WAIT) {
        state = RESET_PRESS;
      }
    }
}

void timer0_timeout_handler(void* p_context) {
    if (state == WAIT)  {
      if (time.mins < 60) {
          time.mins += 1;
          return;
      }
      time.mins = 0;
      if (time.hours < 24) {
          time.hours += 1;
          return;
      }
      time.hours = 0;
      if (time.days < 30) {
          time.days += 1;
          return;
      }
      state = MONTH;
    }
}

void timer1_timeout_handler(void* p_context) {
    if (state == RESET_PRESS) {
        bool pushed = false;
        app_button_is_pushed(0, &pushed);
        if (pushed) state = RESET;
        else state = IDLE;
    }
}

int main(void) {
    // Initialize.

    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_8000MS_CALIBRATION,
            false);

    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);

    app_timer_create(&timer0, APP_TIMER_MODE_REPEATED, timer0_timeout_handler);
    app_timer_create(&timer1, APP_TIMER_MODE_SINGLE_SHOT, timer1_timeout_handler);

    // For 3 users of GPIOTE
    APP_GPIOTE_INIT(3);

    // Button config
    app_button_cfg_t button_cfg = {
        .pin_no = BUTTON_PIN,
        .active_state = APP_BUTTON_ACTIVE_LOW,
        .pull_cfg = NRF_GPIO_PIN_PULLUP,
        .button_handler = button_handler
    };

    app_button_init(&button_cfg, 1, 50);
    app_button_enable();

    // LED config
    led_sb_init_params_t sb_config = LED_SB_INIT_DEFAULT_PARAMS(1 << LED);
    sb_config.on_time_ticks = 1000;

    led_softblink_init(&sb_config);

    // Enable internal DC-DC converter to save power
    // sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);

    // Enter main loop.
    while (1) {
        switch (state) {
          case RESET:
            for(int i = 0; i < 4; i++) {
              led_toggle(LED);
              nrf_delay_ms(100);
            }
            time.mins = 0;
            time.hours = 0;
            time.days = 0;
            app_timer_stop(timer1);
            app_timer_start(timer0, APP_TIMER_TICKS(60000, APP_TIMER_PRESCALER), NULL);
            state = WAIT;
            break;
          case MONTH:
            led_softblink_start(1 << LED);
            state = FLASH;
            break;
          case IDLE:
            led_softblink_stop();
            app_timer_stop(timer0);
            sd_app_evt_wait();
            break;
          case RESET_PRESS:
            app_timer_start(timer1, APP_TIMER_TICKS(1000, APP_TIMER_PRESCALER), NULL);
            break;
          default:
            break;
        }
        sd_app_evt_wait();
    }
}
