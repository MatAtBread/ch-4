#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the fallback configuration web server
void web_config_start(void);

// Start the captive portal AP and configuration web server
void captive_portal_start(void);

#ifdef __cplusplus
}
#endif
