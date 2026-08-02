#include "uh_ota.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

static const char *TAG = "uh_ota";

#define MANIFEST_MAX 2048
#define HTTP_TIMEOUT_MS 30000
#define HTTP_BUF 4096

const char *uh_ota_running_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->version[0]) ? d->version : "unknown";
}

void uh_ota_mark_valid(void)
{
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "marking app valid (cancel rollback)");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
#else
    (void)0;
#endif
}

static esp_err_t http_get_body(const char *url, char *out, size_t out_len, int *status_out)
{
    if (!url || !out || out_len < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status_out) {
        *status_out = status;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "GET %s → HTTP %d", url, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }

    size_t total = 0;
    while (total + 1 < out_len) {
        int n = esp_http_client_read(client, out + total, (int)(out_len - 1 - total));
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    out[total] = '\0';

    if (content_length > 0 && (int)total != content_length) {
        ESP_LOGW(TAG, "body length %u (header %d)", (unsigned)total, content_length);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK || err == ESP_ERR_HTTP_EAGAIN ? ESP_OK : err;
}

static void bytes_to_hex(const unsigned char *in, size_t n, char *out, size_t out_len)
{
    static const char *hex = "0123456789abcdef";
    if (out_len < n * 2 + 1) {
        if (out_len) {
            out[0] = '\0';
        }
        return;
    }
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[n * 2] = '\0';
}

static esp_err_t download_and_apply(const char *bin_url, const char *expect_sha256_hex)
{
    esp_http_client_config_t cfg = {
        .url = bin_url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bin open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "bin HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        ESP_LOGE(TAG, "no OTA update partition");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "writing partition %s @ 0x%x size 0x%x",
             update->label, (unsigned)update->address, (unsigned)update->size);

    esp_ota_handle_t ota = 0;
    err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    char *buf = malloc(HTTP_BUF);
    if (!buf) {
        esp_ota_abort(ota);
        mbedtls_sha256_free(&sha);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (1) {
        int n = esp_http_client_read(client, buf, HTTP_BUF);
        if (n < 0) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "read fail");
            break;
        }
        if (n == 0) {
            err = ESP_OK;
            break;
        }
        mbedtls_sha256_update(&sha, (const unsigned char *)buf, (size_t)n);
        err = esp_ota_write(ota, buf, (size_t)n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
            break;
        }
        total += (size_t)n;
        if ((total & 0xFFFF) == 0) {
            ESP_LOGI(TAG, "OTA wrote %u bytes…", (unsigned)total);
        }
    }
    free(buf);

    unsigned char digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        esp_ota_abort(ota);
        return err;
    }

    char got_hex[65];
    bytes_to_hex(digest, 32, got_hex, sizeof(got_hex));
    ESP_LOGI(TAG, "downloaded %u bytes sha256=%s", (unsigned)total, got_hex);

    if (expect_sha256_hex && expect_sha256_hex[0]) {
        if (strcasecmp(got_hex, expect_sha256_hex) != 0) {
            ESP_LOGE(TAG, "SHA-256 mismatch expect=%s", expect_sha256_hex);
            esp_ota_abort(ota);
            return ESP_ERR_INVALID_CRC;
        }
        ESP_LOGI(TAG, "SHA-256 OK");
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA complete — rebooting into %s", update->label);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; /* not reached */
}

esp_err_t uh_ota_check_and_update(const uh_ota_config_t *cfg)
{
    if (!cfg || !cfg->base_url || !cfg->base_url[0] || !cfg->app_name || !cfg->app_name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->may_start && !cfg->may_start()) {
        ESP_LOGW(TAG, "OTA deferred (may_start=false)");
        return ESP_ERR_INVALID_STATE;
    }

    uh_ota_mark_valid();

    char manifest_url[256];
    int n = snprintf(manifest_url, sizeof(manifest_url),
                     "%s/firmware/%s/manifest.json", cfg->base_url, cfg->app_name);
    if (n <= 0 || n >= (int)sizeof(manifest_url)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "running version=%s", uh_ota_running_version());
    ESP_LOGI(TAG, "manifest %s", manifest_url);

    char body[MANIFEST_MAX];
    int status = 0;
    esp_err_t err = http_get_body(manifest_url, body, sizeof(body), &status);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        ESP_LOGE(TAG, "manifest JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *jver = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *jsha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *jurl = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *jforce = cJSON_GetObjectItemCaseSensitive(root, "force");

    const char *remote_ver =
        (cJSON_IsString(jver) && jver->valuestring) ? jver->valuestring : "";
    const char *sha =
        (cJSON_IsString(jsha) && jsha->valuestring) ? jsha->valuestring : "";
    const char *rel_url =
        (cJSON_IsString(jurl) && jurl->valuestring) ? jurl->valuestring : "";
    bool force = cJSON_IsTrue(jforce);

    bool skip_same = cfg->skip_if_same_version;
    if (remote_ver[0] && skip_same && !force &&
        strcmp(remote_ver, uh_ota_running_version()) == 0) {
        ESP_LOGI(TAG, "already at version %s", remote_ver);
        cJSON_Delete(root);
        return ESP_OK;
    }

    char bin_url[320];
    if (rel_url[0] == 'h') {
        /* absolute http(s)://… */
        snprintf(bin_url, sizeof(bin_url), "%s", rel_url);
    } else if (rel_url[0] == '/') {
        snprintf(bin_url, sizeof(bin_url), "%s%s", cfg->base_url, rel_url);
    } else if (rel_url[0]) {
        snprintf(bin_url, sizeof(bin_url), "%s/%s", cfg->base_url, rel_url);
    } else {
        snprintf(bin_url, sizeof(bin_url), "%s/firmware/%s/app.bin",
                 cfg->base_url, cfg->app_name);
    }

    ESP_LOGI(TAG, "update available remote=%s → %s", remote_ver, bin_url);
    cJSON_Delete(root);

    return download_and_apply(bin_url, sha);
}
