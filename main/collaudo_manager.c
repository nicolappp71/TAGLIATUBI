#include "collaudo_manager.h"
#include "http_client.h"
#include "mode.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "COLLAUDO_MGR";

#define SERVER_COLLAUDO_URL  SERVER_BASE "/iot/collaudoDataIn.php"

// ── Stato interno ─────────────────────────────────────────────────────────────
static collaudo_state_t     s_state     = COLLAUDO_STATE_CHECKIN;
static collaudo_motore_t    s_motore    = {0};
static collaudo_operatore_t s_operatore = {0};
static SemaphoreHandle_t    s_mutex     = NULL;

// ── Forward: callbacks verso AppBanchetto (implementate in AppBanchetto.cpp) ──
extern void collaudo_app_on_badge_ok(void);
extern void collaudo_app_on_motore_ok(void);
extern void collaudo_app_on_error(const char *msg);

// ═══════════════════════════════════════════════════════════
// INIT
// ═══════════════════════════════════════════════════════════
void collaudo_manager_init(void)
{
    if (s_mutex == NULL)
        s_mutex = xSemaphoreCreateMutex();

    s_state = COLLAUDO_STATE_CHECKIN;
    memset(&s_motore,    0, sizeof(s_motore));
    memset(&s_operatore, 0, sizeof(s_operatore));
    ESP_LOGI(TAG, "Inizializzato. Attendo badge operatore.");
}

// ═══════════════════════════════════════════════════════════
// STATE MACHINE
// ═══════════════════════════════════════════════════════════
collaudo_state_t collaudo_manager_get_state(void)
{
    return s_state;
}

void collaudo_manager_set_state(collaudo_state_t state)
{
    s_state = state;
    ESP_LOGI(TAG, "Stato → %d", state);
}

// ═══════════════════════════════════════════════════════════
// BADGE IN  —  PLACEHOLDER
// Quando l'endpoint sarà disponibile, sostituire con chiamata HTTP
// ═══════════════════════════════════════════════════════════
esp_err_t collaudo_manager_badge_in(const char *badge)
{
    if (!badge || strlen(badge) == 0)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Badge ricevuto: %s (placeholder)", badge);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        snprintf(s_operatore.badge, sizeof(s_operatore.badge), "%s", badge);
        // PLACEHOLDER: nome fittizio finché non c'è l'endpoint
        snprintf(s_operatore.nome, sizeof(s_operatore.nome), "OP: %s", badge);
        xSemaphoreGive(s_mutex);
    }

    collaudo_manager_set_state(COLLAUDO_STATE_SCAN_MOTORE);
    collaudo_app_on_badge_ok();
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════
// TASK HTTP: scarica parametri motore dal server
// ═══════════════════════════════════════════════════════════
static void fetch_motore_task(void *arg)
{
    char *barcode = (char *)arg;

    char codice_tipo[4] = {0};
    char matricola[8]   = {0};

    size_t blen = strlen(barcode);
    if (blen < 10) {
        ESP_LOGE(TAG, "Barcode troppo corto: %s", barcode);
        collaudo_app_on_error("Barcode non valido (minimo 10 caratteri)");
        free(barcode);
        vTaskDelete(NULL);
        return;
    }

    strncpy(codice_tipo, barcode,     3);
    strncpy(matricola,   barcode + 3, 7);

    ESP_LOGI(TAG, "Tipo motore: %s  Matricola: %s", codice_tipo, matricola);

    char url[256];
    snprintf(url, sizeof(url), "%s?barcode=%s", SERVER_COLLAUDO_URL, barcode);

    int   resp_code = 0;
    char *body      = NULL;

    esp_err_t ret = http_get_request(url, &resp_code, &body);
    if (ret != ESP_OK || resp_code != 200 || body == NULL) {
        ESP_LOGE(TAG, "Errore HTTP: ret=%d code=%d", ret, resp_code);
        collaudo_app_on_error("Server non raggiungibile");
        if (body) free(body);
        free(barcode);
        vTaskDelete(NULL);
        return;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);

    if (json == NULL) {
        ESP_LOGE(TAG, "JSON non valido");
        collaudo_app_on_error("Risposta server non valida");
        free(barcode);
        vTaskDelete(NULL);
        return;
    }

    cJSON *err = cJSON_GetObjectItem(json, "errore");
    if (err && cJSON_IsString(err) && err->valuestring[0] != '\0') {
        ESP_LOGW(TAG, "Errore server: %s", err->valuestring);
        collaudo_app_on_error(err->valuestring);
        cJSON_Delete(json);
        free(barcode);
        vTaskDelete(NULL);
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(&s_motore, 0, sizeof(s_motore));

        memcpy(s_motore.codice_tipo, codice_tipo, 3); s_motore.codice_tipo[3] = '\0';
        memcpy(s_motore.matricola,   matricola,   7); s_motore.matricola[7]   = '\0';

        cJSON *desc = cJSON_GetObjectItem(json, "descrizione");
        if (desc && cJSON_IsString(desc))
            strncpy(s_motore.descrizione, desc->valuestring, sizeof(s_motore.descrizione) - 1);

        cJSON *c = cJSON_GetObjectItem(json, "carico");
        if (c) {
            s_motore.carico_consumo_min = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "consumo_min"));
            s_motore.carico_consumo_max = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "consumo_max"));
            s_motore.carico_giri_min    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "giri_min"));
            s_motore.carico_giri_max    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "giri_max"));
        }

        cJSON *m = cJSON_GetObjectItem(json, "minimo");
        if (m) {
            s_motore.minimo_consumo_min = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(m, "consumo_min"));
            s_motore.minimo_consumo_max = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(m, "consumo_max"));
            s_motore.minimo_giri_min    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(m, "giri_min"));
            s_motore.minimo_giri_max    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(m, "giri_max"));
        }

        cJSON *t = cJSON_GetObjectItem(json, "top");
        if (t) {
            s_motore.top_consumo_min = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(t, "consumo_min"));
            s_motore.top_consumo_max = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(t, "consumo_max"));
            s_motore.top_giri_min    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(t, "giri_min"));
            s_motore.top_giri_max    = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(t, "giri_max"));
        }

        xSemaphoreGive(s_mutex);
    }

    cJSON_Delete(json);
    free(barcode);

    ESP_LOGI(TAG, "Motore caricato: [%s] %s  mat=%s",
             s_motore.codice_tipo, s_motore.descrizione, s_motore.matricola);

    collaudo_manager_set_state(COLLAUDO_STATE_IN_CORSO);
    collaudo_app_on_motore_ok();

    vTaskDelete(NULL);
}

// ═══════════════════════════════════════════════════════════
// SCAN BARCODE MOTORE
// ═══════════════════════════════════════════════════════════
esp_err_t collaudo_manager_scan_barcode(const char *barcode)
{
    if (!barcode || strlen(barcode) == 0)
        return ESP_ERR_INVALID_ARG;

    if (s_state != COLLAUDO_STATE_SCAN_MOTORE) {
        ESP_LOGW(TAG, "scan_barcode ignorato: stato=%d", s_state);
        return ESP_FAIL;
    }

    char *bc_copy = strdup(barcode);
    if (!bc_copy)
        return ESP_ERR_NO_MEM;

    xTaskCreate(fetch_motore_task, "collaudo_fetch", 6144, bc_copy, 5, NULL);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════
// ACCESSO DATI
// ═══════════════════════════════════════════════════════════
bool collaudo_manager_get_motore(collaudo_motore_t *out)
{
    if (!out) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out, &s_motore, sizeof(collaudo_motore_t));
        xSemaphoreGive(s_mutex);
        return true;
    }
    return false;
}

bool collaudo_manager_get_operatore(collaudo_operatore_t *out)
{
    if (!out) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out, &s_operatore, sizeof(collaudo_operatore_t));
        xSemaphoreGive(s_mutex);
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════
// RESET
// ═══════════════════════════════════════════════════════════
void collaudo_manager_reset(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memset(&s_motore,    0, sizeof(s_motore));
        memset(&s_operatore, 0, sizeof(s_operatore));
        xSemaphoreGive(s_mutex);
    }
    s_state = COLLAUDO_STATE_CHECKIN;
    ESP_LOGI(TAG, "Reset sessione collaudo.");
}
