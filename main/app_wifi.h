#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include "esp_err.h"

/* Brings up station mode and keeps reconnecting in the background. */
esp_err_t app_wifi_start(void);

/* Blocks until an IP address has been assigned. */
bool app_wifi_wait_connected(int timeout_ms);

#endif /* APP_WIFI_H */
