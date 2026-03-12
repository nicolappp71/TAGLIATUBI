#include "ble_manager.h"
#include "banchetto_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gatt.h"
#include <string.h>

static const char *TAG = "BLE_MGR";
static volatile bool s_scan_paused = false;

/* UUID del servizio e characteristic della CNC */
static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0xbb, 0xaa, 0x33, 0x22, 0x11, 0x00, 0xcd, 0xab,
                     0x78, 0x56, 0x34, 0x12, 0x00, 0xa0, 0xe5, 0xd0);

static const ble_uuid128_t chr_uuid =
    BLE_UUID128_INIT(0xbb, 0xaa, 0x33, 0x22, 0x11, 0x00, 0xcd, 0xab,
                     0x78, 0x56, 0x34, 0x12, 0x01, 0xa0, 0xe5, 0xd0);

/* Nome target costruito dinamicamente */
static char target_name[32] = {0};

/* Dichiarazione della funzione esterna per aggiornare l'interfaccia */
extern void ui_update_ble_status(bool connected);

/* Stato connessione */
static uint16_t conn_handle = 0;
static bool is_connected = false;

/* Task handle per versa asincrono */
static TaskHandle_t versa_task_handle = NULL;

/* Riferimenti esterni */
extern void deep_sleep_reset_timer(void);
extern bool banchetto_manager_versa(uint32_t qta);

/* Forward declarations */
static void ble_start_scan(void);
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);

/* Task separato per eseguire versa (stack pesante) */
static void ble_versa_task(void *param)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGW(TAG, "VERSA da BLE!");
        deep_sleep_reset_timer();
        banchetto_manager_versa(1);
    }
}

/* Aggiorna il nome target dal banchetto_manager */
static void update_target_name(void)
{
    const char *id = banchetto_manager_get_banchetto_id();
    if (id && id[0] != '\0') {
        snprintf(target_name, sizeof(target_name), "CNC_%s", id);
    }
}

/* ===== GATT: subscribe alle indication ===== */

static int ble_on_subscribe(uint16_t conn, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    if (error->status == 0) {
        ESP_LOGI(TAG, "Subscribed alle indication OK");
    } else {
        ESP_LOGE(TAG, "Errore subscribe: %d", error->status);
    }
    return 0;
}

static int ble_on_disc_chr(uint16_t conn, const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0 && chr != NULL) {
        ESP_LOGI(TAG, "Characteristic trovata, val_handle: %d", chr->val_handle);

        uint8_t value[2] = {0x02, 0x00};
        int rc = ble_gattc_write_flat(conn, chr->val_handle + 1,
                                      value, sizeof(value),
                                      ble_on_subscribe, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Errore write CCCD: %d", rc);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Discovery characteristic completata");
    } else {
        ESP_LOGE(TAG, "Errore discovery chr: %d", error->status);
    }
    return 0;
}

static int ble_on_disc_svc(uint16_t conn, const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == 0 && svc != NULL) {
        ESP_LOGI(TAG, "Servizio trovato, handle: %d-%d",
                 svc->start_handle, svc->end_handle);

        int rc = ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle,
                                             svc->end_handle,
                                             &chr_uuid.u,
                                             ble_on_disc_chr, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Errore disc chr: %d", rc);
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Discovery servizio completata");
    } else {
        ESP_LOGE(TAG, "Errore discovery svc: %d", error->status);
    }
    return 0;
}

/* ===== GAP: scan, connect, notify ===== */

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                         event->disc.length_data);
        if (rc != 0) return 0;

        if (fields.name != NULL && fields.name_len > 0) {
            char name[32] = {0};
            int len = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
            memcpy(name, fields.name, len);

            update_target_name();

            if (target_name[0] != '\0' && strcmp(name, target_name) == 0) {
                ESP_LOGI(TAG, "Trovato %s! RSSI: %d, connessione...",
                         target_name, event->disc.rssi);

                ble_gap_disc_cancel();
                rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                                     10000, NULL, ble_gap_event_cb, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Errore connect: %d", rc);
                    ble_start_scan();
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            is_connected = true;
            
            // ---> AGGIUNTO QUI <---
            ui_update_ble_status(true); 

            ESP_LOGI(TAG, "Connesso a %s! handle: %d", target_name, conn_handle);

            int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &svc_uuid.u,
                                                ble_on_disc_svc, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Errore disc svc: %d", rc);
            }
        } else {
            ESP_LOGW(TAG, "Connessione fallita: %d, riscan...", event->connect.status);
            is_connected = false;
            
            // ---> AGGIUNTO QUI (per sicurezza in caso di fail dopo connect) <---
            ui_update_ble_status(false);

            ble_start_scan();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGW(TAG, "Disconnesso da %s (reason: %d), riscan...",
                 target_name, event->disconnect.reason);
        is_connected = false;
        conn_handle = 0;
        
        // ---> AGGIUNTO QUI <---
        ui_update_ble_status(false);

        vTaskDelay(pdMS_TO_TICKS(1000));
        ble_start_scan();
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.om != NULL) {
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            char buf[128] = {0};
            if (len < sizeof(buf)) {
                os_mbuf_copydata(event->notify_rx.om, 0, len, buf);
                buf[len] = '\0';

                ESP_LOGI(TAG, "RICEVUTO: %s (indication: %d)",
                         buf, event->notify_rx.indication);

                /* Verifica payload */
                const char *banc_id = banchetto_manager_get_banchetto_id();
                char id_check[48];
                snprintf(id_check, sizeof(id_check), "\"id\":\"%s\"", banc_id);

                if (strstr(buf, "\"v\":1") && strstr(buf, id_check)) {
                    /* Segnala al task versa (non chiamare versa qui!) */
                    if (versa_task_handle) {
                        xTaskNotifyGive(versa_task_handle);
                    }
                } else {
                    ESP_LOGW(TAG, "Payload non riconosciuto o ID errato: %s", buf);
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE: {
        if (!is_connected) {
            update_target_name();
            if (target_name[0] == '\0') {
                ESP_LOGW(TAG, "Banchetto non ancora assegnato, riprovo tra 5s...");
                vTaskDelay(pdMS_TO_TICKS(5000));
            } else {
                ESP_LOGI(TAG, "Scan completato, %s non trovato. Riscan...", target_name);
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            ble_start_scan();
        }
        return 0;
    }

    default:
        return 0;
    }
}

static void ble_start_scan(void)
{
    if (s_scan_paused) {
        ESP_LOGD(TAG, "Scan BLE sospesa (WiFi scan in corso).");
        return;
    }

    struct ble_gap_disc_params scan_params = {
        .itvl = 160,
        .window = 80,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };

    update_target_name();

    if (target_name[0] == '\0') {
        ESP_LOGW(TAG, "Nessun banchetto configurato, scan rinviato...");
        return;
    }

    ESP_LOGI(TAG, "Scan BLE per %s...", target_name);

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 15000, &scan_params,
                          ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Errore avvio scan: %d", rc);
    }
}

/* ===== Callback sync/reset ===== */

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Errore config indirizzo BLE: %d", rc);
        return;
    }

    update_target_name();
    ESP_LOGI(TAG, "Stack BLE pronto, target: %s",
             target_name[0] ? target_name : "(non ancora assegnato)");
    ble_start_scan();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "Stack BLE reset, reason: %d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "Task NimBLE host avviato");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ===== Pause / Resume scan (per WiFi coexistence) ===== */

void ble_manager_pause_scan(void)
{
    s_scan_paused = true;
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
        ESP_LOGI(TAG, "Scan BLE sospesa per WiFi scan.");
    }
}

void ble_manager_resume_scan(void)
{
    s_scan_paused = false;
    if (!is_connected) {
        ESP_LOGI(TAG, "Scan BLE ripresa.");
        ble_start_scan();
    }
}

/* ===== Init ===== */

void ble_manager_init(void)
{
    ESP_LOGI(TAG, "Inizializzazione BLE Manager...");

    update_target_name();
    ESP_LOGI(TAG, "Target iniziale: %s",
             target_name[0] ? target_name : "(banchetto non ancora caricato)");

    /* Task separato per versa: serve stack grande per HTTP + LVGL + audio */
    xTaskCreate(ble_versa_task, "ble_versa", 8192, NULL, 5, &versa_task_handle);

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Errore nimble_port_init: %d", rc);
        return;
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Manager inizializzato");
}