
#include <stdio.h>

#include "acquisition/adc_task.h"
#include "acquisition/analyzer.h"
#include "acquisition/acq_consts.h"
#include "ble/ble_service.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io/io.h"
#include "misc/elapsed.h"
#include "misc/util.h"
#include "settings/controls.h"
#include "settings/nvs_config.h"

static constexpr auto TAG = "main";

static const analyzer::Settings kDefaultSettings = {
    .offset1 = 1800, .offset2 = 1800, .is_reverse_direction = false};

static analyzer::State state;

static analyzer::AdcCaptureBuffer capture_buffer;

// Used to blink N times LED 2.
static Elapsed led2_timer;
// Down counter. If value > 0, then led2 blinks and bit 0 controls
// the led state.
static uint16_t led2_counter;

static Elapsed periodic_timer;

// Used to generate blink to indicates that
// acquisition is working.
static uint32_t analyzer_counter = 0;

static void start_led2_blinks(uint16_t n) {
  led2_timer.reset();
  led2_counter = n * 2;
  io::LED2.write(led2_counter > 0);
}

static void setup() {
  // Set initial LEDs values.
  io::LED1.clear();
  io::LED2.clear();

  // Init config.
  // nvs_config::setup();
  util::nvs_init();

  // Fetch settings.
  analyzer::Settings settings;
  if (!nvs_config::read_acquisition_settings(&settings)) {
    ESP_LOGE(TAG, "Failed to read settings, will use default.");
    settings = kDefaultSettings;
  }
  ESP_LOGI(TAG, "Settings: %d, %d, %d", settings.offset1, settings.offset2,
           settings.is_reverse_direction);

  // Init acquisition.
  analyzer::setup(settings);
  adc_task::setup();

  // Init BLE
  // TODO: Make this configured by board resistors.
  ble_service::setup(0, acq_consts::xCC6920BSO5A_ADC_TICKS_PER_AMP);
}

// static uint32_t loop_counter = 0;

static bool is_connected = false;

static void loop() {
  // Handle button.
  const Button::ButtonEvent button_event = io::BUTTON1.update();
  if (button_event != Button::EVENT_NONE) {
    ESP_LOGI(TAG, "Button event: %d", button_event);

    // Handle single click. Reverse direction.
    if (button_event == Button::EVENT_SHORT_CLICK) {
      bool new_is_reversed_direcction;
      const bool ok = controls::toggle_direction(&new_is_reversed_direcction);
      const uint16_t num_blinks = !ok ? 10 : new_is_reversed_direcction ? 2 : 1;
      start_led2_blinks(num_blinks);
    }

    // Handle long press. Zero calibration.
    else if (button_event == Button::EVENT_LONG_PRESS) {
      // zero_setting = true;
      const bool ok = controls::zero_calibration();
      start_led2_blinks(ok ? 3 : 10);
    }
  }

  // Update is_connected periodically.
  if (periodic_timer.elapsed_millis() >= 500) {
    periodic_timer.reset();
    is_connected = false;  // ble_service::is_connected();
  }

  // Update LED blinks.  Blinking indicates analyzer works
  // and provides states. High speed blink indicates connection
  // status.
  const int blink_shift = is_connected ? 0 : 3;
  const bool blink_state = ((analyzer_counter >> blink_shift) & 0x7) == 0x0;
  // Supress LED1 while blinking LED2. We want to have them appart on the
  // board such that they don't interfere visually.
  io::LED1.write(blink_state && !led2_counter);

  if (led2_counter > 0 && led2_timer.elapsed_millis() >= 500) {
    led2_timer.reset();
    led2_counter--;
    io::LED2.write(led2_counter > 0 && !(led2_counter & 0x1));
  }

  // Blocking. 50Hz.
  analyzer::pop_next_state(&state);

  analyzer_counter++;
  ble_service::notify();

  // Dump ADC state
  // if (analyzer_counter % 100 == 0) {
  //   analyzer::dump_state(state);
  //   adc_task::dump_stats();
  // }

  // Dump capture buffer
  // if (analyzer_counter % 150 == 0) {
  //   analyzer::get_last_capture_snapshot(&capture_buffer);
  //   for (int i = 0; i < capture_buffer.items.size(); i++) {
  //     const analyzer::AdcCaptureItem* item = capture_buffer.items.get(i);
  //     printf("%hd,%hd\n", item->v1, item->v2);
  //   }
  // }
}

// The runtime environment expects a "C" main.
extern "C" void app_main() {
  setup();
  for (;;) {
    loop();
  }
}
