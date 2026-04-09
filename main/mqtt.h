#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialise the MQTT client
void mqtt_start(const char *url, const char *device_name);

// Handle JSON payload containing state variables 
// Used by both MQTT receiver and HTTP Web config processor
void process_state_json(const char *json_payload);

#ifdef __cplusplus
}
#endif
