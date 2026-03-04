#include "mqtt_payload.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lightning/as3935.h"

static const char *TAG = "MQTT_PAYLOAD";

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */

static void fmt_iso8601(int64_t timestamp_us, char *out, size_t len)
{
    /* Best-effort ISO 8601 UTC timestamp.
     * Falls back to "unknown" if wall-clock is not yet synced (year < 2020). */
    time_t    t   = (time_t)(timestamp_us / 1000000LL);
    struct tm tm_info;
    gmtime_r(&t, &tm_info);
    if (tm_info.tm_year < (2020 - 1900)) {
        snprintf(out, len, "unknown");
    } else {
        strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    }
}

static int serialise(cJSON *root, char *buf, size_t buf_len)
{
    if (!root) {
        ESP_LOGE(TAG, "cJSON root is NULL");
        return -1;
    }
    bool ok = cJSON_PrintPreallocated(root, buf, (int)buf_len, false);
    cJSON_Delete(root);
    if (!ok) {
        ESP_LOGE(TAG, "cJSON_PrintPreallocated failed (buf too small?)");
        return -1;
    }
    /* Determine length */
    size_t n = 0;
    while (n < buf_len && buf[n] != '\0') n++;
    return (int)n;
}

/* Build the common fields shared by all sensor events. */
static cJSON *build_event_base(const char *event_name,
                                const as3935_data_t *data,
                                const char *device_id, int rssi)
{
    char iso_buf[32];
    fmt_iso8601(data->timestamp_us, iso_buf, sizeof(iso_buf));

    uint64_t uptime_s = (uint64_t)(esp_timer_get_time() / 1000000LL);

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "event", event_name);
    cJSON_AddStringToObject(root, "timestamp_iso", iso_buf);
    /* timestamp_us as string to avoid JSON integer precision loss */
    char ts_str[24];
    snprintf(ts_str, sizeof(ts_str), "%" PRId64, data->timestamp_us);
    cJSON_AddStringToObject(root, "timestamp_us", ts_str);
    cJSON_AddStringToObject(root, "device_id", device_id);
    cJSON_AddNumberToObject(root, "rssi", rssi);
    cJSON_AddNumberToObject(root, "uptime_s", (double)uptime_s);

    return root;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

int mqtt_payload_build_lightning(const as3935_data_t *data,
                                  const char          *device_id,
                                  int rssi, char *buf, size_t buf_len)
{
    cJSON *root = build_event_base("lightning", data, device_id, rssi);
    if (!root) return -1;

    cJSON_AddNumberToObject(root, "distance_km", data->distance_km);
    cJSON_AddNumberToObject(root, "energy", (double)data->energy);

    return serialise(root, buf, buf_len);
}

int mqtt_payload_build_noise(const as3935_data_t *data,
                              const char *device_id, int rssi,
                              char *buf, size_t buf_len)
{
    cJSON *root = build_event_base("noise", data, device_id, rssi);
    if (!root) return -1;
    return serialise(root, buf, buf_len);
}

int mqtt_payload_build_disturber(const as3935_data_t *data,
                                  const char *device_id, int rssi,
                                  char *buf, size_t buf_len)
{
    cJSON *root = build_event_base("disturber", data, device_id, rssi);
    if (!root) return -1;
    return serialise(root, buf, buf_len);
}

int mqtt_payload_build_status(bool online, char *buf, size_t buf_len)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddStringToObject(root, "state", online ? "online" : "offline");
    return serialise(root, buf, buf_len);
}

int mqtt_payload_build_config(bool indoor_mode, uint8_t noise_floor,
                               uint8_t watchdog, uint8_t spike_rejection,
                               uint8_t min_strikes, bool disturber_mask,
                               char *buf, size_t buf_len)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddBoolToObject(root, "indoor_mode", indoor_mode);
    cJSON_AddNumberToObject(root, "noise_floor", noise_floor);
    cJSON_AddNumberToObject(root, "watchdog", watchdog);
    cJSON_AddNumberToObject(root, "spike_rejection", spike_rejection);
    cJSON_AddNumberToObject(root, "min_strikes", min_strikes);
    cJSON_AddBoolToObject(root, "disturber_mask", disturber_mask);

    return serialise(root, buf, buf_len);
}
