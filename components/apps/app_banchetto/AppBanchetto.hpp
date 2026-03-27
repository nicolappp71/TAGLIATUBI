#pragma once
#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "json_parser.h"  // BANCHETTO_MAX_ITEMS

extern "C" {
#include "tagliatubi_manager.h"
}

// Codice banchetto che ha le pagine tagliatubi extra
#define TAGL_BANCHETTO_ID  "233"

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
    static void update_page4_scatola(void);
    static void add_versa_switch(lv_obj_t *sidebar, int y_offset = 220);

    // Tagliatubi state callback (via lv_async_call)
    static void on_tagl_state_update(void *user_data);

    // Accessibili dal timer callback (file scope)
    static lv_obj_t   *p4_pill_uomo_morto;
    static lv_obj_t   *p4_lbl_uomo_morto;
    static lv_obj_t   *p4_pill_materiale;
    static lv_obj_t   *p4_lbl_materiale;
    static lv_obj_t   *p4_pill_carter;
    static lv_obj_t   *p4_lbl_carter;
    static lv_timer_t *p4_uomo_morto_timer;

private:
    // ── Page 1 ──────────────────────────────────────────────────────────────
    static lv_obj_t *page1_scr[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_matricola[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_ciclo[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_codice[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_descr[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_odp[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_fase[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_sessione_stato[BANCHETTO_MAX_ITEMS];
    static lv_obj_t *lbl_banc[BANCHETTO_MAX_ITEMS];

    // ── Page 3 — Impostazioni tagliatubi (solo banchetto 233) ───────────────
    static lv_obj_t *page3_scr;
    static lv_obj_t *p3_lbl_codice;
    static lv_obj_t *p3_lbl_descr;
    static lv_obj_t *p3_lbl_lunghezza;
    static lv_obj_t *p3_lbl_quantita;
    static lv_obj_t *p3_lbl_velocita;
    static lv_obj_t *p3_lbl_pill;

    // ── Page 4 — Ciclo tagliatubi (solo banchetto 233) ─────────────────────
    static lv_obj_t   *page4_scr;
    static lv_obj_t   *p4_lbl_counter;
    static lv_obj_t   *p4_lbl_stato;
    static lv_obj_t   *p4_lbl_avanzamento;

    // ── Shared ───────────────────────────────────────────────────────────────
    static lv_obj_t *current_scr;
    static lv_obj_t *offline_banner;
    static lv_timer_t *offline_timer;
    static uint8_t    s_tagl_idx;   // indice del banchetto 233

    // ── Methods ──────────────────────────────────────────────────────────────
    static void crea_page1(uint8_t idx);
    static void crea_page3(uint8_t idx);
    static void crea_page4(uint8_t idx);
    static void update_page3(uint8_t idx);
    static void refresh_page4(tagliatubi_state_t state, const tagliatubi_data_t *data);
    static void swipe_event_cb(lv_event_t *e);
    static void offline_timer_cb(lv_timer_t *t);

    lv_obj_t *container;
    lv_obj_t *test_button;
};
