#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FILE_MAX 4096
#define LINE_MAX_LEN    256

void config_defaults(gb_config_t *cfg)
{
    strcpy(cfg->adapter_host, CONFIG_DEFAULT_HOST);
    cfg->adapter_port = CONFIG_DEFAULT_PORT;
    cfg->wifi_slot = 0;
}

bool config_valid_ipv4(const char *s)
{
    for (int part = 0; part < 4; part++) {
        int value = 0;
        int digits = 0;
        while (isdigit((unsigned char)*s)) {
            value = value * 10 + (*s - '0');
            digits++;
            s++;
            if (digits > 3 || value > 255) {
                return false;
            }
        }
        if (digits == 0) {
            return false;
        }
        if (part < 3) {
            if (*s != '.') {
                return false;
            }
            s++;
        }
    }
    return *s == '\0';
}

static bool parse_port(const char *s, uint16_t *out)
{
    long value = 0;
    if (*s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s)) {
            return false;
        }
        value = value * 10 + (*s - '0');
        if (value > 65535) {
            return false;
        }
    }
    if (value == 0) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
    return s;
}

static void lower(char *s)
{
    for (; *s != '\0'; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

int config_parse(gb_config_t *cfg, const char *text)
{
    char section[32] = "";
    int warnings = 0;

    while (*text != '\0') {
        size_t len = strcspn(text, "\r\n");
        char buf[LINE_MAX_LEN];
        size_t copy = len < sizeof buf - 1 ? len : sizeof buf - 1;
        memcpy(buf, text, copy);
        buf[copy] = '\0';
        text += len;
        while (*text == '\r' || *text == '\n') {
            text++;
        }

        char *line = trim(buf);
        if (*line == '\0' || *line == ';' || *line == '#') {
            continue;
        }
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (end == NULL) {
                warnings++;
                section[0] = '\0';
                continue;
            }
            *end = '\0';
            char *name = trim(line + 1);
            lower(name);
            size_t name_len = strlen(name);
            if (name_len >= sizeof section) {
                warnings++;
                name_len = 0;
            }
            memcpy(section, name, name_len);
            section[name_len] = '\0';
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == NULL) {
            warnings++;
            continue;
        }
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);
        lower(key);

        if (strcmp(section, "adapter") != 0) {
            continue;
        }
        if (strcmp(key, "host") == 0) {
            if (config_valid_ipv4(value)) {
                strcpy(cfg->adapter_host, value);
            } else {
                warnings++;
            }
        } else if (strcmp(key, "port") == 0) {
            if (!parse_port(value, &cfg->adapter_port)) {
                warnings++;
            }
        } else if (strcmp(key, "wifi_slot") == 0) {
            lower(value);
            if (strcmp(value, "any") == 0 || strcmp(value, "0") == 0) {
                cfg->wifi_slot = 0;
            } else if (value[0] >= '1' && value[0] <= '0' + CONFIG_WIFI_SLOTS && value[1] == '\0') {
                cfg->wifi_slot = (uint8_t)(value[0] - '0');
            } else {
                warnings++;
            }
        }
    }
    return warnings;
}

bool config_load(gb_config_t *cfg, const char *path, int *warnings)
{
    config_defaults(cfg);
    if (warnings != NULL) {
        *warnings = 0;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    char *text = malloc(CONFIG_FILE_MAX + 1);
    if (text == NULL) {
        fclose(f);
        return false;
    }
    size_t n = fread(text, 1, CONFIG_FILE_MAX, f);
    fclose(f);
    text[n] = '\0';

    int w = config_parse(cfg, text);
    free(text);
    if (warnings != NULL) {
        *warnings = w;
    }
    return true;
}

bool config_save(const gb_config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    char slot[8];
    if (cfg->wifi_slot >= 1 && cfg->wifi_slot <= CONFIG_WIFI_SLOTS) {
        snprintf(slot, sizeof slot, "%u", (unsigned)cfg->wifi_slot);
    } else {
        strcpy(slot, "any");
    }
    int rc = fprintf(f,
        "; GaugeBoy settings\n"
        ";\n"
        "; Wi-Fi ELM327 adapter address. Most adapters use 192.168.0.10\n"
        "; port 35000; check your adapter's manual if yours differs.\n"
        ";\n"
        "; wifi_slot: the System Settings connection slot (1, 2 or 3) that holds\n"
        "; the adapter's network, or \"any\" to use whichever saved one is in range.\n"
        "\n"
        "[adapter]\n"
        "host = %s\n"
        "port = %u\n"
        "wifi_slot = %s\n",
        cfg->adapter_host, (unsigned)cfg->adapter_port, slot);
    bool ok = rc > 0;
    if (fclose(f) != 0) {
        ok = false;
    }
    return ok;
}

bool config_write_default(const char *path)
{
    gb_config_t cfg;
    config_defaults(&cfg);
    return config_save(&cfg, path);
}
