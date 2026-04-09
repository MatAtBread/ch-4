#include "relays.h"
#include "driver/gpio.h"
#include <string.h>

#define RELAY_1_GPIO 17
#define RELAY_2_GPIO 18
#define RELAY_3_GPIO 19
#define RELAY_4_GPIO 20

void relays_init(void) {
    gpio_reset_pin(RELAY_1_GPIO);
    gpio_set_direction(RELAY_1_GPIO, GPIO_MODE_OUTPUT);
    
    gpio_reset_pin(RELAY_2_GPIO);
    gpio_set_direction(RELAY_2_GPIO, GPIO_MODE_OUTPUT);
    
    gpio_reset_pin(RELAY_3_GPIO);
    gpio_set_direction(RELAY_3_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(RELAY_4_GPIO);
    gpio_set_direction(RELAY_4_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(RELAY_1_GPIO, 0);
    gpio_set_level(RELAY_2_GPIO, 0);
    gpio_set_level(RELAY_3_GPIO, 0);
    gpio_set_level(RELAY_4_GPIO, 0);
}

void relays_apply(const char *mode, bool pause) {
    if (mode && strcmp(mode, "on") == 0) {
        gpio_set_level(RELAY_1_GPIO, 1);
        gpio_set_level(RELAY_2_GPIO, 1);
    } else { // "off", "clock", or invalid
        gpio_set_level(RELAY_1_GPIO, 0);
        gpio_set_level(RELAY_2_GPIO, 0);
    }

    gpio_set_level(RELAY_3_GPIO, pause ? 1 : 0);
    
    // Relay 4 is currently unused
    gpio_set_level(RELAY_4_GPIO, 0);
}
