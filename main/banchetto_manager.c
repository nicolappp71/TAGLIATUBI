#include "banchetto_manager.h"
#include "json_parser.h"
#include "lvgl.h"
#include "bsp/display.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"
#include "screens.h"
#include "web_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_client.h"
#include "key_manager.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "audio_player.h"
#include "tastiera.h"
#include "mode.h"

extern void app_banchetto_update_page1(void);
extern void app_banchetto_update_page2(void);
extern void app_assegna_banchetto_close(void);
extern void deep_sleep_reset_timer(void);


static char s_formazione_cod_art[32] = {0}; // codice articolo mancante formazione
static char s_formazione_badge[64] = {0};
static const char *TAG = "BANCHETTO_MGR";

#define SERVER_URL SERVER_BASE "/iot/banchetti_1_9_4.php"
#define SERVER_BADGE_URL SERVER_BASE "/iot/badge.php"

// ═══════════════════════════════════════════════════════════
// STATO INTERNO
// ═══════════════════════════════════════════════════════════

static banchetto_list_t s_list = {0};
static uint8_t s_current_idx = 0;
static SemaphoreHandle_t data_mutex = NULL;
static char device_key[17] = {0};
static banchetto_state_t s_state = BANCHETTO_STATE_CHECKIN;

// ═══════════════════════════════════════════════════════════
// STATE MACHINE
// ═══════════════════════════════════════════════════════════
const char* banchetto_manager_get_banchetto_id(void)
{
    static char id[32] = {0};
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000))) {
        if (s_list.count > 0)
            snprintf(id, sizeof(id), "%s", s_list.items[0].banchetto);
        xSemaphoreGive(data_mutex);
    }
    return id;
}
void banchetto_manager_set_state(banchetto_state_t state)
{
    s_state = state;
    const char *names[] = {"CHECKIN", "CONTEGGIO", "CONTROLLO", "ASSEGNA_BANCHETTO"};
    ESP_LOGI(TAG, "Stato → %s", names[state]);
}

banchetto_state_t banchetto_manager_get_state(void)
{
    return s_state;
}

// ═══════════════════════════════════════════════════════════
// NAVIGAZIONE UI
// ═══════════════════════════════════════════════════════════

uint8_t banchetto_manager_get_current_index(void)
{
    return s_current_idx;
}

void banchetto_manager_set_current_index(uint8_t index)
{
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        if (index < s_list.count)
            s_current_idx = index;
        else
            ESP_LOGW(TAG, "set_current_index: indice %d fuori range (count=%d)", index, s_list.count);
        xSemaphoreGive(data_mutex);
    }
}

uint8_t banchetto_manager_get_count(void)
{
    uint8_t count = 0;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        count = s_list.count;
        xSemaphoreGive(data_mutex);
    }
    return count;
}

// ═══════════════════════════════════════════════════════════
// FEEDBACK VISIVO E SONORO
// ═══════════════════════════════════════════════════════════

void reset_panel_style(lv_timer_t *timer)
{
    lv_obj_t *obj = (lv_obj_t *)timer->user_data;
    if (obj != NULL)
    {
        lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_invalidate(obj);
    }
}

void visual_feedback_ok(void)
{
    lv_obj_t *obj = objects[s_current_idx].obj11;
    if (obj != NULL)
    {
        lv_obj_set_style_outline_color(obj, lv_color_hex(0x2ECC71), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_width(obj, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_timer_t *timer = lv_timer_create(reset_panel_style, 500, obj);
        lv_timer_set_repeat_count(timer, 1);
    }
}

void myBeep(void)
{
    char *path = BSP_SPIFFS_MOUNT_POINT "/music/beep.mp3";
    FILE *f = fopen(path, "rb");
    if (f)
    {
        audio_player_play(f);
        vTaskDelay(pdMS_TO_TICKS(1000));
        audio_player_stop();
    }
    else
    {
        ESP_LOGE("AUDIO", "File non trovato: %s", path);
    }
}

// ═══════════════════════════════════════════════════════════
// INIT
// ═══════════════════════════════════════════════════════════

void banchetto_manager_init(void)
{
    ESP_LOGI(TAG, "Inizializzazione Manager");

    data_mutex = xSemaphoreCreateMutex();
    if (data_mutex == NULL)
    {
        ESP_LOGE(TAG, "Impossibile creare mutex");
        return;
    }

    memset(&s_list, 0, sizeof(banchetto_list_t));
    s_current_idx = 0;
    s_state = BANCHETTO_STATE_CHECKIN;

    esp_err_t ret = key_manager_get_key(device_key, sizeof(device_key));
    if (ret == ESP_OK)
        ESP_LOGI(TAG, "Chiave dispositivo: %s", device_key);
    else
        ESP_LOGE(TAG, "Errore caricamento chiave");

    ESP_LOGI(TAG, "Manager pronto");
}

// ═══════════════════════════════════════════════════════════
// FETCH
// ═══════════════════════════════════════════════════════════

esp_err_t banchetto_manager_fetch_from_server(void)
{
    if (device_key[0] == '\0')
    {
        ESP_LOGE(TAG, "Chiave dispositivo non configurata");
        return ESP_FAIL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=inizializza", SERVER_URL, device_key);
    ESP_LOGI(TAG, "GET: %s", url);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP: %s (code: %d)", esp_err_to_name(ret), response_code);
        if (response_body)
            free(response_body);
        return ESP_FAIL;
    }
    if (!response_body)
    {
        ESP_LOGE(TAG, "Response body NULL");
        return ESP_FAIL;
    }
    static banchetto_list_t temp_list;
    memset(&temp_list, 0, sizeof(banchetto_list_t));
    ret = parse_banchetto_list(response_body, &temp_list);
    free(response_body);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Errore parsing JSON");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        memcpy(&s_list, &temp_list, sizeof(banchetto_list_t));
        if (s_current_idx >= s_list.count)
            s_current_idx = 0;
        xSemaphoreGive(data_mutex);
        ESP_LOGI(TAG, "Fetch OK — %d articoli", s_list.count);
        web_server_broadcast_update();
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Timeout mutex");
    return ESP_FAIL;
}

// ═══════════════════════════════════════════════════════════
// GET DATA
// ═══════════════════════════════════════════════════════════

bool banchetto_manager_get_data(banchetto_data_t *out_data)
{
    if (!out_data)
        return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        if (s_list.count == 0)
        {
            xSemaphoreGive(data_mutex);
            return false;
        }
        memcpy(out_data, &s_list.items[s_current_idx], sizeof(banchetto_data_t));
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

bool banchetto_manager_get_item(uint8_t index, banchetto_data_t *out_data)
{
    if (!out_data)
        return false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        if (index >= s_list.count)
        {
            ESP_LOGW(TAG, "get_item: indice %d fuori range", index);
            xSemaphoreGive(data_mutex);
            return false;
        }
        memcpy(out_data, &s_list.items[index], sizeof(banchetto_data_t));
        xSemaphoreGive(data_mutex);
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════
// VERSA — loop su tutti gli articoli
// ═══════════════════════════════════════════════════════════

// bool banchetto_manager_versa(uint32_t qta)
// {
//     ESP_LOGI(TAG, "Versa: %lu", qta);

//     if (!xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
//         return false;

//     if (!s_list.items[0].sessione_aperta)
//     {
//         ESP_LOGW(TAG, "Sessione NON aperta");
//         xSemaphoreGive(data_mutex);
//         return false;
//     }

//     banchetto_data_t *cur = &s_list.items[s_current_idx];
//     if (cur->matr_scatola_corrente[0] == '\0')
//     {
//         ESP_LOGW(TAG, "Scatola non impostata su articolo %d", s_current_idx);
//         xSemaphoreGive(data_mutex);
//         return false;
//     }
//     xSemaphoreGive(data_mutex);

//     bool all_ok = true;
//     char contenitori_pieni[256] = {0};

//     for (int i = 0; i < s_list.count; i++)
//     {
//         if (!xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
//         {
//             all_ok = false;
//             continue;
//         }

//         uint32_t qta_parziale = s_list.items[i].qta_prod_fase;
//         uint32_t qta_pezzi = s_list.items[i].qta_pezzi;
//         uint32_t ord_prod = s_list.items[i].ord_prod;
//         xSemaphoreGive(data_mutex);

//         uint32_t qta_reale = qta * qta_pezzi;
//         char url[512];
//         snprintf(url, sizeof(url), "%s?key=%s&comando=versa&qta=%lu&qta_parziale=%lu&ord_prod=%lu",
//                  SERVER_URL, device_key, qta_reale, qta_parziale, ord_prod);

//         int response_code = 0;
//         char *response_body = NULL;
//         esp_err_t ret = http_get_request(url, &response_code, &response_body);
//         if (response_body)
//             free(response_body);

//         if (ret != ESP_OK || response_code != 200)
//         {
//             ESP_LOGE(TAG, "[%d] Errore HTTP versa (code: %d)", i, response_code);
//             all_ok = false;
//             continue;
//         }

//         if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
//         {
//             banchetto_data_t *item = &s_list.items[i];
//             item->qta_scatola += qta_reale;
//             item->qta_prod_fase += qta_reale;
//             item->qta_prod_sessione += qta_reale;
//             item->qta_totale_giornaliera += qta_reale;

//             // Controlla se contenitore pieno dopo il versamento
//             if (item->qta_scatola >= item->qta_totale_scatola)
//             {
//                 char tmp[64];
//                 snprintf(tmp, sizeof(tmp), "%s%s",
//                          contenitori_pieni[0] ? "\n" : "",
//                          item->codice_articolo);
//                 strncat(contenitori_pieni, tmp, sizeof(contenitori_pieni) - strlen(contenitori_pieni) - 1);
//             }

//             ESP_LOGI(TAG, "[%d] Versamento OK — scatola:%lu/%lu fase:%lu sess:%lu",
//                      i, item->qta_scatola, item->qta_totale_scatola,
//                      item->qta_prod_fase, item->qta_prod_sessione);
//             xSemaphoreGive(data_mutex);
//         }
//     }

//     if (all_ok)
//     {
//         if (lvgl_port_lock(pdMS_TO_TICKS(100)))
//         {
//             app_banchetto_update_page2();

//             if (contenitori_pieni[0])
//             {
//                 char msg[320];
//                 snprintf(msg, sizeof(msg),
//                          "Il contenitore del codice:\n%s\ne' pieno!\nCambiare il contenitore!",
//                          contenitori_pieni);
//                 popup_avviso_open(LV_SYMBOL_WARNING " Contenitore Pieno", msg);
//             }

//             lvgl_port_unlock();
//         }
//         myBeep();
//         visual_feedback_ok();
//         web_server_broadcast_update();
//     }

//     return all_ok;
// }

bool banchetto_manager_versa(uint32_t qta)
{
    ESP_LOGI(TAG, "Versa: %lu", qta);

    if (!xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
        return false;

    if (!s_list.items[0].sessione_aperta)
    {
        ESP_LOGW(TAG, "Sessione NON aperta");
        xSemaphoreGive(data_mutex);
        return false;
    }

    banchetto_data_t *cur = &s_list.items[s_current_idx];
    if (cur->matr_scatola_corrente[0] == '\0')
    {
        ESP_LOGW(TAG, "Scatola non impostata su articolo %d", s_current_idx);
        xSemaphoreGive(data_mutex);
        return false;
    }
    xSemaphoreGive(data_mutex);

    // Effettua una singola chiamata al server con la quantità versata
    char url[512];
    snprintf(url, sizeof(url), "%s?key=%s&comando=versa&qta=%lu",
             SERVER_URL, device_key, qta);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);
    if (response_body)
        free(response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP versa (code: %d)", response_code);
        return false;
    }

    bool all_ok = true;
    char contenitori_pieni[256] = {0};

    for (int i = 0; i < s_list.count; i++)
    {
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
        {
            banchetto_data_t *item = &s_list.items[i];
            uint32_t qta_reale = qta * item->qta_pezzi;

            item->qta_scatola += qta_reale;
            item->qta_prod_fase += qta_reale;
            item->qta_prod_sessione += qta_reale;
            item->qta_totale_giornaliera += qta_reale;

            // Controlla se contenitore pieno dopo il versamento
            if (item->qta_scatola >= item->qta_totale_scatola)
            {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%s%s",
                         contenitori_pieni[0] ? "\n" : "",
                         item->codice_articolo);
                strncat(contenitori_pieni, tmp, sizeof(contenitori_pieni) - strlen(contenitori_pieni) - 1);
            }

            ESP_LOGI(TAG, "[%d] Versamento OK — scatola:%lu/%lu fase:%lu sess:%lu",
                     i, item->qta_scatola, item->qta_totale_scatola,
                     item->qta_prod_fase, item->qta_prod_sessione);
            xSemaphoreGive(data_mutex);
        }
        else
        {
            all_ok = false;
        }
    }

    if (all_ok)
    {
        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            app_banchetto_update_page2();

            if (contenitori_pieni[0])
            {
                char msg[320];
                snprintf(msg, sizeof(msg),
                         "Il contenitore del codice:\n%s\ne' pieno!\nCambiare il contenitore!",
                         contenitori_pieni);
                popup_avviso_open(LV_SYMBOL_WARNING " Contenitore Pieno", msg);
            }

            lvgl_port_unlock();
        }
        deep_sleep_reset_timer();
        myBeep();
        visual_feedback_ok();
        web_server_broadcast_update();

    }

    return all_ok;
}
// ═══════════════════════════════════════════════════════════
// SCARTO — loop su tutti gli articoli
// ═══════════════════════════════════════════════════════════

bool banchetto_manager_scarto(uint32_t qta_scarti)
{
    ESP_LOGI(TAG, "Scarto [idx=%d]: %lu", s_current_idx, qta_scarti);

    if (qta_scarti == 0)
        return false;

    if (!xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        ESP_LOGE(TAG, "Timeout mutex");
        return false;
    }
    if (!s_list.items[0].sessione_aperta)
    {
        ESP_LOGW(TAG, "Sessione NON aperta");
        xSemaphoreGive(data_mutex);
        return false;
    }
    uint32_t ord_prod = s_list.items[s_current_idx].ord_prod;
    xSemaphoreGive(data_mutex);

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=scarto&qta=%lu&ord_prod=%lu",
             SERVER_URL, device_key, qta_scarti, ord_prod);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        if (response_body)
            free(response_body);
        return false;
    }

    cJSON *json = cJSON_Parse(response_body);
    free(response_body);
    if (!json)
        return false;

    cJSON *ok_item = cJSON_GetObjectItem(json, "OK");
    bool ok = false;
    if (ok_item)
    {
        if (cJSON_IsNumber(ok_item))
            ok = (ok_item->valueint > 0);
        else if (cJSON_IsString(ok_item))
            ok = (atoi(ok_item->valuestring) > 0);
    }
    cJSON_Delete(json);

    if (!ok)
        return false;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        banchetto_data_t *item = &s_list.items[s_current_idx];
        item->qta_prod_fase = (item->qta_prod_fase >= qta_scarti) ? item->qta_prod_fase - qta_scarti : 0;
        item->qta_scatola = (item->qta_scatola >= qta_scarti) ? item->qta_scatola - qta_scarti : 0;
        item->qta_totale_giornaliera = (item->qta_totale_giornaliera >= qta_scarti) ? item->qta_totale_giornaliera - qta_scarti : 0;
        item->qta_prod_sessione = (item->qta_prod_sessione >= qta_scarti) ? item->qta_prod_sessione - qta_scarti : 0;
        xSemaphoreGive(data_mutex);
    }

    if (lvgl_port_lock(pdMS_TO_TICKS(100)))
    {
        app_banchetto_update_page2();
        lvgl_port_unlock();
    }
    web_server_broadcast_update();

    return true;
}
// ═══════════════════════════════════════════════════════════
// SET BARCODE — solo articolo corrente
// ═══════════════════════════════════════════════════════════

// void banchetto_manager_set_barcode(const char *barcode)
// {
//     if (!barcode) return;

//     ESP_LOGI(TAG, "Scatola [idx=%d]: %s", s_current_idx, barcode);

//     char url[256];
//     snprintf(url, sizeof(url), "%s?key=%s&comando=scatola&scatola=%s&ord_prod=%lu",
//              SERVER_URL, device_key, barcode,
//              s_list.items[s_current_idx].ord_prod);

//     int   response_code = 0;
//     char *response_body = NULL;
//     esp_err_t ret = http_get_request(url, &response_code, &response_body);

//     if (ret != ESP_OK || response_code != 200) {
//         if (response_body) free(response_body);
//         return;
//     }

//     cJSON *json = cJSON_Parse(response_body);
//     free(response_body);
//     if (!json) return;

//     cJSON *ok_item = cJSON_GetObjectItem(json, "OK");
//     bool ok = (ok_item && cJSON_IsNumber(ok_item) && ok_item->valueint == 1);
//     cJSON_Delete(json);
//     if (!ok) return;

//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000))) {
//         banchetto_data_t *item = &s_list.items[s_current_idx];

//         if (strncmp(item->matr_scatola_corrente, barcode, 31) != 0)
//             item->qta_scatola = 0;

//         strncpy(item->matr_scatola_corrente, barcode, sizeof(item->matr_scatola_corrente) - 1);
//         item->matr_scatola_corrente[sizeof(item->matr_scatola_corrente) - 1] = '\0';

//         ESP_LOGI(TAG, "[%d] Scatola: %s (%lu/%lu)", s_current_idx,
//                  item->matr_scatola_corrente, item->qta_scatola, item->qta_totale_scatola);

//         if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
//             app_banchetto_update_page2();
//             lvgl_port_unlock();
//         }
//         xSemaphoreGive(data_mutex);
//         web_server_broadcast_update();
//     }
// }

// ═══════════════════════════════════════════════════════════
// LOGIN BADGE
// ═══════════════════════════════════════════════════════════
void banchetto_manager_set_barcode(const char *barcode)
{
    if (!barcode)
        return;

    ESP_LOGI(TAG, "Scatola [idx=%d]: %s", s_current_idx, barcode);

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=scatola&scatola=%s&ord_prod=%lu",
             SERVER_URL, device_key, barcode,
             s_list.items[s_current_idx].ord_prod);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        if (response_body)
            free(response_body);
        return;
    }

    // cJSON *json = cJSON_Parse(response_body);
    // free(response_body);
    // if (!json)
    //     return;

    // cJSON *ok_item = cJSON_GetObjectItem(json, "OK");
    // bool ok = (ok_item && cJSON_IsNumber(ok_item) && ok_item->valueint == 1);
    // cJSON_Delete(json);
    // if (!ok)
    //     return;

    // if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    // {
    //     banchetto_data_t *item = &s_list.items[s_current_idx];

    //     if (strncmp(item->matr_scatola_corrente, barcode, 31) != 0)
    //         item->qta_scatola = 0;

    //     strncpy(item->matr_scatola_corrente, barcode, sizeof(item->matr_scatola_corrente) - 1);
    //     item->matr_scatola_corrente[sizeof(item->matr_scatola_corrente) - 1] = '\0';

    //     ESP_LOGI(TAG, "[%d] Scatola: %s (%lu/%lu)", s_current_idx,
    //              item->matr_scatola_corrente, item->qta_scatola, item->qta_totale_scatola);

    //     xSemaphoreGive(data_mutex);
    // ...

    cJSON *json = cJSON_Parse(response_body);
    free(response_body);
    if (!json)
        return;

    cJSON *ok_item = cJSON_GetObjectItem(json, "OK");
    bool ok = (ok_item && cJSON_IsNumber(ok_item) && ok_item->valueint == 1);

    cJSON *qta_scatola_item = cJSON_GetObjectItem(json, "qta_scatola");
    uint32_t qta_scatola = (qta_scatola_item && cJSON_IsNumber(qta_scatola_item)) ? qta_scatola_item->valueint : 0;

    cJSON_Delete(json);
    if (!ok)
        return;

    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        banchetto_data_t *item = &s_list.items[s_current_idx];

        strncpy(item->matr_scatola_corrente, barcode, sizeof(item->matr_scatola_corrente) - 1);
        item->matr_scatola_corrente[sizeof(item->matr_scatola_corrente) - 1] = '\0';

        item->qta_scatola = qta_scatola;

        ESP_LOGI(TAG, "[%d] Scatola: %s (%lu/%lu)", s_current_idx,
                 item->matr_scatola_corrente, item->qta_scatola, item->qta_totale_scatola);

        xSemaphoreGive(data_mutex);

        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            app_banchetto_update_page2();
            lvgl_port_unlock();
        }
        web_server_broadcast_update();
    }
}
// esp_err_t banchetto_manager_login_badge(const char *badge)
// {
//     if (!badge || strlen(badge) == 0)
//         return ESP_FAIL;

//     switch (s_state)
//     {
//     case BANCHETTO_STATE_CONTROLLO:
//         banchetto_manager_set_state(BANCHETTO_STATE_CONTEGGIO);
//         return banchetto_manager_controllo(badge);
//     default:
//         break;
//     }

//     ESP_LOGI(TAG, "Badge: %s", badge);

//     char url[256];
//     snprintf(url, sizeof(url), "%s?key=%s&comando=badge&badge=%s",
//              SERVER_URL, device_key, badge);

//     int response_code = 0;
//     char *response_body = NULL;
//     esp_err_t ret = http_get_request(url, &response_code, &response_body);

//     if (ret != ESP_OK || response_code != 200)
//     {
//         ESP_LOGE(TAG, "Errore HTTP: %s (code: %d)", esp_err_to_name(ret), response_code);
//         if (response_body)
//             free(response_body);
//         return ESP_FAIL;
//     }

//     badge_response_t badge_resp;
//     if (parse_badge_response(response_body, &badge_resp) != ESP_OK)
//     {
//         ESP_LOGE(TAG, "Errore parsing risposta badge");
//         if (response_body)
//             free(response_body);
//         return ESP_FAIL;
//     }
//     free(response_body);

//     // ── LOGOUT ───────────────────────────────────────────────
//     if (badge_resp.matricola == 0)
//     {
//         if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
//         {
//             for (int i = 0; i < s_list.count; i++)
//             {
//                 s_list.items[i].sessione_aperta = false;
//                 s_list.items[i].operatore[0] = '\0';
//                 s_list.items[i].matricola[0] = '\0';
//                 s_list.items[i].qta_prod_sessione = 0;
//             }
//             xSemaphoreGive(data_mutex);
//             banchetto_manager_set_state(BANCHETTO_STATE_CHECKIN);
//             ESP_LOGI(TAG, "Sessione CHIUSA");
//             myBeep();
//             if (lvgl_port_lock(pdMS_TO_TICKS(100)))
//             {
//                 app_banchetto_update_page1();
//                 app_banchetto_update_page2();
//                 lvgl_port_unlock();
//             }
//             web_server_broadcast_update();
//             return ESP_OK;
//         }
//         return ESP_FAIL;
//     }

//     if (!badge_resp.success)
//     {
//         ESP_LOGE(TAG, "Login FALLITO: %s", badge_resp.errore);
//         myBeep();
//         vTaskDelay(pdMS_TO_TICKS(200));
//         myBeep();
//         return ESP_FAIL;
//     }

//     // ── LOGIN ────────────────────────────────────────────────
//     if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
//     {
//         for (int i = 0; i < s_list.count; i++)
//         {
//             snprintf(s_list.items[i].operatore, sizeof(s_list.items[i].operatore),
//                      "%s", badge_resp.operatore);
//             snprintf(s_list.items[i].matricola, sizeof(s_list.items[i].matricola),
//                      "%d", badge_resp.matricola);
//             s_list.items[i].sessione_aperta = true;
//             s_list.items[i].qta_prod_sessione = 0;
//         }
//         xSemaphoreGive(data_mutex);
//         banchetto_manager_set_state(BANCHETTO_STATE_CONTEGGIO);
//         ESP_LOGI(TAG, "Login OK — Operatore: %s mat:%s",
//                  badge_resp.operatore, s_list.items[0].matricola);
//         if (lvgl_port_lock(pdMS_TO_TICKS(100)))
//         {
//             app_banchetto_update_page1();
//             app_banchetto_update_page2();
//             lvgl_port_unlock();
//         }
//         myBeep();
//         web_server_broadcast_update();
//         return ESP_OK;
//     }
//     return ESP_FAIL;
// }

// ═══════════════════════════════════════════════════════════
// CONTROLLO
// ═══════════════════════════════════════════════════════════

esp_err_t banchetto_manager_login_badge(const char *badge)
{
    if (!badge || strlen(badge) == 0)
        return ESP_FAIL;

    ESP_LOGW(TAG, "login_badge — stato attuale: %d", s_state);

    switch (s_state)
    {
    case BANCHETTO_STATE_CONTROLLO:
        banchetto_manager_set_state(BANCHETTO_STATE_CONTEGGIO);
        return banchetto_manager_controllo(badge);

    case BANCHETTO_STATE_ATTESA_FORMATORE:
        return banchetto_manager_formazione_formatore(badge);

    case BANCHETTO_STATE_ATTESA_CONFERMA_FORMAZIONE:
        return banchetto_manager_formazione_accettazione(badge);

    default:
        break;
    }

    ESP_LOGI(TAG, "Badge: %s", badge);

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=badge&badge=%s",
             SERVER_URL, device_key, badge);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP: %s (code: %d)", esp_err_to_name(ret), response_code);
        if (response_body)
            free(response_body);
        return ESP_FAIL;
    }

    badge_response_t badge_resp;
    if (parse_badge_response(response_body, &badge_resp) != ESP_OK)
    {
        ESP_LOGE(TAG, "Errore parsing risposta badge");
        if (response_body)
            free(response_body);
        return ESP_FAIL;
    }

    // ── FORMAZIONE MANCANTE ──────────────────────────────
    if (badge_resp.formazione == 999)
    {
        ESP_LOGW(TAG, "Operatore %d: formazione mancante", badge_resp.matricola);

        // Recupera cod_art dalla risposta JSON per mostrarlo nel popup
        cJSON *json = cJSON_Parse(response_body);
        if (json)
        {
            cJSON *cod = cJSON_GetObjectItem(json, "cod_art");
            if (cod && cJSON_IsString(cod) && cod->valuestring)
            {
                strncpy(s_formazione_cod_art, cod->valuestring, sizeof(s_formazione_cod_art) - 1);
                s_formazione_cod_art[sizeof(s_formazione_cod_art) - 1] = '\0';
            }
            else
            {
                strncpy(s_formazione_cod_art, "sconosciuto", sizeof(s_formazione_cod_art) - 1);
            }
            cJSON_Delete(json);
        }
        free(response_body);

        // Salva badge operatore per riutilizzarlo al passo 3
        strncpy(s_formazione_badge, badge, sizeof(s_formazione_badge) - 1);
        s_formazione_badge[sizeof(s_formazione_badge) - 1] = '\0';

        banchetto_manager_set_state(BANCHETTO_STATE_ATTESA_FORMATORE);

        // Apri popup formazione (va chiamato con LVGL lock)
        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Non hai la formazione\nsul codice %s.\nChiama il responsabile.",
                     s_formazione_cod_art);
            popup_formazione_open("Formazione mancante", msg);
            lvgl_port_unlock();
        }

        myBeep();
        vTaskDelay(pdMS_TO_TICKS(200));
        myBeep();
        return ESP_FAIL;
    }

    free(response_body);

    // ── LOGOUT ───────────────────────────────────────────
    if (badge_resp.matricola == 0)
    {
        if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
        {
            for (int i = 0; i < s_list.count; i++)
            {
                s_list.items[i].sessione_aperta = false;
                s_list.items[i].operatore[0] = '\0';
                s_list.items[i].matricola[0] = '\0';
                s_list.items[i].qta_prod_sessione = 0;
            }
            xSemaphoreGive(data_mutex);
            banchetto_manager_set_state(BANCHETTO_STATE_CHECKIN);
            ESP_LOGI(TAG, "Sessione CHIUSA");
            myBeep();
            if (lvgl_port_lock(pdMS_TO_TICKS(100)))
            {
                app_banchetto_update_page1();
                app_banchetto_update_page2();
                lvgl_port_unlock();
            }
            web_server_broadcast_update();
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    if (!badge_resp.success)
    {
        ESP_LOGE(TAG, "Login FALLITO: %s", badge_resp.errore);
        myBeep();
        vTaskDelay(pdMS_TO_TICKS(200));
        myBeep();
        return ESP_FAIL;
    }

    // ── LOGIN ────────────────────────────────────────────
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        for (int i = 0; i < s_list.count; i++)
        {
            snprintf(s_list.items[i].operatore, sizeof(s_list.items[i].operatore),
                     "%s", badge_resp.operatore);
            snprintf(s_list.items[i].matricola, sizeof(s_list.items[i].matricola),
                     "%d", badge_resp.matricola);
            s_list.items[i].sessione_aperta = true;
            s_list.items[i].qta_prod_sessione = 0;
        }
        xSemaphoreGive(data_mutex);
        banchetto_manager_set_state(BANCHETTO_STATE_CONTEGGIO);
        ESP_LOGI(TAG, "Login OK — Operatore: %s mat:%s",
                 badge_resp.operatore, s_list.items[0].matricola);
        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            app_banchetto_update_page1();
            app_banchetto_update_page2();
            lvgl_port_unlock();
        }
        myBeep();
        web_server_broadcast_update();
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ═══════════════════════════════════════════════════════════
// FORMAZIONE FORMATORE — Step 2: badge del formatore
// ═══════════════════════════════════════════════════════════

esp_err_t banchetto_manager_formazione_formatore(const char *badge)
{
    if (!badge || strlen(badge) == 0)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Formazione formatore — badge: %s", badge);

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=formazione_formatore&badge=%s",
             SERVER_URL, device_key, badge);

    int response_code = 0;
    char *body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP formazione formatore (code: %d)", response_code);
        if (body)
            free(body);
        return ESP_FAIL;
    }

    // Parsa risposta
    bool ok = false;
    char msg_errore[64] = {0};

    cJSON *json = cJSON_Parse(body ? body : "");
    if (json)
    {
        cJSON *risp = cJSON_GetObjectItem(json, "risposta");
        if (risp && cJSON_IsString(risp) && risp->valuestring)
        {
            if (strcmp(risp->valuestring, "Ok del formatore") == 0)
            {
                ok = true;
            }
            else
            {
                strncpy(msg_errore, risp->valuestring, sizeof(msg_errore) - 1);
            }
        }
        cJSON_Delete(json);
    }
    if (body)
        free(body);

    if (ok)
    {
        // Formatore validato — passa a step 3
        banchetto_manager_set_state(BANCHETTO_STATE_ATTESA_CONFERMA_FORMAZIONE);
        ESP_LOGI(TAG, "Formatore OK — attesa conferma operatore");

        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_formazione_close();
            popup_formazione_open(
                "Conferma formazione",
                "Dichiaro di avere ricevuto\nla formazione necessaria.\nConferma tramite badge.");
            lvgl_port_unlock();
        }
        myBeep();
        return ESP_OK;
    }
    else
    {
        // Non è un formatore — resta in attesa
        ESP_LOGW(TAG, "Badge non valido come formatore: %s", msg_errore);

        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_avviso_open("Non sei un formatore",
                              strlen(msg_errore) > 0 ? msg_errore : "Badge non riconosciuto\ncome formatore.");
            lvgl_port_unlock();
        }
        myBeep();
        vTaskDelay(pdMS_TO_TICKS(200));
        myBeep();
        return ESP_FAIL;
    }
}

// ═══════════════════════════════════════════════════════════
// FORMAZIONE ACCETTAZIONE — Step 3: badge operatore conferma
// ═══════════════════════════════════════════════════════════

esp_err_t banchetto_manager_formazione_accettazione(const char *badge)
{
    if (!badge || strlen(badge) == 0)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Formazione accettazione — badge: %s", badge);

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=formazione_accettazione&badge=%s",
             SERVER_URL, device_key, badge);

    int response_code = 0;
    char *body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP formazione accettazione (code: %d)", response_code);
        if (body)
            free(body);
        return ESP_FAIL;
    }

    bool ok = false;
    char msg_errore[64] = {0};

    cJSON *json = cJSON_Parse(body ? body : "");
    if (json)
    {
        cJSON *risp = cJSON_GetObjectItem(json, "risposta");
        if (risp && cJSON_IsString(risp) && risp->valuestring)
        {
            if (strcmp(risp->valuestring, "Formazione registrata") == 0)
            {
                ok = true;
            }
            else
            {
                strncpy(msg_errore, risp->valuestring, sizeof(msg_errore) - 1);
            }
        }
        cJSON_Delete(json);
    }
    if (body)
        free(body);

    if (ok)
    {
        ESP_LOGI(TAG, "Formazione registrata — procedo con login");

        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_formazione_close();
            lvgl_port_unlock();
        }

        // Ripeti il login con il badge originale dell'operatore
        banchetto_manager_set_state(BANCHETTO_STATE_CHECKIN);
        return banchetto_manager_login_badge(s_formazione_badge);
    }
    else
    {
        ESP_LOGW(TAG, "Formazione non accettata: %s", msg_errore);

        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_avviso_open("Errore formazione",
                              strlen(msg_errore) > 0 ? msg_errore : "Formazione non registrata.");
            lvgl_port_unlock();
        }
        myBeep();
        vTaskDelay(pdMS_TO_TICKS(200));
        myBeep();
        return ESP_FAIL;
    }
}
esp_err_t banchetto_manager_controllo(const char *badge)
{
    if (!badge || strlen(badge) == 0)
        return ESP_ERR_INVALID_ARG;

    banchetto_data_t d;
    if (!banchetto_manager_get_data(&d))
        return ESP_FAIL;

    char url[300];
    snprintf(url, sizeof(url), "%s?key=%s&comando=controllo&badge=%s&ord_prod=%lu",
             SERVER_URL, device_key, badge, d.ord_prod);

    ESP_LOGI(TAG, "GET controllo: %s", url);

    int response_code = 0;
    char *body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP controllo (code: %d)", response_code);
        if (body)
            free(body);
        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_controllo_close();
            popup_avviso_open(LV_SYMBOL_WARNING " Errore", "Impossibile contattare il server.");
            lvgl_port_unlock();
        }
        return ESP_FAIL;
    }

    bool ok = false;
    char msg_buf[128] = "Operazione completata.";

    cJSON *json = cJSON_Parse(body ? body : "");
    if (json)
    {
        cJSON *err_item = cJSON_GetObjectItem(json, "errore");
        cJSON *op_item = cJSON_GetObjectItem(json, "operatore");
        cJSON *mat_item = cJSON_GetObjectItem(json, "matricola");

        if (err_item && cJSON_IsString(err_item) && err_item->valuestring[0])
        {
            ok = false;
            snprintf(msg_buf, sizeof(msg_buf), "%s", err_item->valuestring);
        }
        else if (op_item && mat_item)
        {
            ok = true;
            snprintf(msg_buf, sizeof(msg_buf), "Operazione completata.\n%s\nMatricola: %d",
                     cJSON_IsString(op_item) ? op_item->valuestring : "",
                     cJSON_IsNumber(mat_item) ? mat_item->valueint : 0);
        }
        cJSON_Delete(json);
    }
    if (body)
        free(body);

    if (lvgl_port_lock(pdMS_TO_TICKS(100)))
    {
        popup_controllo_close();
        popup_avviso_open(ok ? LV_SYMBOL_OK " Controllo OK"
                             : LV_SYMBOL_WARNING " Controllo rifiutato",
                          msg_buf);
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Controllo %s — badge=%s ord_prod=%lu",
             ok ? "OK" : "RIFIUTATO", badge, d.ord_prod);
    return ok ? ESP_OK : ESP_FAIL;
}

// ═══════════════════════════════════════════════════════════
// ASSEGNA BANCHETTO
// ═══════════════════════════════════════════════════════════

static void assegna_ok_btn_cb(lv_event_t *e)
{
    lv_obj_t *p = (lv_obj_t *)lv_event_get_user_data(e);
    if (p)
        lv_obj_del(p);
    app_assegna_banchetto_close();
}

esp_err_t banchetto_manager_assegna_banchetto(const char *barcode)
{
    if (!barcode || strlen(barcode) == 0)
        return ESP_ERR_INVALID_ARG;

    char url[256];
    snprintf(url, sizeof(url), "%s?key=%s&comando=assegna&banchetto=%s",
             SERVER_URL, device_key, barcode);

    ESP_LOGI(TAG, "GET assegna banchetto: %s", url);

    int response_code = 0;
    char *body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP assegna (code: %d)", response_code);
        if (body)
            free(body);
        if (lvgl_port_lock(pdMS_TO_TICKS(100)))
        {
            popup_avviso_open(LV_SYMBOL_WARNING " Errore", "Impossibile contattare il server.");
            lvgl_port_unlock();
        }
        return ESP_FAIL;
    }

    char msg_buf[128] = {0};
    bool ok = false;

    cJSON *json = cJSON_Parse(body ? body : "");
    if (json)
    {
        cJSON *ok_item = cJSON_GetObjectItem(json, "OK");
        cJSON *banc_item = cJSON_GetObjectItem(json, "banchetto");
        cJSON *err_item = cJSON_GetObjectItem(json, "errore");

        if (ok_item && cJSON_IsNumber(ok_item) && ok_item->valueint == 1)
        {
            ok = true;
            snprintf(msg_buf, sizeof(msg_buf), "Banchetto: %s\nInserire ordine per continuare",
                     (banc_item && cJSON_IsString(banc_item)) ? banc_item->valuestring : barcode);

            if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
            {
                for (int i = 0; i < s_list.count; i++)
                {
                    strncpy(s_list.items[i].banchetto,
                            (banc_item && cJSON_IsString(banc_item)) ? banc_item->valuestring : barcode,
                            sizeof(s_list.items[i].banchetto) - 1);
                }
                xSemaphoreGive(data_mutex);
            }
        }
        else
        {
            snprintf(msg_buf, sizeof(msg_buf), "%s",
                     (err_item && cJSON_IsString(err_item)) ? err_item->valuestring : "Errore sconosciuto");
        }
        cJSON_Delete(json);
    }
    if (body)
        free(body);

    if (lvgl_port_lock(pdMS_TO_TICKS(100)))
    {
        if (ok)
        {
            lv_obj_t *p = lv_obj_create(lv_scr_act());
            lv_obj_set_size(p, 520, 300);
            lv_obj_center(p);
            lv_obj_move_foreground(p);
            lv_obj_set_style_bg_color(p, lv_color_hex(0x141E30), 0);
            lv_obj_set_style_bg_opa(p, 255, 0);
            lv_obj_set_style_border_color(p, lv_color_hex(0x2ECC71), 0);
            lv_obj_set_style_border_width(p, 2, 0);
            lv_obj_set_style_radius(p, 12, 0);
            lv_obj_set_style_pad_all(p, 16, 0);
            lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_shadow_width(p, 30, 0);
            lv_obj_set_style_shadow_color(p, lv_color_hex(0x000000), 0);
            lv_obj_set_style_shadow_opa(p, 200, 0);

            lv_obj_t *tit = lv_label_create(p);
            lv_label_set_text(tit, LV_SYMBOL_OK " Assegnato");
            lv_obj_set_style_text_font(tit, &lv_font_montserrat_30, 0);
            lv_obj_set_style_text_color(tit, lv_color_hex(0x2ECC71), 0);
            lv_obj_set_width(tit, LV_PCT(100));
            lv_obj_set_style_text_align(tit, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(tit, LV_ALIGN_TOP_MID, 0, 0);

            lv_obj_t *lbl = lv_label_create(p);
            lv_label_set_text(lbl, msg_buf);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_26, 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_width(lbl, LV_PCT(100));
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -10);

            lv_obj_t *btn = lv_btn_create(p);
            lv_obj_set_size(btn, 120, 44);
            lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C5282), 0);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_add_event_cb(btn, assegna_ok_btn_cb, LV_EVENT_CLICKED, p);

            lv_obj_t *btn_lbl = lv_label_create(btn);
            lv_label_set_text(btn_lbl, "OK");
            lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_30, 0);
            lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(btn_lbl);

            myBeep();
        }
        else
        {
            popup_avviso_open(LV_SYMBOL_WARNING " Errore", msg_buf);
        }
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Assegna %s — barcode=%s", ok ? "OK" : "FAIL", barcode);
    return ok ? ESP_OK : ESP_FAIL;
}

// ═══════════════════════════════════════════════════════════
// BADGE DEZ→HEX
// ═══════════════════════════════════════════════════════════

static esp_err_t badge_dez_to_hex_inline(const char *dez, char *hex_out, size_t hex_size)
{
    if (!dez || !hex_out || hex_size < 11 || strlen(dez) != 10)
    {
        ESP_LOGE(TAG, "Parametri invalidi per conversione badge");
        return ESP_ERR_INVALID_ARG;
    }

    char dez_full[21];
    snprintf(dez_full, sizeof(dez_full), "0000000000%s", dez);

    char unique_id1[11] = {0};
    for (int i = 0; i < 10; i++)
    {
        char pair[3] = {dez_full[i * 2], dez_full[i * 2 + 1], 0};
        sprintf(&unique_id1[i], "%X", atoi(pair));
    }

    unsigned long long uid_dec = strtoull(unique_id1, NULL, 16);

    char bin_uid1[41] = {0};
    for (int i = 0; i < 40; i++)
        bin_uid1[39 - i] = ((uid_dec >> i) & 1) ? '1' : '0';

    char bin_original[41] = {0};
    for (int g = 0; g < 10; g++)
        for (int b = 0; b < 4; b++)
            bin_original[g * 4 + b] = bin_uid1[g * 4 + (3 - b)];

    for (int i = 0; i < 10; i++)
    {
        int val = 0;
        for (int j = 0; j < 4; j++)
            val = (val << 1) | (bin_original[i * 4 + j] - '0');
        sprintf(&hex_out[i], "%X", val);
    }
    hex_out[10] = '\0';
    ESP_LOGI(TAG, "HEX output: %s", hex_out);
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════
// LOGIN BY MATRICOLA
// ═══════════════════════════════════════════════════════════

esp_err_t banchetto_manager_login_by_matricola(uint16_t matricola)
{
    ESP_LOGI(TAG, "Matricola: %d", matricola);

    char url[256];
    snprintf(url, sizeof(url), "%s?operatore=%d", SERVER_BADGE_URL, matricola);

    int response_code = 0;
    char *response_body = NULL;
    esp_err_t ret = http_get_request(url, &response_code, &response_body);

    if (ret != ESP_OK || response_code != 200)
    {
        ESP_LOGE(TAG, "Errore HTTP badge.php: %s (code: %d)", esp_err_to_name(ret), response_code);
        if (response_body)
            free(response_body);
        return ESP_FAIL;
    }
    if (!response_body || strlen(response_body) == 0)
    {
        if (response_body)
            free(response_body);
        return ESP_FAIL;
    }

    char badge_dez[32];
    strncpy(badge_dez, response_body, sizeof(badge_dez) - 1);
    badge_dez[sizeof(badge_dez) - 1] = '\0';
    free(response_body);

    char *end = badge_dez + strlen(badge_dez) - 1;
    while (end > badge_dez && (*end == ' ' || *end == '\n' || *end == '\r'))
        *end-- = '\0';

    char badge_hex[16];
    ret = badge_dez_to_hex_inline(badge_dez, badge_hex, sizeof(badge_hex));
    if (ret != ESP_OK)
        return ESP_FAIL;

    return banchetto_manager_login_badge(badge_hex);
}

// ═══════════════════════════════════════════════════════════
// UTILITY
// ═══════════════════════════════════════════════════════════

void banchetto_manager_print_status(void)
{
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        ESP_LOGI(TAG, "Articoli: %d | CurrentIdx: %d | Stato: %d",
                 s_list.count, s_current_idx, s_state);
        for (int i = 0; i < s_list.count; i++)
        {
            banchetto_data_t *d = &s_list.items[i];
            ESP_LOGI(TAG, "[%d] %s | Op:%s | Mat:%s | Sess:%s",
                     i, d->codice_articolo, d->operatore, d->matricola,
                     d->sessione_aperta ? "APERTA" : "CHIUSA");
        }
        xSemaphoreGive(data_mutex);
    }
}

bool banchetto_manager_is_ready(void)
{
    bool ready = false;
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        if (s_list.count > 0)
        {
            banchetto_data_t *cur = &s_list.items[s_current_idx];
            ready = cur->sessione_aperta &&
                    (cur->matr_scatola_corrente[0] != '\0') &&
                    (cur->operatore[0] != '\0' || cur->matricola[0] != '\0');
        }
        xSemaphoreGive(data_mutex);
    }
    return ready;
}

void banchetto_manager_reset_data(void)
{
    if (xSemaphoreTake(data_mutex, pdMS_TO_TICKS(1000)))
    {
        memset(&s_list, 0, sizeof(banchetto_list_t));
        s_current_idx = 0;
        xSemaphoreGive(data_mutex);
        ESP_LOGI(TAG, "Dati azzerati");
    }
}

// {
//   "OK": 1,
//   "blocca_qta": 0,
//   "scatole": [
//     {
//       "cod_art": "0101202",
//       "qta_scatola": 16
//     },
//     {
//       "cod_art": "2816100",
//       "qta_scatola": 32
//     },
//     {
//       "cod_art": "0101303",
//       "qta_scatola": 8
//     },
//     {
//       "cod_art": "0101200",
//       "qta_scatola": 8
//     }
//   ],
//   "debug": ""
// }