#include "relays.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

extern const char *TAG;

#define RELAY_ONOFF       20
#define RELAY_CLOCKBYPASS 18
#define RELAY_PAUSE       19
#define RELAY_UNUSED      17

void relays_init(void) {
    for (int i=RELAY_UNUSED; i<=RELAY_ONOFF; i++) {
        gpio_reset_pin(i);
        gpio_set_direction(i, GPIO_MODE_OUTPUT);
        gpio_set_level(i, 1);
    }
}

void relays_apply(const char *mode, bool pause) {
    int bypass = -1;
    int onoff = -1;
    if (mode) {
        if (strcasecmp(mode, "on") == 0) {
            bypass = 1;
            onoff = 1;
        } else if (strcasecmp(mode, "clock") == 0) {
            bypass = 0;
            onoff = 1;
        } else if (strcasecmp(mode, "off") == 0) {
            bypass = 0; // Don't care
            onoff = 0;
        } else {
            ESP_LOGW(TAG, "Unknown mode '%s'", mode);
        }
        gpio_set_level(RELAY_CLOCKBYPASS, bypass); // high == closed
        gpio_set_level(RELAY_ONOFF, !onoff); // low == closed
    }

    gpio_set_level(RELAY_PAUSE, pause); // low == closed (not paused)

    ESP_LOGI(TAG, "Mode %s: bypass[%d]=%d, on/off[%d]=%d, pause[%d]=%d", mode ? mode : "?", RELAY_CLOCKBYPASS, bypass, RELAY_ONOFF, onoff, RELAY_PAUSE, pause);

    // Relay 4 is currently unused
    gpio_set_level(RELAY_UNUSED, !0);
}
