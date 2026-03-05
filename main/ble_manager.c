#include "ble_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "BLE_MGR";

/* Callback per ogni dispositivo trovato durante lo scan */
static int ble_scan_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                          event->disc.length_data);
        
        char name[32] = "???";
        if (rc == 0 && fields.name != NULL && fields.name_len > 0) {
            int len = fields.name_len < sizeof(name) - 1 ? fields.name_len : sizeof(name) - 1;
            memcpy(name, fields.name, len);
            name[len] = '\0';
        }

        ESP_LOGI(TAG, "Trovato: %s | RSSI: %d | MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 name,
                 event->disc.rssi,
                 event->disc.addr.val[5], event->disc.addr.val[4],
                 event->disc.addr.val[3], event->disc.addr.val[2],
                 event->disc.addr.val[1], event->disc.addr.val[0]);

        return 0;
    }

    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGI(TAG, "Scan completato");
        return 0;
    }

    return 0;
}

/* Avvia uno scan BLE di 10 secondi */
static void ble_start_scan(void)
{
    struct ble_gap_disc_params scan_params = {
        .itvl = 160,              /* 100ms (unità: 0.625ms) */
        .window = 80,             /* 50ms */
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,             /* Scan attivo: richiede scan response */
        .filter_duplicates = 1,
    };

    ESP_LOGI(TAG, "Avvio scan BLE (10 secondi)...");

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 10000, &scan_params,
                           ble_scan_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Errore avvio scan: %d", rc);
    }
}

/* Callback di sync: chiamato quando lo stack BLE è pronto */
static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Errore configurazione indirizzo BLE: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Stack BLE pronto, avvio scan...");
    ble_start_scan();
}

/* Callback di reset */
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "Stack BLE reset, reason: %d", reason);
}

/* Task host NimBLE */
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "Task NimBLE host avviato");
    nimble_port_run();
    /* nimble_port_run non ritorna finché lo stack non viene fermato */
    nimble_port_freertos_deinit();
}

void ble_manager_init(void)
{
    ESP_LOGI(TAG, "Inizializzazione BLE Manager...");

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Errore nimble_port_init: %d", rc);
        return;
    }

    /* Registra callbacks */
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Avvia il task host */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Manager inizializzato");
}