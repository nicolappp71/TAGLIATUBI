#pragma once
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "json_parser.h"  // BANCHETTO_MAX_ITEMS

class AppBanchetto : public ESP_Brookesia_PhoneApp
{
public:
    AppBanchetto();
    ~AppBanchetto();
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool resume(void) override;

    static void update_page1(uint8_t idx);
    static void update_page2(uint8_t idx);

private:
    static lv_obj_t *page1_scr[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_matricola[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_ciclo[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_codice[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_descr[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_odp[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_fase[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_sessione_stato[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_banc[BANCHETTO_MAX_ITEMS];

    static lv_obj_t *current_scr;
    static lv_obj_t *offline_banner;
    static lv_timer_t *offline_timer;

    static void crea_page1(uint8_t idx);
    static void swipe_event_cb(lv_event_t *e);
    static void offline_timer_cb(lv_timer_t *t);

    lv_obj_t *container;
    lv_obj_t *test_button;
};