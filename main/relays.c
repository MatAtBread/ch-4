#include "relays.h"
#include "driver/gpio.h"
#include <string.h>

// NOTE: Relay GPIO is active low (0 = on, 1 = off)
#define RELAY_1_GPIO 17
#define RELAY_2_GPIO 19
#define RELAY_3_GPIO 20
#define RELAY_4_GPIO 18

void relays_init(void) {
    gpio_reset_pin(RELAY_1_GPIO);
    gpio_set_direction(RELAY_1_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(RELAY_2_GPIO);
    gpio_set_direction(RELAY_2_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(RELAY_3_GPIO);
    gpio_set_direction(RELAY_3_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(RELAY_4_GPIO);
    gpio_set_direction(RELAY_4_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(RELAY_1_GPIO, 1);
    gpio_set_level(RELAY_2_GPIO, 1);
    gpio_set_level(RELAY_3_GPIO, 1);
    gpio_set_level(RELAY_4_GPIO, 1);
}

void relays_apply(const char *mode, bool pause) {
    if (mode) {
        if (strcasecmp(mode, "on") == 0) {
            gpio_set_level(RELAY_1_GPIO, 0);
            gpio_set_level(RELAY_2_GPIO, 0);
        } else if (strcasecmp(mode, "off") == 0) {
            gpio_set_level(RELAY_1_GPIO, 1);
            gpio_set_level(RELAY_2_GPIO, 1);
        } else if (strcasecmp(mode, "clock") == 0) {
            gpio_set_level(RELAY_1_GPIO, 0);
            gpio_set_level(RELAY_2_GPIO, 1);
        }
    }

    gpio_set_level(RELAY_3_GPIO, pause ? 1 : 0);

    // Relay 4 is currently unused
    gpio_set_level(RELAY_4_GPIO, 1);
}
