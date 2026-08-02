/*
 * Shared LAN Wi‑Fi OTA helper for User-Hackable ESP-IDF apps.
 *
 * Fetches manifest.json from a base URL, compares version, downloads app.bin
 * with SHA-256 check, writes the inactive OTA slot, and reboots.
 *
 * Requires dual-OTA partition table (see partitions/partitions_two_ota_4mb.csv)
 * and CONFIG_OTA_ALLOW_HTTP for LAN HTTP.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional gate: return false to skip OTA (e.g. lamps on). */
typedef bool (*uh_ota_may_start_fn)(void);

typedef struct {
    /** e.g. "http://192.168.1.202" — no trailing slash */
    const char *base_url;
    /** e.g. "session_timer" → /firmware/session_timer/manifest.json */
    const char *app_name;
    /** If set and returns false, OTA is skipped with ESP_ERR_INVALID_STATE */
    uh_ota_may_start_fn may_start;
    /** Skip update when remote version equals running version (default true). */
    bool skip_if_same_version;
} uh_ota_config_t;

/**
 * Check for a newer image and apply it (reboots on success).
 *
 * @return ESP_OK if already up to date or OTA not needed;
 *         ESP_ERR_NOT_FOUND if no update / 404;
 *         other errors on failure. Does not return on successful OTA (reboots).
 */
esp_err_t uh_ota_check_and_update(const uh_ota_config_t *cfg);

/** Running app version string (from esp_app_desc), never NULL. */
const char *uh_ota_running_version(void);

/**
 * Call once after a healthy boot so bootloader rollback is cancelled.
 * Safe to call every boot.
 */
void uh_ota_mark_valid(void);

#ifdef __cplusplus
}
#endif
