#ifndef GAUGEBOY_CONFIG_H
#define GAUGEBOY_CONFIG_H

/*
 * Settings from sdmc:/3ds/gaugeboy/config.ini. Platform-free (stdio only).
 *
 * Format: "key = value" lines, optional [section] headers, comments start
 * with ';' or '#'. Unknown sections and keys are ignored; bad values keep
 * the default and are counted as warnings.
 *
 *   [adapter]
 *   host = 192.168.0.10
 *   port = 35000
 *   wifi_slot = any        ; or 1, 2, 3: the System Settings connection slot
 */

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_DIR  "sdmc:/3ds/gaugeboy"
#define CONFIG_PATH CONFIG_DIR "/config.ini"

#define CONFIG_DEFAULT_HOST "192.168.0.10"
#define CONFIG_DEFAULT_PORT 35000

typedef struct {
    char adapter_host[16];   /* dotted IPv4 */
    uint16_t adapter_port;
    uint8_t wifi_slot;       /* 0 = any saved connection, 1-3 = that slot only */
} gb_config_t;

#define CONFIG_WIFI_SLOTS 3

void config_defaults(gb_config_t *cfg);

/* Parse ini text over the current values. Returns the number of warnings
 * (bad values or malformed lines). */
int config_parse(gb_config_t *cfg, const char *text);

/* Defaults, then the file at path if it exists. Returns false if the file
 * could not be opened (cfg then holds the defaults). *warnings may be NULL. */
bool config_load(gb_config_t *cfg, const char *path, int *warnings);

/* Write cfg as a commented config file. Returns false on error. */
bool config_save(const gb_config_t *cfg, const char *path);

/* Write a commented default config file. Returns false on error. */
bool config_write_default(const char *path);

/* True for a dotted-quad IPv4 address such as "192.168.0.10". */
bool config_valid_ipv4(const char *s);

#endif
