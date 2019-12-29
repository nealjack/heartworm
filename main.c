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

#define PHYSWEB_URL     "j2x.us/heart"

// Need pin number for LED
#define LED 24
#define USE_LED 1

// Interrupt pin number
#define BUTTON_PIN 25

#define DEVICE_NAME "heartworm"
#define PHYSWEB_URL     "j2x.us/heart"
#define ADV_SWITCH_MS 5000

#include "simple_ble.h"

static ble_advdata_manuf_data_t adv_data = {
  .company_identifier = 0x02e0,
  .data = {0},
};

// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
        // c0:98:e5:45:xx:xx
        .platform_id       = 0xff,    // used as 4th octect in device BLE address
        .device_id         = 0x0101,
        .adv_name          = DEVICE_NAME,
        .adv_interval      = MSEC_TO_UNITS(5000, UNIT_0_625_MS),
        .min_conn_interval = MSEC_TO_UNITS(500, UNIT_1_25_MS),
        .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS),
};

/*******************************************************************************
 *   State for this application
 ******************************************************************************/
// Main application state
simple_ble_app_t* simple_ble_app;

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

static void adv_config_eddystone () {
    eddystone_adv(PHYSWEB_URL, NULL);
}

static void adv_config_data () {
    uint8_t data[3] = {29 - time.days, 23 - time.hours, 59 - time.mins};
    adv_data.data.size = sizeof(data);
    adv_data.data.p_data = data;
    simple_adv_manuf_data(&adv_data);
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

    // Setup BLE
    simple_ble_app = simple_ble_init(&ble_config);

    // Need to init multi adv
    multi_adv_init(ADV_SWITCH_MS);

    // Now register our advertisement configure functions
    multi_adv_register_config(adv_config_eddystone);
    multi_adv_register_config(adv_config_data);

    //SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_RC_250_PPM_8000MS_CALIBRATION,
            //false);

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
    sb_config.on_time_ticks   = APP_TIMER_TICKS(100, APP_TIMER_PRESCALER);
    sb_config.off_time_ticks  = APP_TIMER_TICKS(100, APP_TIMER_PRESCALER);
    sb_config.duty_cycle_max  = 255;
    sb_config.duty_cycle_min  = 0;
    sb_config.duty_cycle_step = 255;

    led_softblink_init(&sb_config);

    multi_adv_start();

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
