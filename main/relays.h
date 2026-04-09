#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Set up GPIO pins 17, 18, 19, 20
void relays_init(void);

// Apply a given state to the relays
void relays_apply(const char *mode, bool pause);

#ifdef __cplusplus
}
#endif
