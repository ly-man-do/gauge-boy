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
    cfg->car_make[0] = '\0';
    cfg->car_model[0] = '\0';
    cfg->car_year = 0;
    cfg->metric = false;
    static const uint8_t live[] = CONFIG_LIVE_DEFAULT_PIDS;
    memcpy(cfg->live_pids, live, sizeof live);
    cfg->live_pid_count = (uint8_t)sizeof live;
    strcpy(cfg->live_view, "engine");
    cfg->shift_rpm = CONFIG_SHIFT_DEFAULT;
}

/* "0C 0D 05" or "0c,0d,5": hex PIDs 01-FF, repeats dropped. False (and
 * cfg unchanged) on a bad token. */
static bool parse_pids(gb_config_t *cfg, const char *value)
{
    uint8_t pids[CONFIG_LIVE_PIDS_MAX];
    int count = 0;
    const char *p = value;
    while (*p != '\0') {
        while (*p == ' ' || *p == ',' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        int v = 0, digits = 0;
        while (isxdigit((unsigned char)*p)) {
            int c = tolower((unsigned char)*p);
            v = v * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
            digits++;
            p++;
        }
        if (digits == 0 || digits > 2 || v == 0 ||
            (*p != '\0' && *p != ' ' && *p != ',' && *p != '\t')) {
            return false;
        }
        bool seen = false;
        for (int i = 0; i < count; i++) {
            seen = seen || pids[i] == v;
        }
        if (!seen) {
            if (count == CONFIG_LIVE_PIDS_MAX) {
                return false;
            }
            pids[count++] = (uint8_t)v;
        }
    }
    memcpy(cfg->live_pids, pids, (size_t)count);
    cfg->live_pid_count = (uint8_t)count;
    return true;
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

        if (strcmp(section, "live") == 0) {
            if (strcmp(key, "pids") == 0 && !parse_pids(cfg, value)) {
                warnings++;
            } else if (strcmp(key, "view") == 0) {
                lower(value);
                size_t n = strlen(value);
                if (n == 0 || n >= sizeof cfg->live_view) {
                    warnings++;
                } else {
                    memcpy(cfg->live_view, value, n + 1);
                }
            }
            continue;
        }
        if (strcmp(section, "track") == 0) {
            if (strcmp(key, "shift_rpm") == 0) {
                char *end = NULL;
                long r = strtol(value, &end, 10);
                if (value[0] != '\0' && end != NULL && *end == '\0' && r >= CONFIG_SHIFT_MIN &&
                    r <= CONFIG_SHIFT_MAX) {
                    cfg->shift_rpm = (uint16_t)r;
                } else {
                    warnings++;
                }
            }
            continue;
        }
        if (strcmp(section, "display") == 0) {
            if (strcmp(key, "units") == 0) {
                if (strcmp(value, "us") == 0 || strcmp(value, "US") == 0) {
                    cfg->metric = false;
                } else if (strcmp(value, "metric") == 0) {
                    cfg->metric = true;
                } else {
                    warnings++;
                }
            }
            continue;
        }
        if (strcmp(section, "vehicle") == 0) {
            bool is_auto = strlen(value) == 4 && tolower((unsigned char)value[0]) == 'a' &&
                           tolower((unsigned char)value[1]) == 'u' &&
                           tolower((unsigned char)value[2]) == 't' &&
                           tolower((unsigned char)value[3]) == 'o';
            if (strcmp(key, "year") == 0) {
                char *end = NULL;
                long y = strtol(value, &end, 10);
                if (value[0] == '\0' || is_auto) {
                    cfg->car_year = 0;
                } else if (end != NULL && *end == '\0' && y >= CONFIG_YEAR_MIN &&
                           y <= CONFIG_YEAR_MAX) {
                    cfg->car_year = (uint16_t)y;
                } else {
                    warnings++;
                }
                continue;
            }
            if (strcmp(key, "model") == 0) {
                size_t n = strlen(value);
                bool ok = n < sizeof cfg->car_model;
                for (size_t i = 0; ok && i < n; i++) {
                    ok = isprint((unsigned char)value[i]) != 0 && value[i] != ';' &&
                         value[i] != '#';
                }
                if (!ok) {
                    warnings++;
                } else if (n == 0 || is_auto) {
                    cfg->car_model[0] = '\0';
                } else {
                    memcpy(cfg->car_model, value, n + 1);
                }
                continue;
            }
            if (strcmp(key, "make") == 0) {
                size_t n = strlen(value);
                bool ok = n < sizeof cfg->car_make;
                for (size_t i = 0; ok && i < n; i++) {
                    ok = isprint((unsigned char)value[i]) != 0;
                }
                if (!ok) {
                    warnings++;
                } else if (n == 0 || (n == 4 && tolower((unsigned char)value[0]) == 'a'
                                      && tolower((unsigned char)value[1]) == 'u'
                                      && tolower((unsigned char)value[2]) == 't'
                                      && tolower((unsigned char)value[3]) == 'o')) {
                    cfg->car_make[0] = '\0';
                } else {
                    memcpy(cfg->car_make, value, n + 1);
                }
            }
            continue;
        }
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
    char pids[3 * CONFIG_LIVE_PIDS_MAX + 1] = "";
    for (int i = 0; i < cfg->live_pid_count && i < CONFIG_LIVE_PIDS_MAX; i++) {
        snprintf(pids + strlen(pids), sizeof pids - strlen(pids), "%s%02X", i ? " " : "",
                 (unsigned)cfg->live_pids[i]);
    }
    char year[8] = "auto";
    if (cfg->car_year != 0) {
        snprintf(year, sizeof year, "%u", (unsigned)cfg->car_year);
    }
    char slot[8];
    if (cfg->wifi_slot >= 1 && cfg->wifi_slot <= CONFIG_WIFI_SLOTS) {
        snprintf(slot, sizeof slot, "%u", (unsigned)cfg->wifi_slot);
    } else {
        strcpy(slot, "any");
    }
    int rc = fprintf(f,
        "; Gauge Boy settings\n"
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
        "wifi_slot = %s\n"
        "\n"
        "; make: \"auto\" reads it from the VIN; set a make (e.g. Geo) for cars\n"
        "; that don't report a VIN, so DIAG shows that make's own code meanings.\n"
        "; model and year: \"auto\" reads them from the VIN; set them (SETUP >\n"
        "; VEHICLE) for cars that don't report a VIN.\n"
        "\n"
        "[vehicle]\n"
        "make = %s\n"
        "model = %s\n"
        "year = %s\n"
        "\n"
        "; units: us (mph, F, psi, inHg) or metric (km/h, C, kPa).\n"
        "\n"
        "[display]\n"
        "units = %s\n"
        "\n"
        "; shift_rpm: the shift light's shift point (TRACK > SHIFT LIGHT),\n"
        "; 2000 to 9900 rpm.\n"
        "\n"
        "[track]\n"
        "shift_rpm = %u\n"
        "\n"
        "; pids: the LIVE Custom view's Mode 01 PIDs, in hex (chosen with EDIT).\n"
        "; Fewer PIDs refresh faster. view: the LIVE view opened last.\n"
        "\n"
        "[live]\n"
        "pids = %s\n"
        "view = %s\n",
        cfg->adapter_host, (unsigned)cfg->adapter_port, slot,
        cfg->car_make[0] != '\0' ? cfg->car_make : "auto",
        cfg->car_model[0] != '\0' ? cfg->car_model : "auto", year,
        cfg->metric ? "metric" : "us", (unsigned)cfg->shift_rpm, pids, cfg->live_view);
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

void config_clean_text(char *s)
{
    size_t n = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isprint(c) && c != ';' && c != '#') {
            s[n++] = (char)c;
        }
    }
    while (n > 0 && s[n - 1] == ' ') {
        n--;
    }
    s[n] = '\0';
    size_t lead = 0;
    while (s[lead] == ' ') {
        lead++;
    }
    memmove(s, s + lead, n - lead + 1);
    if (strlen(s) == 4 && tolower((unsigned char)s[0]) == 'a' &&
        tolower((unsigned char)s[1]) == 'u' && tolower((unsigned char)s[2]) == 't' &&
        tolower((unsigned char)s[3]) == 'o') {
        s[0] = '\0';
    }
}

int config_year_step(int year, int dir, int last)
{
    if (year == 0) {
        return dir > 0 ? CONFIG_YEAR_STEP_FIRST : last;
    }
    int y = year + (dir > 0 ? 1 : -1);
    if (y > last || y < CONFIG_YEAR_MIN || (dir < 0 && year == CONFIG_YEAR_STEP_FIRST)) {
        return 0;
    }
    return y;
}
