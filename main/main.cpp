
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "bsp/touch.h"
#include "esp_brookesia.hpp"
#include "app_examples/phone/squareline/src/phone_app_squareline.hpp"
#include "apps.h"

#include "esp_sleep.h"
#include "driver/rtc_io.h"

extern "C"
{
#include "wifi_manager.h"
#include "web_server.h"
#include "banchetto_manager.h"
#include "settings_manager.h"
#include "json_parser.h"
#include "http_client.h"
#include "key_manager.h"
#include "rfid_manager.h"
#include "gpio_manager.h"
#include "i2c_manager.h"
#include "scanner_manager.h"
#include "battery_manager.h"
#include "log_manager.h"
#include "ble_manager.h"
#include "ota_manager.h"
}

static const char *TAG = "MAIN";

#define BACKLIGHT_TIMEOUT_MS 600000   // 30s → backlight off
#define DEEP_SLEEP_TIMEOUT_MS 3600000 // 1 min per test → deep sleep
#define WAKEUP_GPIO GPIO_NUM_5        // Bottone fisico

extern EventGroupHandle_t s_wifi_event_group;
extern int wifi_get_rssi(void);

/* Timestamp ultimo versa — aggiornato da banchetto_manager */
static volatile uint32_t last_versa_tick = 0;

/* Chiamata da banchetto_manager.c dopo versa OK */
extern "C" void deep_sleep_reset_timer(void)
{
    last_versa_tick = xTaskGetTickCount();
    ESP_LOGW(TAG, "AZZERATO timer deep sleep");
    if (bsp_display_lock(0))
    {
        lv_disp_trig_activity(NULL);
        bsp_display_unlock();
    }
}

void backlight_auto_task(void *arg)
{
    lv_display_t *disp = (lv_display_t *)arg;
    if (disp == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    bool backlight_on = true;
    last_versa_tick = xTaskGetTickCount();

    while (1)
    {
        uint32_t inactive_ms = 0;

        bsp_display_lock(0);
        inactive_ms = lv_disp_get_inactive_time(disp);
        bsp_display_unlock();

        /* Controlla inattività produttiva (tempo dall'ultimo versa) */
        uint32_t now = xTaskGetTickCount();
        uint32_t ms_since_versa = (now - last_versa_tick) * portTICK_PERIOD_MS;

        if (ms_since_versa > DEEP_SLEEP_TIMEOUT_MS)
        {
            ESP_LOGW(TAG, "Nessun versa da %lu ms, entro in DEEP SLEEP!",
                     (unsigned long)ms_since_versa);
            bsp_display_backlight_off();
            vTaskDelay(pdMS_TO_TICKS(500));

            esp_err_t ret = esp_sleep_enable_ext1_wakeup_io(
                1ULL << WAKEUP_GPIO, ESP_EXT1_WAKEUP_ANY_LOW);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Errore config wakeup: %s", esp_err_to_name(ret));
                continue;
            }

            rtc_gpio_pullup_en(WAKEUP_GPIO);
            rtc_gpio_pulldown_dis(WAKEUP_GPIO);

            ESP_LOGW(TAG, "DEEP SLEEP NOW - premi GPIO%d per risvegliare", WAKEUP_GPIO);
            esp_deep_sleep_start();
        }

        /* Backlight gestito dal touch display */
        if (inactive_ms > BACKLIGHT_TIMEOUT_MS && backlight_on)
        {
            bsp_display_backlight_off();
            backlight_on = false;
            ESP_LOGI(TAG, "Backlight OFF (inattivo %lu ms)", (unsigned long)inactive_ms);
        }
        else if (inactive_ms < BACKLIGHT_TIMEOUT_MS && !backlight_on)
        {
            bsp_display_backlight_on();
            backlight_on = true;
            last_versa_tick = xTaskGetTickCount(); // ← aggiungi questa riga
            ESP_LOGI(TAG, "Backlight ON (touch rilevato)");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// --- TASK BATTERIA ---
void battery_status_update_task(void *arg)
{
    ESP_Brookesia_StatusBar *status_bar = (ESP_Brookesia_StatusBar *)arg;
    if (status_bar == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    int percentuale = 0, voltage_mv = 0;
    bool in_carica = false;

    while (1)
    {
        if (ota_in_progress)
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (battery_get_percentage(&percentuale) == ESP_OK)
        {
            battery_get_voltage(&voltage_mv);
            battery_is_charging(&in_carica);
            bsp_display_lock(0);
            status_bar->setBatteryPercent(in_carica, percentuale);
            bsp_display_unlock();
            ESP_LOGI(TAG, "Batteria: %d%% (%dmV) %s",
                     percentuale, voltage_mv, in_carica ? "[CARICA]" : "");
        }
        else
        {
            ESP_LOGE(TAG, "Errore lettura batteria");
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

// --- TASK WIFI ---
void wifi_status_update_task(void *arg)
{
    ESP_Brookesia_StatusBar *status_bar = (ESP_Brookesia_StatusBar *)arg;
    if (status_bar == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        if (ota_in_progress)
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)
        {
            int rssi = wifi_get_rssi();
            int wifi_state = (rssi > -60) ? 3 : (rssi > -80) ? 2
                                            : (rssi > -100)  ? 1
                                                             : 0;
            bsp_display_lock(0);
            status_bar->setWifiIconState(wifi_state);
            bsp_display_unlock();
        }
        else
        {
            bsp_display_lock(0);
            status_bar->setWifiIconState(0);
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// --- TASK ORARIO ---
void orario_server_update_task(void *arg)
{
    ESP_Brookesia_StatusBar *status_bar = (ESP_Brookesia_StatusBar *)arg;
    if (status_bar == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    int ore, minuti;
    while (1)
    {
        if (ota_in_progress)
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT)
        {
            if (http_get_server_time(&ore, &minuti) == ESP_OK)
            {
                bool is_pm = (ore >= 12);
                bsp_display_lock(0);
                status_bar->setClock(ore, minuti, is_pm);
                bsp_display_unlock();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
// --- TASK BATTERIA ---

void inizializza_testi_gui()
{
    banchetto_data_t dati;
    banchetto_manager_get_data(&dati);
}

extern "C" void app_main(void)
{
    log_manager_init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_ERROR_CHECK(bsp_extra_codec_init());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {.buff_dma = false, .buff_spiram = true, .sw_rotate = true}};
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp)
        bsp_display_rotate(disp, LV_DISPLAY_ROTATION_0);

    bsp_display_lock(0);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_sta();

    gpio_init();
    init_usb_driver();
    init_rfid_uart();
    key_manager_init();
    banchetto_manager_init();
    banchetto_manager_fetch_from_server();
    settings_init();
    web_server_init();
    ble_manager_init();

    if (battery_manager_init() != ESP_OK)
        ESP_LOGE(TAG, "Errore init Battery Manager - continuando senza monitoraggio");

    ESP_Brookesia_Phone *banchetto = new ESP_Brookesia_Phone();
    if (banchetto == nullptr)
    {
        ESP_LOGE(TAG, "Errore creazione Phone");
        return;
    }

    ESP_Brookesia_PhoneStylesheet_t *banchetto_stylesheet =
        new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_1024_600_DARK_STYLESHEET();

    banchetto->addStylesheet(*banchetto_stylesheet);
    banchetto->activateStylesheet(*banchetto_stylesheet);

    if (!banchetto->begin())
    {
        ESP_LOGE(TAG, "Errore inizializzazione Phone");
        return;
    }

    ESP_Brookesia_StatusBar *status_bar = banchetto->getHome().getStatusBar();
    if (status_bar == nullptr)
    {
        ESP_LOGE(TAG, "Status bar non disponibile");
    }
    else
    {
        xTaskCreate(wifi_status_update_task, "WiFi Status", 4096, status_bar, 1, NULL);
        xTaskCreate(orario_server_update_task, "Orario Server", 4096, status_bar, 1, NULL);
        xTaskCreate(battery_status_update_task, "Battery Status", 4096, status_bar, 1, NULL);
    }

    /* Backlight task: fuori dall'if della status_bar, serve sempre */
    xTaskCreate(backlight_auto_task, "Backlight Auto", 4096, disp, 1, NULL);

    // esp_err_t sd_ret = bsp_sdcard_mount();
    // if (sd_ret == ESP_OK)
    // {
    //     ESP_LOGI(TAG, "SD Card montata correttamente!");
    //     log_manager_sd_ready();
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "Fallimento montaggio SD: %s", esp_err_to_name(sd_ret));
    // }

    // FILE *f = fopen("/sdcard/test_sd.txt", "r");
    // if (f == NULL)
    // {
    //     ESP_LOGE(TAG, "Impossibile aprire il file su SD!");
    // }
    // else
    // {
    //     char line[64];
    //     fgets(line, sizeof(line), f);
    //     fclose(f);
    //     ESP_LOGI(TAG, "Contenuto file SD: %s", line);
    // }
    esp_err_t sd_ret = bsp_sdcard_mount();
    if (sd_ret == ESP_OK)
    {
        ESP_LOGI(TAG, "SD Card montata correttamente!");
        log_manager_sd_ready();

        if (status_bar != nullptr)
        {
            status_bar->setSdIconState(1);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Fallimento montaggio SD: %s", esp_err_to_name(sd_ret));

        if (status_bar != nullptr)
        {
            status_bar->setSdIconState(0);
        }
    }
    banchetto->installApp(new AppBanchetto());
    inizializza_testi_gui();
    banchetto->installApp(new Calculator());
    banchetto->installApp(new MiaApp());
    banchetto->installApp(new Logged());
    banchetto->installApp(new DocBrowser());

    if (bsp_extra_player_init() != ESP_OK)
        ESP_LOGE(TAG, "bsp_extra_player_init failed - audio non disponibile");
    else
        ESP_LOGI(TAG, "Audio player inizializzato");

    bsp_display_unlock();
    bsp_display_backlight_on();

    ESP_LOGI(TAG, "Sistema avviato completamente");
}