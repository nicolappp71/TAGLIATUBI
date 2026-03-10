#include "wifi_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "credential.h"
#include "offline_queue.h"

// --- CONFIGURAZIONE ---
#define MONITOR_CHECK_INTERVAL_MS   60000   // Ogni 30s controlla se c'è di meglio
#define RSSI_THRESHOLD_ROAMING      -75     // Se scende sotto -75, cerca altro

static const char *TAG = "WIFI_MANAGER";
EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static bool s_is_locked = false;

// Variabile per ricordare il BSSID corrente (per evitare switch inutili sulla stessa antenna)
static uint8_t current_bssid[6] = {0};

// Funzione "Radar": Scansiona TUTTO e trova il migliore in assoluto
static bool find_best_network_global(uint8_t *bssid_out, uint8_t *channel_out, int *rssi_out)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100, .scan_time.active.max = 200
    };

    esp_err_t ret;
    int retries = 0;
    
    do {
        ret = esp_wifi_scan_start(&scan_config, true);
        if (ret == ESP_ERR_WIFI_STATE) {
            vTaskDelay(pdMS_TO_TICKS(200));
            retries++;
        }
    } while (ret != ESP_OK && retries < 5);

    if (ret != ESP_OK) return false;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return false;

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) return false;

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    bool found = false;
    int best_rssi = -128;

    ESP_LOGD(TAG, "🔍 Scansione completa: Trovati %d AP.", ap_count);

    for (int i = 0; i < ap_count; i++) {
        if (strcmp((char *)ap_list[i].ssid, CONFIG_WIFI_SS) == 0) {
            // Filtro anti-fantasmi
            if (ap_list[i].rssi < 0 && ap_list[i].rssi > -95) {
                if (ap_list[i].rssi > best_rssi) {
                    best_rssi = ap_list[i].rssi;
                    *channel_out = ap_list[i].primary;
                    memcpy(bssid_out, ap_list[i].bssid, 6);
                    *rssi_out = best_rssi;
                    found = true;
                }
            }
        }
    }
    free(ap_list);
    return found;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_set_max_tx_power(8);
        ESP_LOGI(TAG, "WiFi avviato.");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        
        if (s_is_locked && s_retry_num > 3) {
            ESP_LOGE(TAG, "🚨 L'antenna bloccata non risponde più! Sblocco.");
            
            s_is_locked = false;
            s_retry_num = 0;

            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_STA, &conf);
            conf.sta.bssid_set = false;
            memset(conf.sta.bssid, 0, 6);
            
            esp_wifi_stop();
            esp_wifi_set_config(WIFI_IF_STA, &conf);
            esp_wifi_start();
            return;
        }

        if (s_retry_num >= 5) {
            ESP_LOGW(TAG, "Troppi tentativi (%d). WiFi non disponibile, UI avviata senza rete.", s_retry_num);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            return;
        }

        ESP_LOGI(TAG, "Disconnesso. Riprovo... (%d/5)", s_retry_num);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);

        ESP_LOGI(TAG, "✓ CONNESSO!");
        ESP_LOGI(TAG, "   Canale: %d | RSSI: %d dBm", ap_info.primary, ap_info.rssi);
        // Ora usiamo la variabile 'event', quindi il warning sparisce:
        ESP_LOGI(TAG, "   IP: " IPSTR, IP2STR(&event->ip_info.ip)); 

        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Sync operazioni offline pendenti
        if (offline_queue_count() > 0)
        {
            ESP_LOGI(TAG, "WiFi riconnesso — avvio sync coda offline...");
            offline_queue_process();
        }
    }
}

// --- TASK INTELLIGENTE ---
void wifi_monitor_task(void *pvParameters)
{
    uint8_t best_bssid[6];
    uint8_t best_channel;
    int best_rssi;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_CHECK_INTERVAL_MS));

        int current_rssi = wifi_get_rssi();
        
        if (current_rssi < RSSI_THRESHOLD_ROAMING && current_rssi != 0) {
            ESP_LOGW(TAG, "📉 Segnale degradato (%d dBm). Cerco alternative...", current_rssi);
            
            if (find_best_network_global(best_bssid, &best_channel, &best_rssi)) {
                // Se migliora di almeno 10dBm
                if (best_rssi > (current_rssi + 10)) {
                    ESP_LOGI(TAG, "🎉 Trovata antenna migliore (Ch %d | %d dBm). Switch...", best_channel, best_rssi);

                    wifi_config_t conf;
                    esp_wifi_get_config(WIFI_IF_STA, &conf);
                    memcpy(conf.sta.bssid, best_bssid, 6);
                    conf.sta.bssid_set = true;
                    conf.sta.channel = best_channel;

                    s_is_locked = true;
                    memcpy(current_bssid, best_bssid, 6);

                    esp_wifi_stop();
                    esp_wifi_set_config(WIFI_IF_STA, &conf);
                    esp_wifi_start();
                    esp_wifi_connect();
                }
            }
        }
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_country_t country = { .cc = "IT", .schan = 1, .nchan = 13, .max_tx_power = 20, .policy = WIFI_COUNTRY_POLICY_AUTO };
    esp_wifi_set_country(&country);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SS,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // --- FASE AVVIO ---
    uint8_t best_bssid[6];
    uint8_t best_channel;
    int best_rssi;
    
    vTaskDelay(pdMS_TO_TICKS(1000)); 

    ESP_LOGI(TAG, "🌍 Scansione GLOBALE iniziale...");
    
    bool initial_lock = false;
    for(int i=0; i<3; i++) {
        if (find_best_network_global(best_bssid, &best_channel, &best_rssi)) {
            ESP_LOGI(TAG, "👑 VINCITORE: Canale %d con %d dBm.", best_channel, best_rssi);
            
            memcpy(wifi_config.sta.bssid, best_bssid, 6);
            wifi_config.sta.bssid_set = true;
            wifi_config.sta.channel = best_channel;
            
            ESP_ERROR_CHECK(esp_wifi_stop());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            
            s_is_locked = true;
            memcpy(current_bssid, best_bssid, 6);
            initial_lock = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!initial_lock) {
        ESP_LOGW(TAG, "⚠ Nessuna rete trovata. Avvio Auto-Connect.");
    }

    ESP_LOGI(TAG, "Connessione...");
    esp_wifi_connect();

    xTaskCreate(wifi_monitor_task, "wifi_scout", 4096, NULL, 5, NULL);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
}

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) return ap_info.rssi;
    return 0;
}