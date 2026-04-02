#include "AppBanchetto.hpp"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "screens.h"
#include "fonts.h"
#include <string.h>
#include <stdlib.h>

extern "C"
{
#include "banchetto_manager.h"
#include "tastiera.h"
#include "wifi_manager.h"
}
extern "C" void popup_controllo_open(void);
extern "C" void popup_avviso_open(const char *titolo, const char *messaggio, bool offline);

static const char *TAG = "AppBanchetto";

// ─── Sensibilità swipe (pixel minimi) ────────────────────
#define SWIPE_H_THRESHOLD  80   // orizzontale: cambia pagina
#define SWIPE_V_THRESHOLD  80   // verticale: cambia articolo

// ─── Toggle versa ─────────────────────────────────────────
#define VERSA_SW_MAX 4
static lv_obj_t *s_versa_switches[VERSA_SW_MAX] = {};
static uint8_t   s_versa_sw_count = 0;

static void versa_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    banchetto_manager_set_versa_abilitato(on);
    // Sincronizza tutti gli altri switch
    for (uint8_t i = 0; i < s_versa_sw_count; i++) {
        if (s_versa_switches[i] && s_versa_switches[i] != sw) {
            if (on) lv_obj_add_state(s_versa_switches[i], LV_STATE_CHECKED);
            else    lv_obj_clear_state(s_versa_switches[i], LV_STATE_CHECKED);
        }
    }
}

// ─── Variabili statiche — array per articolo ─────────────
lv_obj_t *AppBanchetto::page1_scr[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_matricola[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_ciclo[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_codice[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_descr[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_odp[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_fase[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_sessione_stato[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::lbl_banc[BANCHETTO_MAX_ITEMS] = {};
lv_obj_t *AppBanchetto::current_scr  = nullptr;
lv_obj_t *AppBanchetto::offline_banner = nullptr;
lv_timer_t *AppBanchetto::offline_timer = nullptr;

// ─── Variabili statiche — pagine tagliatubi ──────────────
lv_obj_t *AppBanchetto::page3_scr       = nullptr;
lv_obj_t *AppBanchetto::page4_scr       = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_codice   = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_descr    = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_lunghezza = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_quantita = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_velocita = nullptr;
lv_obj_t *AppBanchetto::p3_lbl_pill     = nullptr;
lv_obj_t *AppBanchetto::p3_pill         = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_counter      = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_stato        = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_avanzamento  = nullptr;
lv_obj_t   *AppBanchetto::p4_pill_uomo_morto  = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_uomo_morto   = nullptr;
lv_obj_t   *AppBanchetto::p4_pill_materiale   = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_materiale    = nullptr;
lv_obj_t   *AppBanchetto::p4_pill_carter      = nullptr;
lv_obj_t   *AppBanchetto::p4_lbl_carter       = nullptr;
lv_timer_t *AppBanchetto::p4_uomo_morto_timer = nullptr;
uint8_t   AppBanchetto::s_tagl_idx      = 255;

// ─── Variabili statiche — pagine collaudo ────────────────
lv_obj_t *AppBanchetto::page5_scr             = nullptr;
lv_obj_t *AppBanchetto::s_coll_scan_panel     = nullptr;
lv_obj_t *AppBanchetto::s_coll_scan_lbl       = nullptr;
lv_obj_t *AppBanchetto::s_coll_data_panel     = nullptr;
lv_obj_t *AppBanchetto::s_coll_lbl_operatore  = nullptr;
lv_obj_t *AppBanchetto::s_coll_lbl_matricola  = nullptr;
lv_obj_t *AppBanchetto::s_coll_lbl_consumo_ist= nullptr;
lv_obj_t *AppBanchetto::s_coll_lbl_giri_ist   = nullptr;
lv_obj_t *AppBanchetto::s_coll_lbl_consumo_min[3]     = {};
lv_obj_t *AppBanchetto::s_coll_lbl_consumo_ist_col[3] = {};
lv_obj_t *AppBanchetto::s_coll_lbl_consumo_max[3]     = {};
lv_obj_t *AppBanchetto::s_coll_lbl_giri_min[3]        = {};
lv_obj_t *AppBanchetto::s_coll_lbl_giri_ist_col[3]    = {};
lv_obj_t *AppBanchetto::s_coll_lbl_giri_max[3]        = {};
lv_obj_t *AppBanchetto::s_coll_btn_fase[3]            = {};
lv_obj_t *AppBanchetto::s_coll_tbl            = nullptr;
uint8_t   AppBanchetto::s_coll_idx            = 255;

// ─── Numpad inline (page3) ───────────────────────────────
struct TNumpadTarget { lv_obj_t *label; const char *title; int max_digits; };
static TNumpadTarget  t_np_targets[2];   // [0]=lunghezza [1]=velocita

static lv_obj_t *t_np_overlay    = nullptr;
static lv_obj_t *t_np_popup      = nullptr;
static lv_obj_t *t_np_display    = nullptr;
static lv_obj_t *t_np_target_lbl = nullptr;
static int       t_np_maxdig     = 5;
static char      t_np_buf[8]     = {0};

static void t_np_update_display(void) {
    if (t_np_display) lv_label_set_text(t_np_display, t_np_buf[0] ? t_np_buf : "0");
}
static void t_np_close(void) {
    if (t_np_overlay) { lv_obj_del(t_np_overlay); t_np_overlay = nullptr; }
    if (t_np_popup)   { lv_obj_del(t_np_popup);   t_np_popup   = nullptr; }
    t_np_display = t_np_target_lbl = nullptr;
    memset(t_np_buf, 0, sizeof(t_np_buf));
}
static void t_cb_np_digit(lv_event_t *e) {
    char d = (char)(intptr_t)lv_event_get_user_data(e);
    size_t len = strlen(t_np_buf);
    if ((int)len >= t_np_maxdig) return;
    if (len == 0 && d == '0') return;
    t_np_buf[len] = d; t_np_buf[len+1] = '\0';
    t_np_update_display();
}
static void t_cb_np_bs(lv_event_t *e) {
    (void)e;
    size_t len = strlen(t_np_buf);
    if (len > 0) t_np_buf[len-1] = '\0';
    t_np_update_display();
}
static void t_cb_np_ok(lv_event_t *e) {
    (void)e;
    if (t_np_target_lbl && t_np_buf[0])
        lv_label_set_text(t_np_target_lbl, t_np_buf);
    t_np_close();
}
static void t_cb_np_cancel(lv_event_t *e) { (void)e; t_np_close(); }

static lv_obj_t *t_np_btn(lv_obj_t *parent, const char *txt, lv_color_t bg,
                            lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, 255, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}
static void t_cb_open_numpad(lv_event_t *e) {
    TNumpadTarget *t = static_cast<TNumpadTarget *>(lv_event_get_user_data(e));
    if (t_np_popup) return;
    t_np_target_lbl = t->label;
    t_np_maxdig     = t->max_digits;
    memset(t_np_buf, 0, sizeof(t_np_buf));  // sempre vuoto all'apertura

    t_np_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(t_np_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(t_np_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(t_np_overlay, 160, 0);
    lv_obj_set_style_border_width(t_np_overlay, 0, 0);
    lv_obj_set_style_radius(t_np_overlay, 0, 0);
    lv_obj_clear_flag(t_np_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t_np_overlay, LV_OBJ_FLAG_CLICKABLE);

    t_np_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(t_np_popup, 400, 500);
    lv_obj_center(t_np_popup);
    lv_obj_move_foreground(t_np_popup);
    lv_obj_set_style_bg_color(t_np_popup, lv_color_hex(0x141E30), 0);
    lv_obj_set_style_bg_opa(t_np_popup, 255, 0);
    lv_obj_set_style_border_color(t_np_popup, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_border_width(t_np_popup, 2, 0);
    lv_obj_set_style_radius(t_np_popup, 16, 0);
    lv_obj_set_style_pad_all(t_np_popup, 16, 0);
    lv_obj_clear_flag(t_np_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titolo = lv_label_create(t_np_popup);
    lv_label_set_text(titolo, t->title);
    lv_obj_set_style_text_font(titolo, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(titolo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(titolo, LV_PCT(100));
    lv_obj_set_style_text_align(titolo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(titolo, LV_ALIGN_TOP_MID, 0, 0);

    t_np_display = lv_label_create(t_np_popup);
    lv_label_set_text(t_np_display, t_np_buf[0] ? t_np_buf : "0");
    lv_obj_set_style_text_font(t_np_display, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(t_np_display, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_bg_color(t_np_display, lv_color_hex(0x0D1526), 0);
    lv_obj_set_style_bg_opa(t_np_display, 255, 0);
    lv_obj_set_style_radius(t_np_display, 8, 0);
    lv_obj_set_style_pad_all(t_np_display, 10, 0);
    lv_obj_set_style_text_align(t_np_display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(t_np_display, LV_PCT(100));
    lv_obj_align(t_np_display, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *grid = lv_obj_create(t_np_popup);
    lv_obj_set_size(grid, LV_PCT(100), 240);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_opa(grid, 0, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);

    const char *digits[] = {"1","2","3","4","5","6","7","8","9"};
    for (int i = 0; i < 9; i++) {
        lv_obj_t *b = t_np_btn(grid, digits[i], lv_color_hex(0x2C5282),
                                t_cb_np_digit, (void*)(intptr_t)(digits[i][0]));
        lv_obj_set_grid_cell(b, LV_GRID_ALIGN_STRETCH, i%3, 1, LV_GRID_ALIGN_STRETCH, i/3, 1);
    }
    lv_obj_t *b_bs = t_np_btn(grid, LV_SYMBOL_BACKSPACE, lv_color_hex(0xF39C12), t_cb_np_bs, nullptr);
    lv_obj_set_grid_cell(b_bs, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
    lv_obj_t *b0 = t_np_btn(grid, "0", lv_color_hex(0x2C5282), t_cb_np_digit, (void*)(intptr_t)'0');
    lv_obj_set_grid_cell(b0, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 3, 1);
    lv_obj_t *dummy = lv_obj_create(grid);
    lv_obj_set_style_bg_opa(dummy, 0, 0);
    lv_obj_set_style_border_width(dummy, 0, 0);
    lv_obj_set_grid_cell(dummy, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

    lv_obj_t *btn_c = t_np_btn(t_np_popup, "Annulla", lv_color_hex(0xFA0000), t_cb_np_cancel, nullptr);
    lv_obj_set_size(btn_c, 160, 55);
    lv_obj_align(btn_c, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *btn_ok = t_np_btn(t_np_popup, "OK", lv_color_hex(0x2ECC71), t_cb_np_ok, nullptr);
    lv_obj_set_size(btn_ok, 160, 55);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

// ─── Tagliatubi state payload ─────────────────────────────
struct TaqlUpdatePayload { tagliatubi_state_t state; tagliatubi_data_t data; };

LV_IMG_DECLARE(b2);

// ─────────────────────────────────────────────────────────
// TAGLIATUBI STATE CALLBACK
// ─────────────────────────────────────────────────────────

static void banchetto_tagl_state_cb(tagliatubi_state_t state, const tagliatubi_data_t *data)
{
    auto *p = new TaqlUpdatePayload{state, *data};
    lv_async_call(AppBanchetto::on_tagl_state_update, p);
}

void AppBanchetto::on_tagl_state_update(void *user_data)
{
    auto *p = static_cast<TaqlUpdatePayload *>(user_data);
    refresh_page4(p->state, &p->data);
    delete p;
}

static const char *tagl_state_label(tagliatubi_state_t s)
{
    switch (s) {
        case TAGL_STATE_RUNNING:           return "IN CORSO";
        case TAGL_STATE_CUTTING:           return "TAGLIO";
        case TAGL_STATE_DONE:              return "COMPLETATO";
        case TAGL_STATE_ERROR_NO_MATERIAL:  return "NO MATERIALE";
        case TAGL_STATE_ERROR_SAFETY:       return "SPORTELLO APERTO";
        case TAGL_STATE_ERROR_LENGTH:       return "ERRORE MISURA";
        case TAGL_STATE_BOX_FULL:           return "SCATOLA PIENA";
        case TAGL_STATE_ERROR_UOMO_MORTO:   return "SICUREZZA";
        default:                            return "IDLE";
    }
}
static lv_color_t tagl_state_color(tagliatubi_state_t s)
{
    switch (s) {
        case TAGL_STATE_RUNNING:           return lv_color_hex(0x00FF88);
        case TAGL_STATE_CUTTING:           return lv_color_hex(0xFF8800);
        case TAGL_STATE_DONE:              return lv_color_hex(0x00D4FF);
        case TAGL_STATE_ERROR_NO_MATERIAL:
        case TAGL_STATE_ERROR_SAFETY:
        case TAGL_STATE_ERROR_LENGTH:
        case TAGL_STATE_ERROR_UOMO_MORTO:  return lv_color_hex(0xFF4444);
        case TAGL_STATE_BOX_FULL:          return lv_color_hex(0xFFAA00);
        default:                           return lv_color_hex(0x888888);
    }
}

// ─────────────────────────────────────────────────────────
// TIMER — polling pulsante uomo morto (200ms)
// ─────────────────────────────────────────────────────────
static void update_pin_pill(lv_obj_t *pill, lv_obj_t *lbl, bool ok)
{
    lv_label_set_text(lbl, ok ? LV_SYMBOL_OK " OK" : LV_SYMBOL_CLOSE " NO");
    lv_obj_set_style_bg_color(pill, ok ? lv_color_hex(0x16A34A) : lv_color_hex(0xE11D48), 0);
}

static void p4_uomo_morto_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!AppBanchetto::p4_pill_uomo_morto ||
        !AppBanchetto::p4_pill_materiale  ||
        !AppBanchetto::p4_pill_carter) {
        ESP_LOGW("AppBanchetto", "[p4 timer] pill nullptr — cancello timer");
        lv_timer_del(t);
        AppBanchetto::p4_uomo_morto_timer = nullptr;
        return;
    }

    bool sic = tagliatubi_manager_is_uomo_morto();
    bool mat = tagliatubi_manager_is_materiale();
    bool car = tagliatubi_manager_is_carter();

    // Log solo quando cambia stato (evita flood)
    static bool prev_sic = false, prev_mat = false, prev_car = false;
    if (sic != prev_sic || mat != prev_mat || car != prev_car) {
        ESP_LOGI("AppBanchetto", "[pin] SICUREZZA=%s  MATERIALE=%s  CARTER=%s",
                 sic ? "OK" : "NO", mat ? "OK" : "NO", car ? "OK" : "NO");
        prev_sic = sic; prev_mat = mat; prev_car = car;
    }

    update_pin_pill(AppBanchetto::p4_pill_uomo_morto, AppBanchetto::p4_lbl_uomo_morto, sic);
    update_pin_pill(AppBanchetto::p4_pill_materiale,  AppBanchetto::p4_lbl_materiale,  mat);
    update_pin_pill(AppBanchetto::p4_pill_carter,     AppBanchetto::p4_lbl_carter,     car);
}

// ─────────────────────────────────────────────────────────
// CREA PAGE 3 — Impostazioni tagliatubi
// ─────────────────────────────────────────────────────────

void AppBanchetto::crea_page3(uint8_t idx)
{
    banchetto_data_t d;
    banchetto_manager_get_item(idx, &d);

    lv_obj_t *scr = lv_obj_create(NULL);
    page3_scr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(scr, 255, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);

    // ── SIDEBAR NERA ─────────────────────────────────────────
    lv_obj_t *sidebar = lv_obj_create(scr);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 260, 549);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_bg_opa(sidebar, 255, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 24, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_EVENT_BUBBLE);
    add_versa_switch(sidebar);

    lv_obj_t *lbl_op_tit = lv_label_create(sidebar);
    lv_label_set_text(lbl_op_tit, "OPERATORE");
    lv_obj_set_style_text_font(lbl_op_tit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_op_tit, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(lbl_op_tit, 4, 0);
    lv_obj_align(lbl_op_tit, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_op_val = lv_label_create(sidebar);
    lv_label_set_text(lbl_op_val, d.matricola);
    lv_obj_set_style_text_font(lbl_op_val, &ui_font_my_font75, 0);
    lv_obj_set_style_text_color(lbl_op_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_op_val, LV_ALIGN_TOP_LEFT, 0, 28);

    p3_pill = lv_obj_create(sidebar);
    lv_obj_t *pill = p3_pill;
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, d.sessione_aperta ? lv_color_hex(0x16A34A) : lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_bg_opa(pill, 255, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 4, 0);
    lv_obj_set_style_pad_top(pill, 4, 0);
    lv_obj_set_style_pad_bottom(pill, 4, 0);
    lv_obj_set_style_pad_left(pill, 10, 0);
    lv_obj_set_style_pad_right(pill, 10, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_EVENT_BUBBLE);

    p3_lbl_pill = lv_label_create(pill);
    lv_label_set_text(p3_lbl_pill, d.sessione_aperta ? "LOGGATO " LV_SYMBOL_OK : "NON LOGGATO");
    lv_obj_set_style_text_font(p3_lbl_pill, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(p3_lbl_pill, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(p3_lbl_pill, 2, 0);
    lv_obj_align(p3_lbl_pill, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sep = lv_obj_create(sidebar);
    lv_obj_set_pos(sep, 0, 340);
    lv_obj_set_size(sep, 212, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(sep, 255, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *lbl_banc_tit = lv_label_create(sidebar);
    lv_label_set_text(lbl_banc_tit, "BANCHETTO");
    lv_obj_set_style_text_font(lbl_banc_tit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_banc_tit, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(lbl_banc_tit, 3, 0);
    lv_obj_align(lbl_banc_tit, LV_ALIGN_TOP_LEFT, 0, 358);

    lv_obj_t *lbl_banc_val = lv_label_create(sidebar);
    lv_label_set_text(lbl_banc_val, d.banchetto);
    lv_obj_set_style_text_font(lbl_banc_val, &ui_font_my_font75, 0);
    lv_obj_set_style_text_color(lbl_banc_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_banc_val, LV_ALIGN_TOP_LEFT, 0, 382);

    // ── CODICE ARTICOLO — hero giallo (readonly, da banchetto) ───────────────
    lv_obj_t *box_codice = lv_obj_create(scr);
    lv_obj_set_pos(box_codice, 276, 16);
    lv_obj_set_size(box_codice, 732, 120);
    lv_obj_set_style_bg_color(box_codice, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(box_codice, 255, 0);
    lv_obj_set_style_border_width(box_codice, 0, 0);
    lv_obj_set_style_radius(box_codice, 12, 0);
    lv_obj_set_style_pad_left(box_codice, 20, 0);
    lv_obj_set_style_pad_top(box_codice, 12, 0);
    lv_obj_clear_flag(box_codice, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box_codice, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_codice);
        lv_label_set_text(t, "CODICE ARTICOLO");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x00000066), 0);
        lv_obj_set_style_text_letter_space(t, 4, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        p3_lbl_codice = lv_label_create(box_codice);
        lv_label_set_text(p3_lbl_codice, d.codice_articolo);
        lv_obj_set_style_text_font(p3_lbl_codice, &ui_font_my_font75, 0);
        lv_obj_set_style_text_color(p3_lbl_codice, lv_color_hex(0x000000), 0);
        lv_obj_align(p3_lbl_codice, LV_ALIGN_TOP_LEFT, 0, 26);
    }

    // ── LUNGHEZZA — blu, tappabile ────────────────────────────────────────────
    t_np_targets[0] = { nullptr, "LUNGHEZZA (mm)", 5 };
    lv_obj_t *box_lung = lv_btn_create(scr);
    lv_obj_set_pos(box_lung, 276, 152);
    lv_obj_set_size(box_lung, 342, 110);
    lv_obj_set_style_bg_color(box_lung, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_bg_color(box_lung, lv_color_hex(0x2C70E0), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(box_lung, 255, 0);
    lv_obj_set_style_border_width(box_lung, 0, 0);
    lv_obj_set_style_radius(box_lung, 12, 0);
    lv_obj_set_style_shadow_width(box_lung, 0, 0);
    lv_obj_set_style_pad_all(box_lung, 14, 0);
    lv_obj_clear_flag(box_lung, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *t = lv_label_create(box_lung);
        lv_label_set_text(t, "LUNGHEZZA");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF99), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        p3_lbl_lunghezza = lv_label_create(box_lung);
        lv_label_set_text(p3_lbl_lunghezza, "0");
        lv_obj_set_style_text_font(p3_lbl_lunghezza, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(p3_lbl_lunghezza, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(p3_lbl_lunghezza, LV_ALIGN_TOP_LEFT, 0, 28);
    }
    t_np_targets[0].label = p3_lbl_lunghezza;
    lv_obj_add_event_cb(box_lung, t_cb_open_numpad, LV_EVENT_CLICKED, &t_np_targets[0]);

    // ── SEPARATORE (vuoto) ────────────────────────────────────────────────────
    lv_obj_t *box_qta = lv_obj_create(scr);
    lv_obj_set_pos(box_qta, 634, 152);
    lv_obj_set_size(box_qta, 16, 110);
    lv_obj_set_style_bg_opa(box_qta, 0, 0);
    lv_obj_set_style_border_width(box_qta, 0, 0);
    lv_obj_clear_flag(box_qta, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(box_qta, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_label_create(box_qta);

    // ── VELOCITA' — rosso, tappabile ─────────────────────────────────────────
    t_np_targets[1] = { nullptr, "VELOCITA' (1-99)", 2 };
    lv_obj_t *box_vel = lv_btn_create(scr);
    lv_obj_set_pos(box_vel, 666, 152);
    lv_obj_set_size(box_vel, 342, 110);
    lv_obj_set_style_bg_color(box_vel, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_bg_color(box_vel, lv_color_hex(0xC01038), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(box_vel, 255, 0);
    lv_obj_set_style_border_width(box_vel, 0, 0);
    lv_obj_set_style_radius(box_vel, 12, 0);
    lv_obj_set_style_shadow_width(box_vel, 0, 0);
    lv_obj_set_style_pad_all(box_vel, 14, 0);
    lv_obj_clear_flag(box_vel, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *t = lv_label_create(box_vel);
        lv_label_set_text(t, "VELOCITA'");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF99), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        p3_lbl_velocita = lv_label_create(box_vel);
        lv_label_set_text(p3_lbl_velocita, "0");
        lv_obj_set_style_text_font(p3_lbl_velocita, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(p3_lbl_velocita, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(p3_lbl_velocita, LV_ALIGN_TOP_LEFT, 0, 28);
    }
    t_np_targets[1].label = p3_lbl_velocita;
    lv_obj_add_event_cb(box_vel, t_cb_open_numpad, LV_EVENT_CLICKED, &t_np_targets[1]);

    // ── DESCRIZIONE ARTICOLO ──────────────────────────────────────────────────
    lv_obj_t *box_descr = lv_obj_create(scr);
    lv_obj_set_pos(box_descr, 276, 278);
    lv_obj_set_size(box_descr, 732, 130);
    lv_obj_set_style_bg_color(box_descr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(box_descr, 255, 0);
    lv_obj_set_style_border_width(box_descr, 0, 0);
    lv_obj_set_style_radius(box_descr, 12, 0);
    lv_obj_set_style_pad_all(box_descr, 16, 0);
    lv_obj_clear_flag(box_descr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_descr, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_descr);
        lv_label_set_text(t, "DESCRIZIONE ARTICOLO");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x00000044), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        p3_lbl_descr = lv_label_create(box_descr);
        lv_label_set_text(p3_lbl_descr, d.descrizione_articolo);
        lv_obj_set_style_text_font(p3_lbl_descr, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(p3_lbl_descr, lv_color_hex(0x000000), 0);
        lv_obj_set_width(p3_lbl_descr, 700);
        lv_label_set_long_mode(p3_lbl_descr, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(p3_lbl_descr, LV_ALIGN_TOP_LEFT, 0, 28);
    }

    // ── BOTTONE SALVA ─────────────────────────────────────────────────────────
    lv_obj_t *btn_salva = lv_btn_create(scr);
    lv_obj_set_pos(btn_salva, 276, 424);
    lv_obj_set_size(btn_salva, 732, 110);
    lv_obj_set_style_bg_color(btn_salva, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_salva, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_salva, 12, 0);
    lv_obj_set_style_border_width(btn_salva, 0, 0);
    lv_obj_set_style_shadow_width(btn_salva, 0, 0);
    lv_obj_add_event_cb(btn_salva, [](lv_event_t *) {
        // Se id=0 il prodotto non è stato caricato: ricarica dal codice banchetto
        if (tagliatubi_manager_get_data()->id == 0 && AppBanchetto::s_tagl_idx != 255) {
            banchetto_data_t bd;
            banchetto_manager_get_item(AppBanchetto::s_tagl_idx, &bd);
            ESP_LOGI("AppBanchetto", "[SALVA] id=0, ricarico prodotto cod=%s", bd.codice_articolo);
            tagliatubi_manager_load_product(bd.codice_articolo);
        }
        const char *ltxt = lv_label_get_text(AppBanchetto::p3_lbl_lunghezza);
        int32_t l = (ltxt && ltxt[0] != '\0') ? atoi(ltxt) : -1;
        int      v = atoi(lv_label_get_text(AppBanchetto::p3_lbl_velocita));
        tagliatubi_manager_set_velocita(v);
        if (l > 0) tagliatubi_manager_set_lunghezza(l);
        if (l > 0) tagliatubi_manager_send_lunghezza();
        tagliatubi_manager_send_quantita();
        tagliatubi_manager_send_velocita();
        ESP_LOGI("AppBanchetto", "[SALVA] L:%ld V:%d id:%d", l, v, tagliatubi_manager_get_data()->id);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(btn_salva, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *ico_R = lv_label_create(btn_salva);
        lv_label_set_text(ico_R, LV_SYMBOL_SAVE);
        lv_obj_set_style_text_font(ico_R, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(ico_R, lv_color_hex(0xFFDD00), 0);
        lv_obj_align(ico_R, LV_ALIGN_LEFT_MID, 40, 0);
        
        lv_obj_t *ico_L = lv_label_create(btn_salva);
        lv_label_set_text(ico_L, LV_SYMBOL_SAVE);
        lv_obj_set_style_text_font(ico_L, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(ico_L, lv_color_hex(0xFFDD00), 0);
        lv_obj_align(ico_L, LV_ALIGN_RIGHT_MID, -40, 0);
        lv_obj_t *lbl = lv_label_create(btn_salva);
        
        lv_label_set_text(lbl, "SALVA PARAMETRI");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_letter_space(lbl, 4, 0);
        lv_obj_center(lbl);
       // lv_obj_t *ico = lv_label_create(btn_salva);
        
    }

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++)
        lv_obj_add_flag(lv_obj_get_child(scr, i), LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ─────────────────────────────────────────────────────────
// UPDATE PAGE 3
// ─────────────────────────────────────────────────────────
void AppBanchetto::update_page3(uint8_t idx)
{
    ESP_LOGI("AppBanchetto", "[DBG] update_page3 chiamata, idx=%d page3_scr=%p", idx, page3_scr);
    if (!page3_scr) return;
    banchetto_data_t d;
    banchetto_manager_get_item(idx, &d);
    ESP_LOGI("AppBanchetto", "[DBG] update_page3 codice=%s banchetto=%s", d.codice_articolo, d.banchetto);

    // Carica dal server solo se il prodotto non è ancora stato caricato
    const tagliatubi_data_t *td = tagliatubi_manager_get_data();
    if (td->id == 0) {
        esp_err_t ret = tagliatubi_manager_load_product(d.codice_articolo);
        if (ret != ESP_OK) {
            ESP_LOGW("AppBanchetto", "tagliatubi_manager_load_product(%s) failed", d.codice_articolo);
            return;
        }
        td = tagliatubi_manager_get_data();
    }

    ESP_LOGI("AppBanchetto", "[DBG page3] id=%d codice=%s desc=%s",
             td->id, td->codice, td->descrizione);
    ESP_LOGI("AppBanchetto", "[DBG page3] lunghezza=%ld diametro=%ld quantita=%ld prodotti=%ld velocita=%d",
             td->lunghezza, td->diametro, td->quantita, td->prodotti, td->velocita);

    // Lunghezza dal DB (vuota se 0 — operatore deve inserirla)
    if (p3_lbl_lunghezza) {
        if (td->lunghezza > 0) {
            char lbuf[16];
            snprintf(lbuf, sizeof(lbuf), "%ld", td->lunghezza);
            lv_label_set_text(p3_lbl_lunghezza, lbuf);
        } else {
            lv_label_set_text(p3_lbl_lunghezza, "");
        }
    }
    // Velocita dal DB
    if (p3_lbl_velocita) {
        char vbuf[8];
        snprintf(vbuf, sizeof(vbuf), "%d", td->velocita);
        lv_label_set_text(p3_lbl_velocita, vbuf);
    }
    tagliatubi_manager_set_quantita((int32_t)d.qta_totale);
    ESP_LOGI("AppBanchetto", "[DBG page3] banchetto qta_totale=%lu", d.qta_totale);

    if (p3_pill)
        lv_obj_set_style_bg_color(p3_pill, d.sessione_aperta ? lv_color_hex(0x16A34A) : lv_color_hex(0xE11D48), 0);
    if (p3_lbl_pill)
        lv_label_set_text(p3_lbl_pill, d.sessione_aperta ? "LOGGATO " LV_SYMBOL_OK : "NON LOGGATO");
}

// ─────────────────────────────────────────────────────────
// CREA PAGE 4 — Ciclo tagliatubi
// ─────────────────────────────────────────────────────────
void AppBanchetto::crea_page4(uint8_t idx)
{
    ESP_LOGI("AppBanchetto", "[crea_page4] idx=%d", idx);
    banchetto_data_t d;
    banchetto_manager_get_item(idx, &d);

    // Cleanup timer precedente se page4 viene ricreata
    if (p4_uomo_morto_timer) {
        lv_timer_del(p4_uomo_morto_timer);
        p4_uomo_morto_timer = nullptr;
    }
    p4_pill_uomo_morto = nullptr;
    p4_lbl_uomo_morto  = nullptr;
    p4_pill_materiale  = nullptr;
    p4_lbl_materiale   = nullptr;
    p4_pill_carter     = nullptr;
    p4_lbl_carter      = nullptr;

    lv_obj_t *scr = lv_obj_create(NULL);
    page4_scr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(scr, 255, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);

    const int SB  = 260;
    const int GAP = 16;
    const int CX  = SB + GAP;
    const int CW  = 1024 - CX - GAP;
    const int H   = 549;

    // ── SIDEBAR SCURA ────────────────────────────────────────────────────────
    lv_obj_t *sidebar = lv_obj_create(scr);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, SB, H);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_bg_opa(sidebar, 255, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 24, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_EVENT_BUBBLE);
    add_versa_switch(sidebar, 255);   // spostato sotto i 3 indicatori sicurezza

    lv_obj_t *ts = lv_label_create(sidebar);
    lv_label_set_text(ts, "STATO");
    lv_obj_set_style_text_font(ts, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ts, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(ts, 4, 0);
    lv_obj_align(ts, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *pill = lv_obj_create(sidebar);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x444444), 0);
    lv_obj_set_style_bg_opa(pill, 255, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 4, 0);
    lv_obj_set_style_pad_top(pill, 6, 0); lv_obj_set_style_pad_bottom(pill, 6, 0);
    lv_obj_set_style_pad_left(pill, 12, 0); lv_obj_set_style_pad_right(pill, 12, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_EVENT_BUBBLE);

    p4_lbl_stato = lv_label_create(pill);
    lv_label_set_text(p4_lbl_stato, "IDLE");
    lv_obj_set_style_text_font(p4_lbl_stato, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(p4_lbl_stato, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_letter_space(p4_lbl_stato, 2, 0);
    lv_obj_align(p4_lbl_stato, LV_ALIGN_CENTER, 0, 0);

    // ── Helper lambda per creare una pill di stato pin ───────────────────────
    auto make_pin_indicator = [&](const char *titolo, int y_lbl,
                                  lv_obj_t **out_pill, lv_obj_t **out_lbl) {
        ESP_LOGI("AppBanchetto", "[p4 pin indicator] crea '%s' y=%d", titolo, y_lbl);

        lv_obj_t *t = lv_label_create(sidebar);
        lv_label_set_text(t, titolo);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_letter_space(t, 2, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, y_lbl);
        lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *p = lv_obj_create(sidebar);
        lv_obj_set_size(p, 150, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(p, lv_color_hex(0xE11D48), 0);
        lv_obj_set_style_bg_opa(p, 255, 0);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_radius(p, 4, 0);
        lv_obj_set_style_pad_top(p, 5, 0);    lv_obj_set_style_pad_bottom(p, 5, 0);
        lv_obj_set_style_pad_left(p, 10, 0);  lv_obj_set_style_pad_right(p, 10, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(p, LV_ALIGN_TOP_LEFT, 0, y_lbl + 22);
        lv_obj_add_flag(p, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text(l, LV_SYMBOL_CLOSE " NO");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

        *out_pill = p;
        *out_lbl  = l;
        ESP_LOGI("AppBanchetto", "[p4 pin indicator] '%s' pill=%p lbl=%p", titolo, p, l);
    };

    // SICUREZZA (uomo morto) y=68, MATERIALE y=128, CARTER y=188
    make_pin_indicator("", 68,  &p4_pill_uomo_morto, &p4_lbl_uomo_morto);
    make_pin_indicator("MATERIALE", 128, &p4_pill_materiale,  &p4_lbl_materiale);
    make_pin_indicator("CARTER",    188, &p4_pill_carter,     &p4_lbl_carter);
    ESP_LOGI("AppBanchetto", "[p4] indicatori pin creati — avvio timer 200ms");

    // Avvia timer polling (200ms) per tutti e 3 i pin
    if (p4_uomo_morto_timer) { lv_timer_del(p4_uomo_morto_timer); }
    p4_uomo_morto_timer = lv_timer_create(p4_uomo_morto_timer_cb, 200, nullptr);
    ESP_LOGI("AppBanchetto", "[p4] timer=%p", p4_uomo_morto_timer);

    lv_obj_t *sep = lv_obj_create(sidebar);
    lv_obj_set_pos(sep, 0, 350);
    lv_obj_set_size(sep, SB - 48, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(sep, 255, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *tp = lv_label_create(sidebar);
    lv_label_set_text(tp, "L ENCODER");
    lv_obj_set_style_text_font(tp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(tp, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(tp, 4, 0);
    lv_obj_align(tp, LV_ALIGN_TOP_LEFT, 0, 358);

    p4_lbl_counter = lv_label_create(sidebar);
    lv_label_set_text(p4_lbl_counter, "");
    lv_obj_set_style_text_font(p4_lbl_counter, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(p4_lbl_counter, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(p4_lbl_counter, LV_ALIGN_TOP_LEFT, 0, 382);


    // ── CARD GIALLA — scatola pezzi/capacità ─────────────────────────────────
    lv_obj_t *card_top = lv_obj_create(scr);
    lv_obj_set_pos(card_top, CX, GAP);
    lv_obj_set_size(card_top, CW, 110);
    lv_obj_set_style_bg_color(card_top, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(card_top, 255, 0);
    lv_obj_set_style_border_width(card_top, 0, 0);
    lv_obj_set_style_radius(card_top, 12, 0);
    lv_obj_set_style_pad_all(card_top, 14, 0);
    lv_obj_clear_flag(card_top, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card_top, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *tit = lv_label_create(card_top);
        lv_label_set_text(tit, "SCATOLA");
        lv_obj_set_style_text_font(tit, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tit, lv_color_hex(0x00000066), 0);
        lv_obj_set_style_text_letter_space(tit, 4, 0);
        lv_obj_align(tit, LV_ALIGN_TOP_LEFT, 0, 0);

        p4_lbl_avanzamento = lv_label_create(card_top);
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu / %lu", d.qta_scatola, d.qta_totale_scatola);
        lv_label_set_text(p4_lbl_avanzamento, buf);
        lv_obj_set_style_text_font(p4_lbl_avanzamento, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(p4_lbl_avanzamento, lv_color_hex(0x000000), 0);
        lv_obj_align(p4_lbl_avanzamento, LV_ALIGN_TOP_LEFT, 0, 26);

    }

    // ── BOTTONI AZIONE: 3 righe ───────────────────────────────────────────────
    int btn_y  = GAP + 110 + GAP;               // y=142
    int btn_h  = (H - btn_y - GAP - 2*GAP) / 3; // ~119px, uguale per tutte e 3 le righe
    int btn_w2 = (CW - GAP) / 2;               // larghezza per 2 bottoni = 358

    auto make_btn = [&](const char *txt, lv_color_t bg, lv_event_cb_t cb, int bx, int by) {
        lv_obj_t *b = lv_btn_create(scr);
        lv_obj_set_pos(b, bx, by);
        lv_obj_set_size(b, btn_w2, btn_h);
        lv_obj_set_style_bg_color(b, bg, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_letter_space(l, 2, 0);
        lv_obj_center(l);
        if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    };

    // Riga 1: CICLO | SINGOLO
    make_btn(LV_SYMBOL_PLAY " CICLO", lv_color_hex(0x16A34A), [](lv_event_t *) {
        tagliatubi_state_t s = tagliatubi_manager_get_state();
        esp_err_t r = tagliatubi_manager_start_ciclo();
        if (r == ESP_OK) ESP_LOGI("AppBanchetto", "[CICLO] avviato OK");
        else ESP_LOGW("AppBanchetto", "[CICLO] rifiutato: %s (stato=%d)", esp_err_to_name(r), (int)s);
    }, CX, btn_y);
    make_btn("SINGOLO", lv_color_hex(0x3B82F6), [](lv_event_t *) {
        tagliatubi_state_t s = tagliatubi_manager_get_state();
        esp_err_t r = tagliatubi_manager_singolo();
        if (r == ESP_OK) ESP_LOGI("AppBanchetto", "[SINGOLO] avviato OK");
        else ESP_LOGW("AppBanchetto", "[SINGOLO] rifiutato: %s (stato=%d)", esp_err_to_name(r), (int)s);
    }, CX + btn_w2 + GAP, btn_y);

    // Riga 2: AVANTI | TAGLIO
    int row2_y = btn_y + btn_h + GAP;
    make_btn("AVANTI", lv_color_hex(0x6366F1), [](lv_event_t *) {
        tagliatubi_state_t s = tagliatubi_manager_get_state();
        esp_err_t r = tagliatubi_manager_avanti();
        if (r == ESP_OK) ESP_LOGI("AppBanchetto", "[AVANTI] avviato OK (stato=%d)", (int)s);
        else ESP_LOGW("AppBanchetto", "[AVANTI] rifiutato: %s (stato=%d)", esp_err_to_name(r), (int)s);
    }, CX, row2_y);
    make_btn(LV_SYMBOL_CUT " TAGLIO", lv_color_hex(0xD97706), [](lv_event_t *) {
        tagliatubi_state_t s = tagliatubi_manager_get_state();
        esp_err_t r = tagliatubi_manager_taglio();
        if (r == ESP_OK) ESP_LOGI("AppBanchetto", "[TAGLIO] avviato OK");
        else ESP_LOGW("AppBanchetto", "[TAGLIO] rifiutato: %s (stato=%d)", esp_err_to_name(r), (int)s);
    }, CX + btn_w2 + GAP, row2_y);

    // Riga 3: STOP (unico, larghezza piena)
    int stop_y = row2_y + btn_h + GAP;
    lv_obj_t *btn_stop = lv_btn_create(scr);
    lv_obj_set_pos(btn_stop, CX, stop_y);
    lv_obj_set_size(btn_stop, CW, btn_h);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_radius(btn_stop, 12, 0);
    lv_obj_set_style_border_width(btn_stop, 0, 0);
    lv_obj_set_style_shadow_width(btn_stop, 0, 0);
    lv_obj_add_event_cb(btn_stop, [](lv_event_t *) { tagliatubi_manager_stop(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(btn_stop, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *ico = lv_label_create(btn_stop);
        lv_label_set_text(ico, LV_SYMBOL_STOP);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(0xE11D48), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 40, 0);
        lv_obj_t *ls = lv_label_create(btn_stop);
        lv_label_set_text(ls, "STOP");
        lv_obj_set_style_text_font(ls, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(ls, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_letter_space(ls, 6, 0);
        lv_obj_center(ls);
    }

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++)
        lv_obj_add_flag(lv_obj_get_child(scr, i), LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ─────────────────────────────────────────────────────────
// REFRESH PAGE 4
// ─────────────────────────────────────────────────────────
void AppBanchetto::add_versa_switch(lv_obj_t *sidebar, int y_offset)
{
    // Label
    lv_obj_t *lbl = lv_label_create(sidebar);
    lv_label_set_text(lbl, "CONTEGGIO");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(lbl, 3, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y_offset);

    // Switch
    lv_obj_t *sw = lv_switch_create(sidebar);
    lv_obj_set_size(sw, 80, 40);
    lv_obj_align_to(sw, lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    if (banchetto_manager_get_versa_abilitato())
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, versa_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (s_versa_sw_count < VERSA_SW_MAX)
        s_versa_switches[s_versa_sw_count++] = sw;
}

void AppBanchetto::update_page4_scatola(void)
{
    if (!p4_lbl_avanzamento || s_tagl_idx == 255) return;
    banchetto_data_t bd;
    banchetto_manager_get_item(s_tagl_idx, &bd);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu / %lu", bd.qta_scatola, bd.qta_totale_scatola);
    lv_label_set_text(p4_lbl_avanzamento, buf);
}

void AppBanchetto::refresh_page4(tagliatubi_state_t state, const tagliatubi_data_t *data)
{
    if (!p4_lbl_counter || !p4_lbl_stato) return;
    char buf[64];
    // L ENCODER — lunghezza rilevata dall'encoder
    if (data->lunghezza_misurata > 0)
        snprintf(buf, sizeof(buf), "%ld mm", data->lunghezza_misurata);
    else
        buf[0] = '\0';
    lv_label_set_text(p4_lbl_counter, buf);
    lv_label_set_text(p4_lbl_stato, tagl_state_label(state));
    lv_obj_set_style_text_color(p4_lbl_stato, tagl_state_color(state), 0);

    if (state == TAGL_STATE_BOX_FULL)
        popup_avviso_open(LV_SYMBOL_WARNING " Scatola piena",
                          "Sostituire la scatola\nquindi riavviare il ciclo.", false);
    else if (state == TAGL_STATE_ERROR_NO_MATERIAL)
        popup_avviso_open(LV_SYMBOL_WARNING " Mancanza materiale",
                          "Controllare presenza materiale\ne riavviare.", false);
    else if (state == TAGL_STATE_ERROR_SAFETY)
        popup_avviso_open(LV_SYMBOL_WARNING " Sportello aperto",
                          "Chiudere lo sportello di protezione\nprima di continuare.", false);
    else if (state == TAGL_STATE_ERROR_UOMO_MORTO)
        popup_avviso_open(LV_SYMBOL_WARNING " Sicurezza",
                          "Tenere premuto il pulsante di sicurezza\nprima di avviare.", false);

    // Avanzamento fase — aggiornato dopo ogni versa
    if (p4_lbl_avanzamento && s_tagl_idx != 255) {
        banchetto_data_t bd;
        banchetto_manager_get_item(s_tagl_idx, &bd);
        snprintf(buf, sizeof(buf), "%lu / %lu", bd.qta_scatola, bd.qta_totale_scatola);
        lv_label_set_text(p4_lbl_avanzamento, buf);
    }
}

// ─────────────────────────────────────────────────────────
// COSTRUTTORE / DISTRUTTORE
// ─────────────────────────────────────────────────────────
AppBanchetto::AppBanchetto() : ESP_Brookesia_PhoneApp("Banchetto", &b2, true),
                               container(nullptr),
                               test_button(nullptr)
{
}

AppBanchetto::~AppBanchetto() {}

bool AppBanchetto::init(void)
{
    ESP_LOGI(TAG, "Init app");
    return true;
}

extern "C" void app_banchetto_update_page1(void)
{
    uint8_t count = banchetto_manager_get_count();
    for (uint8_t i = 0; i < count; i++)
        AppBanchetto::update_page1(i);
}

extern "C" void app_banchetto_update_page2(void)
{
    uint8_t count = banchetto_manager_get_count();
    for (uint8_t i = 0; i < count; i++)
        AppBanchetto::update_page2(i);

    AppBanchetto::update_page4_scatola();
}

extern "C" void add_versa_switch_c(lv_obj_t *sidebar)
{
    AppBanchetto::add_versa_switch(sidebar);
}

// ─────────────────────────────────────────────────────────
// OFFLINE BANNER — timer callback (ogni 2s)
// ─────────────────────────────────────────────────────────
void AppBanchetto::offline_timer_cb(lv_timer_t *t)
{
    if (!offline_banner) return;
    if (wifi_is_connected() && banchetto_is_server_reachable())
        lv_obj_add_flag(offline_banner, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_clear_flag(offline_banner, LV_OBJ_FLAG_HIDDEN);
}

// ═════════════════════════════════════════════════════════
// COLLAUDO — PAGE 5
// Solo per banchetto COLLAUDI_BANCHETTO_ID "222"
// Il login operatore avviene su page1 (flusso banchetto normale).
// Page5 mostra un pannello "scan motore" finché non si scansiona
// il barcode, poi mostra il pannello dati collaudo.
// ═════════════════════════════════════════════════════════

// ─── Colori collaudo ─────────────────────────────────────
#define COLL_CLR_BG         0x1A1A1A
#define COLL_CLR_HEADER     0x2C2C2C
#define COLL_CLR_BAND       0x333333
#define COLL_CLR_COL_BG     0xF5F5F5
#define COLL_CLR_COL_BORDER 0xCCCCCC
#define COLL_CLR_VERDE      0x00CC00
#define COLL_CLR_ROSSO      0xCC0000
#define COLL_CLR_GIALLO     0xFFE000
#define COLL_CLR_TESTO_SCURO 0x111111

static const char *COLL_FASE_NOMI[3] = { "CARICO", "MINIMO", "TOP" };

// ─── Helper: band titolo ─────────────────────────────────
static lv_obj_t *coll_make_band(lv_obj_t *parent, const char *titolo, int y, int w, int h)
{
    lv_obj_t *band = lv_obj_create(parent);
    lv_obj_set_size(band, w, h);
    lv_obj_set_pos(band, 0, y);
    lv_obj_set_style_bg_color(band, lv_color_hex(COLL_CLR_BAND), 0);
    lv_obj_set_style_bg_opa(band, 255, 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_radius(band, 0, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(band, 0, 0);
    lv_obj_t *lbl = lv_label_create(band);
    lv_label_set_text(lbl, titolo);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return band;
}

// ─── Helper: box display istantaneo ──────────────────────
static lv_obj_t *coll_make_display_box(lv_obj_t *parent, int x, int y, int w, int h,
                                        const char *val, lv_color_t col)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(box, 255, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, val);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_set_style_text_font(lbl, &ui_font_my_font75, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return lbl;
}

// ─────────────────────────────────────────────────────────
// CREA PAGE 5 — Collaudo (scan motore + dati)
// ─────────────────────────────────────────────────────────
void AppBanchetto::crea_page5_collaudo(uint8_t idx)
{
    int W = 1024, H = 600;

    lv_obj_t *scr = lv_obj_create(NULL);
    page5_scr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLL_CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, 255, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── PANNELLO SCAN MOTORE (visibile finché motore non scansionato) ─────
    s_coll_scan_panel = lv_obj_create(scr);
    lv_obj_set_size(s_coll_scan_panel, W, H);
    lv_obj_set_pos(s_coll_scan_panel, 0, 0);
    lv_obj_set_style_bg_color(s_coll_scan_panel, lv_color_hex(COLL_CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_coll_scan_panel, 255, 0);
    lv_obj_set_style_border_width(s_coll_scan_panel, 0, 0);
    lv_obj_set_style_pad_all(s_coll_scan_panel, 0, 0);
    lv_obj_clear_flag(s_coll_scan_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(s_coll_scan_panel);
    lv_label_set_text(icon, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x555555), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -40);

    s_coll_scan_lbl = lv_label_create(s_coll_scan_panel);
    lv_label_set_text(s_coll_scan_lbl, LV_SYMBOL_WARNING "  Scansiona il barcode del motore");
    lv_obj_set_style_text_font(s_coll_scan_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_coll_scan_lbl, lv_color_hex(COLL_CLR_GIALLO), 0);
    lv_obj_align(s_coll_scan_lbl, LV_ALIGN_CENTER, 0, 20);

    // ── PANNELLO DATI (nascosto finché motore non scansionato) ───────────
    s_coll_data_panel = lv_obj_create(scr);
    lv_obj_set_size(s_coll_data_panel, W, H);
    lv_obj_set_pos(s_coll_data_panel, 0, 0);
    lv_obj_set_style_bg_color(s_coll_data_panel, lv_color_hex(COLL_CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_coll_data_panel, 255, 0);
    lv_obj_set_style_border_width(s_coll_data_panel, 0, 0);
    lv_obj_set_style_pad_all(s_coll_data_panel, 0, 0);
    lv_obj_clear_flag(s_coll_data_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_coll_data_panel, LV_OBJ_FLAG_HIDDEN);

    int y = 0;

    /* ── HEADER ── */
    int HDR_H = 52;
    lv_obj_t *hdr = lv_obj_create(s_coll_data_panel);
    lv_obj_set_size(hdr, W, HDR_H);
    lv_obj_set_pos(hdr, 0, y);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COLL_CLR_HEADER), 0);
    lv_obj_set_style_bg_opa(hdr, 255, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Bottone NUOVO MOTORE → torna al pannello scan
    lv_obj_t *btn_new = lv_btn_create(hdr);
    lv_obj_set_size(btn_new, 140, 36);
    lv_obj_align(btn_new, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_new, lv_color_hex(COLL_CLR_GIALLO), 0);
    lv_obj_set_style_radius(btn_new, 4, 0);
    lv_obj_set_style_shadow_width(btn_new, 0, 0);
    lv_obj_add_event_cb(btn_new, [](lv_event_t *e) {
        collaudo_manager_reset();
        if (AppBanchetto::s_coll_scan_panel)  lv_obj_clear_flag(AppBanchetto::s_coll_scan_panel, LV_OBJ_FLAG_HIDDEN);
        if (AppBanchetto::s_coll_data_panel)  lv_obj_add_flag(AppBanchetto::s_coll_data_panel,   LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_new = lv_label_create(btn_new);
    lv_label_set_text(lbl_new, LV_SYMBOL_REFRESH " NUOVO");
    lv_obj_set_style_text_color(lbl_new, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(lbl_new, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_new, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *centro = lv_obj_create(hdr);
    lv_obj_set_size(centro, W - 160, HDR_H);
    lv_obj_align(centro, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_opa(centro, 0, 0);
    lv_obj_set_style_border_width(centro, 0, 0);
    lv_obj_set_style_pad_all(centro, 0, 0);
    lv_obj_clear_flag(centro, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_logo = lv_label_create(centro);
    lv_label_set_text(lbl_logo, "COLLAUDO MOTORI");
    lv_obj_set_style_text_font(lbl_logo, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_logo, lv_color_hex(COLL_CLR_ROSSO), 0);
    lv_obj_align(lbl_logo, LV_ALIGN_TOP_MID, 0, 2);

    s_coll_lbl_operatore = lv_label_create(centro);
    lv_label_set_text(s_coll_lbl_operatore, "OPERATORE: ---");
    lv_obj_set_style_text_font(s_coll_lbl_operatore, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_coll_lbl_operatore, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_coll_lbl_operatore, LV_ALIGN_CENTER, 0, 2);

    s_coll_lbl_matricola = lv_label_create(centro);
    lv_label_set_text(s_coll_lbl_matricola, "---");
    lv_obj_set_style_text_font(s_coll_lbl_matricola, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_coll_lbl_matricola, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_coll_lbl_matricola, LV_ALIGN_BOTTOM_MID, 0, -2);

    y += HDR_H;

    /* ── BANDA VALORI ISTANTANEI ── */
    int BAND_H = 22;
    coll_make_band(s_coll_data_panel, "VALORI ISTANTANEI", y, W, BAND_H);
    y += BAND_H;

    /* ── DISPLAY ISTANTANEI ── */
    int DISP_H = 100;
    int disp_w = (W / 2) - 20;

    lv_obj_t *disp_area = lv_obj_create(s_coll_data_panel);
    lv_obj_set_size(disp_area, W, DISP_H);
    lv_obj_set_pos(disp_area, 0, y);
    lv_obj_set_style_bg_color(disp_area, lv_color_hex(COLL_CLR_BG), 0);
    lv_obj_set_style_bg_opa(disp_area, 255, 0);
    lv_obj_set_style_border_width(disp_area, 0, 0);
    lv_obj_set_style_radius(disp_area, 0, 0);
    lv_obj_set_style_pad_all(disp_area, 8, 0);
    lv_obj_clear_flag(disp_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t1 = lv_label_create(disp_area);
    lv_label_set_text(lbl_t1, "CONSUMO [kg/h]");
    lv_obj_set_style_text_color(lbl_t1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_t1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_t1, 10, 2);

    lv_obj_t *lbl_t2 = lv_label_create(disp_area);
    lv_label_set_text(lbl_t2, "GIRI [rpm/min]");
    lv_obj_set_style_text_color(lbl_t2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_t2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_t2, W / 2 + 10, 2);

    s_coll_lbl_consumo_ist = coll_make_display_box(disp_area, 10, 18, disp_w, 72,
                                                    "0.000", lv_color_hex(COLL_CLR_VERDE));
    s_coll_lbl_giri_ist    = coll_make_display_box(disp_area, W / 2 + 10, 18, disp_w, 72,
                                                    "0000", lv_color_hex(COLL_CLR_ROSSO));
    y += DISP_H + 4;

    /* ── 3 COLONNE: CARICO / MINIMO / TOP ── */
    int COL_AREA_H = 180;
    int col_w = (W - 6) / 3;

    for (int col_idx = 0; col_idx < 3; col_idx++) {
        int cx = col_idx * (col_w + 3);
        int cw = col_w;
        int ch = COL_AREA_H;

        lv_obj_t *col = lv_obj_create(s_coll_data_panel);
        lv_obj_set_size(col, cw, ch);
        lv_obj_set_pos(col, cx, y);
        lv_obj_set_style_bg_color(col, lv_color_hex(COLL_CLR_COL_BG), 0);
        lv_obj_set_style_bg_opa(col, 255, 0);
        lv_obj_set_style_border_color(col, lv_color_hex(COLL_CLR_COL_BORDER), 0);
        lv_obj_set_style_border_width(col, 1, 0);
        lv_obj_set_style_radius(col, 0, 0);
        lv_obj_set_style_pad_all(col, 6, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        int row_h = 22;
        int ry = 0;

        lv_obj_t *h1 = lv_label_create(col);
        lv_label_set_text(h1, "CONSUMO [kg/h]");
        lv_obj_set_style_text_font(h1, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(h1, lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
        lv_obj_set_pos(h1, 0, ry);

        lv_obj_t *h2 = lv_label_create(col);
        lv_label_set_text(h2, "GIRI [rpm/min]");
        lv_obj_set_style_text_font(h2, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(h2, lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
        lv_obj_set_pos(h2, (cw - 12) / 2, ry);
        ry += row_h;

        lv_obj_t *sep2 = lv_obj_create(col);
        lv_obj_set_size(sep2, cw - 12, 1);
        lv_obj_set_pos(sep2, 0, ry);
        lv_obj_set_style_bg_color(sep2, lv_color_hex(COLL_CLR_COL_BORDER), 0);
        lv_obj_set_style_bg_opa(sep2, 255, 0);
        lv_obj_set_style_border_width(sep2, 0, 0);
        ry += 4;

        const char *row_labels[3] = { "MIN", "ISTANTANEO", "MAX" };
        lv_obj_t **out_c[3] = {
            &s_coll_lbl_consumo_min[col_idx],
            &s_coll_lbl_consumo_ist_col[col_idx],
            &s_coll_lbl_consumo_max[col_idx]
        };
        lv_obj_t **out_g[3] = {
            &s_coll_lbl_giri_min[col_idx],
            &s_coll_lbl_giri_ist_col[col_idx],
            &s_coll_lbl_giri_max[col_idx]
        };

        for (int r = 0; r < 3; r++) {
            lv_obj_t *rl = lv_label_create(col);
            lv_label_set_text(rl, row_labels[r]);
            lv_obj_set_style_text_font(rl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(rl, lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
            lv_obj_set_width(rl, cw - 12);
            lv_obj_set_style_text_align(rl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(rl, 0, ry);
            ry += row_h - 4;

            lv_obj_t *lc = lv_label_create(col);
            lv_label_set_text(lc, "---");
            lv_obj_set_style_text_font(lc, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lc,
                r == 1 ? lv_color_hex(COLL_CLR_VERDE) : lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
            lv_obj_set_pos(lc, 0, ry);
            *out_c[r] = lc;

            lv_obj_t *lg = lv_label_create(col);
            lv_label_set_text(lg, "----");
            lv_obj_set_style_text_font(lg, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lg,
                r == 1 ? lv_color_hex(COLL_CLR_ROSSO) : lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
            lv_obj_set_pos(lg, (cw - 12) / 2, ry);
            *out_g[r] = lg;

            ry += row_h + 2;
        }

        lv_obj_t *btn = lv_btn_create(col);
        lv_obj_set_size(btn, cw - 12, 36);
        lv_obj_set_pos(btn, 0, ch - 12 - 36 - 6);
        lv_obj_set_style_bg_color(btn,
            col_idx == 2 ? lv_color_hex(COLL_CLR_GIALLO) : lv_color_hex(0xDDDDDD), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        // TODO: collegare callback fase
        lv_obj_t *btn_lbl2 = lv_label_create(btn);
        lv_label_set_text(btn_lbl2, COLL_FASE_NOMI[col_idx]);
        lv_obj_set_style_text_font(btn_lbl2, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(btn_lbl2, lv_color_hex(COLL_CLR_TESTO_SCURO), 0);
        lv_obj_align(btn_lbl2, LV_ALIGN_CENTER, 0, 0);
        s_coll_btn_fase[col_idx] = btn;
    }
    y += COL_AREA_H + 4;

    /* ── TABELLA ULTIMI COLLAUDI ── */
    coll_make_band(s_coll_data_panel, "ULTIMI COLLAUDI", y, W, BAND_H);
    y += BAND_H;

    s_coll_tbl = lv_table_create(s_coll_data_panel);
    lv_obj_set_size(s_coll_tbl, W, H - y);
    lv_obj_set_pos(s_coll_tbl, 0, y);
    lv_obj_set_style_bg_color(s_coll_tbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_coll_tbl, 255, 0);
    lv_table_set_col_cnt(s_coll_tbl, 4);
    lv_table_set_col_width(s_coll_tbl, 0, W / 4);
    lv_table_set_col_width(s_coll_tbl, 1, W / 4);
    lv_table_set_col_width(s_coll_tbl, 2, W / 4);
    lv_table_set_col_width(s_coll_tbl, 3, W / 4);
    lv_table_set_cell_value(s_coll_tbl, 0, 0, "MATRICOLA");
    lv_table_set_cell_value(s_coll_tbl, 0, 1, "CONSUMO");
    lv_table_set_cell_value(s_coll_tbl, 0, 2, "GIRI");
    lv_table_set_cell_value(s_coll_tbl, 0, 3, "ESITO");

    ESP_LOGI(TAG, "Collaudo page5 built for banchetto %s (idx %d)", COLLAUDI_BANCHETTO_ID, idx);
}

// ─────────────────────────────────────────────────────────
// UPDATE dopo motore OK → mostra pannello dati
// ─────────────────────────────────────────────────────────
void AppBanchetto::update_collaudo_badge_ok(void)
{
    // Non usato: il badge viene già gestito dal flusso banchetto su page1
}

void AppBanchetto::update_collaudo_motore_ok(void)
{
    collaudo_motore_t    mot;
    collaudo_manager_get_motore(&mot);

    // Prendi operatore dal banchetto già loggato
    banchetto_data_t d;
    banchetto_manager_get_item(s_coll_idx, &d);

    if (s_coll_lbl_operatore)
        lv_label_set_text_fmt(s_coll_lbl_operatore, "OPERATORE: %s", d.operatore);
    if (s_coll_lbl_matricola)
        lv_label_set_text_fmt(s_coll_lbl_matricola, "%s — MAT: %s", mot.descrizione, mot.matricola);

    // Popola i limiti nelle 3 colonne
    float cmin[3] = { mot.carico_consumo_min, mot.minimo_consumo_min, mot.top_consumo_min };
    float cmax[3] = { mot.carico_consumo_max, mot.minimo_consumo_max, mot.top_consumo_max };
    float gmin[3] = { mot.carico_giri_min,    mot.minimo_giri_min,    mot.top_giri_min    };
    float gmax[3] = { mot.carico_giri_max,    mot.minimo_giri_max,    mot.top_giri_max    };
    for (int i = 0; i < 3; i++) {
        if (s_coll_lbl_consumo_min[i]) lv_label_set_text_fmt(s_coll_lbl_consumo_min[i], "%.2f", cmin[i]);
        if (s_coll_lbl_consumo_max[i]) lv_label_set_text_fmt(s_coll_lbl_consumo_max[i], "%.2f", cmax[i]);
        if (s_coll_lbl_giri_min[i])    lv_label_set_text_fmt(s_coll_lbl_giri_min[i],    "%.0f", gmin[i]);
        if (s_coll_lbl_giri_max[i])    lv_label_set_text_fmt(s_coll_lbl_giri_max[i],    "%.0f", gmax[i]);
    }

    // Mostra pannello dati, nascondi pannello scan
    if (s_coll_scan_panel) lv_obj_add_flag(s_coll_scan_panel,  LV_OBJ_FLAG_HIDDEN);
    if (s_coll_data_panel) lv_obj_clear_flag(s_coll_data_panel, LV_OBJ_FLAG_HIDDEN);
}

// ─── Callbacks FreeRTOS → LVGL ───────────────────────────
extern "C" void collaudo_app_on_badge_ok(void)
{
    // Non usato in questo progetto (badge gestito su page1)
}

extern "C" void collaudo_app_on_motore_ok(void)
{
    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        AppBanchetto::update_collaudo_motore_ok();
        lvgl_port_unlock();
    }
}

extern "C" void collaudo_app_on_error(const char *msg)
{
    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        lv_obj_t *box = lv_obj_create(lv_scr_act());
        lv_obj_set_size(box, 400, 160);
        lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x2A0000), 0);
        lv_obj_set_style_border_color(box, lv_color_hex(COLL_CLR_ROSSO), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 8, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lbl = lv_label_create(box);
        lv_label_set_text_fmt(lbl, LV_SYMBOL_CLOSE "  Errore\n%s", msg);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLL_CLR_ROSSO), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -12);
        lv_obj_t *btn = lv_btn_create(box);
        lv_obj_set_size(btn, 100, 36);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLL_CLR_ROSSO), 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            lv_obj_del(lv_obj_get_parent(lv_event_get_target(e)));
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *btn_lbl = lv_label_create(btn);
        lv_label_set_text(btn_lbl, "OK");
        lv_obj_align(btn_lbl, LV_ALIGN_CENTER, 0, 0);
        lvgl_port_unlock();
    }
}

// ─────────────────────────────────────────────────────────
// SWIPE CALLBACK
// ─────────────────────────────────────────────────────────
void AppBanchetto::swipe_event_cb(lv_event_t *e)
{
    static lv_point_t start = {0, 0};
    static bool pressing = false;

    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSING)
    {
        if (!pressing)
        {
            start = pt;
            pressing = true;
            ESP_LOGI("SWIPE", "DOWN x=%d y=%d", (int)pt.x, (int)pt.y);
        }
        return;
    }

    if (code != LV_EVENT_RELEASED || !pressing)
        return;
    pressing = false;

    int32_t dx = pt.x - start.x;
    int32_t dy = pt.y - start.y;
    int32_t start_x = start.x;
    int32_t start_y = start.y;
    start = {0, 0};
    ESP_LOGI("SWIPE", "UP x=%d y=%d  dx=%d dy=%d", (int)pt.x, (int)pt.y, (int)dx, (int)dy);

    uint8_t idx = banchetto_manager_get_current_index();
    uint8_t count = banchetto_manager_get_count();

    // ── SWIPE ORIZZONTALE — cambia pagina ─────────────────
    if (abs(dx) > abs(dy) && abs(dx) > SWIPE_H_THRESHOLD)
    {
        if (start_y < 16 || start_y > 160) return; // zona swipe: solo CODICE ARTICOLO
        lv_obj_t *cur = lv_scr_act();
        bool on_page1 = (cur == page1_scr[idx]);
        bool on_page2 = (cur == objects[idx].main);
        bool on_page3 = (page3_scr && cur == page3_scr);
        bool on_page4 = (page4_scr && cur == page4_scr);
        bool on_page5 = (page5_scr && cur == page5_scr);

        if (dx < 0)
        {
            // ── Swipe SINISTRA ──
            if (on_page1)
            {
                // page1 → page2
                banchetto_data_t d;
                banchetto_manager_get_data(&d);
                if (!d.sessione_aperta)
                {
                    popup_avviso_open(LV_SYMBOL_WARNING " Timbratura mancante",
                                      "Effettuare il login con\nil badge prima di continuare.",
                                      !wifi_is_connected());
                    return;
                }
                lv_scr_load_anim(objects[idx].main, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                current_scr = objects[idx].main;
            }
            else if (on_page2 && page3_scr)
            {
                // page2 → page3 (solo banchetto 233)
                banchetto_data_t d;
                banchetto_manager_get_item(idx, &d);
                if (strcmp(d.banchetto, TAGL_BANCHETTO_ID) == 0)
                {
                    update_page3(idx);
                    lv_scr_load_anim(page3_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    current_scr = page3_scr;
                }
            }
            else if (on_page2 && page5_scr)
            {
                // page2 → page5 (solo banchetto 222)
                banchetto_data_t d;
                banchetto_manager_get_item(idx, &d);
                if (strcmp(d.banchetto, COLLAUDI_BANCHETTO_ID) == 0)
                {
                    lv_scr_load_anim(page5_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                    current_scr = page5_scr;
                }
            }
            else if (on_page3 && page4_scr)
            {
                // page3 → page4
                lv_scr_load_anim(page4_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
                current_scr = page4_scr;
            }
        }
        else
        {
            // ── Swipe DESTRA ──
            if (on_page2)
            {
                // page2 → page1
                lv_scr_load_anim(page1_scr[idx], LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                current_scr = page1_scr[idx];
            }
            else if (on_page3)
            {
                // page3 → page2 (banchetto 233)
                lv_scr_load_anim(objects[s_tagl_idx].main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                current_scr = objects[s_tagl_idx].main;
            }
            else if (on_page4)
            {
                // page4 → page3
                lv_scr_load_anim(page3_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                current_scr = page3_scr;
            }
            else if (on_page5)
            {
                // page5 → page2 (banchetto 222)
                lv_scr_load_anim(objects[s_coll_idx].main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
                current_scr = objects[s_coll_idx].main;
            }
        }
        return;
    }

    // ── SWIPE VERTICALE — cambia articolo (stessa pagina) ─
    if (abs(dy) > abs(dx) && abs(dy) > SWIPE_V_THRESHOLD)
    {
        if (start_x > 260) return; // zona swipe: solo sidebar sinistra
        if (count <= 1)
            return; // un solo articolo, niente da fare

        bool on_page1 = (lv_scr_act() == page1_scr[idx]);
        int8_t new_idx = (int8_t)idx;

        if (dy < 0)
        {
            // swipe su → articolo successivo
            new_idx++;
            if (new_idx >= (int8_t)count)
                new_idx = 0; // wrap
        }
        else
        {
            // swipe giù → articolo precedente
            new_idx--;
            if (new_idx < 0)
                new_idx = (int8_t)(count - 1); // wrap
        }

        banchetto_manager_set_current_index((uint8_t)new_idx);

        lv_obj_t *dest = on_page1 ? page1_scr[new_idx] : objects[new_idx].main;
        // lv_scr_load_anim_t anim = (dy < 0) ? LV_SCR_LOAD_ANIM_MOVE_TOP : LV_SCR_LOAD_ANIM_MOVE_BOTTOM;
        // Nuovo
        lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_IN;
        lv_scr_load_anim(dest, anim, 300, 0, false);
        current_scr = dest;

        ESP_LOGI(TAG, "Articolo %d → %d (%s)", idx, new_idx, on_page1 ? "page1" : "page2");
    }
}

// ─────────────────────────────────────────────────────────
// CREA PAGE 1
// ─────────────────────────────────────────────────────────
void AppBanchetto::crea_page1(uint8_t idx)
{
    uint8_t count = banchetto_manager_get_count();

    lv_obj_t *scr = lv_obj_create(NULL);
    page1_scr[idx] = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_opa(scr, 255, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);

    // ── SIDEBAR NERA ─────────────────────────────────────────
    lv_obj_t *sidebar = lv_obj_create(scr);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 260, 549);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0x1C1C1C), 0);
    lv_obj_set_style_bg_opa(sidebar, 255, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_radius(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 24, 0);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sidebar, LV_OBJ_FLAG_EVENT_BUBBLE);
    add_versa_switch(sidebar);

    lv_obj_t *lbl_op_tit = lv_label_create(sidebar);
    lv_label_set_text(lbl_op_tit, "OPERATORE");
    lv_obj_set_style_text_font(lbl_op_tit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_op_tit, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(lbl_op_tit, 4, 0);
    lv_obj_align(lbl_op_tit, LV_ALIGN_TOP_LEFT, 0, 0);

    lbl_matricola[idx] = lv_label_create(sidebar);
    lv_label_set_text(lbl_matricola[idx], "");
    lv_obj_set_style_text_font(lbl_matricola[idx], &ui_font_my_font75, 0);
    lv_obj_set_style_text_color(lbl_matricola[idx], lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_matricola[idx], LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t *pill = lv_obj_create(sidebar);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_bg_opa(pill, 255, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_radius(pill, 4, 0);
    lv_obj_set_style_pad_top(pill, 4, 0);
    lv_obj_set_style_pad_bottom(pill, 4, 0);
    lv_obj_set_style_pad_left(pill, 10, 0);
    lv_obj_set_style_pad_right(pill, 10, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(pill, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_EVENT_BUBBLE);

    lbl_sessione_stato[idx] = lv_label_create(pill);
    lv_label_set_text(lbl_sessione_stato[idx], "NON LOGGATO");
    lv_obj_set_style_text_font(lbl_sessione_stato[idx], &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sessione_stato[idx], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(lbl_sessione_stato[idx], 2, 0);
    lv_obj_align(lbl_sessione_stato[idx], LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sep = lv_obj_create(sidebar);
    lv_obj_set_pos(sep, 0, 340);
    lv_obj_set_size(sep, 212, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(sep, 255, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *lbl_banc_tit = lv_label_create(sidebar);
    lv_label_set_text(lbl_banc_tit, "BANCHETTO");
    lv_obj_set_style_text_font(lbl_banc_tit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_banc_tit, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_letter_space(lbl_banc_tit, 3, 0);
    lv_obj_align(lbl_banc_tit, LV_ALIGN_TOP_LEFT, 0, 358);

    lbl_banc[idx] = lv_label_create(sidebar);
    lv_label_set_text(lbl_banc[idx], "");
    lv_obj_set_style_text_font(lbl_banc[idx], &ui_font_my_font75, 0);
    lv_obj_set_style_text_color(lbl_banc[idx], lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_banc[idx], LV_ALIGN_TOP_LEFT, 0, 382);

    // ── CODICE ARTICOLO — hero giallo ─────────────────────────
    lv_obj_t *box_codice = lv_obj_create(scr);
    lv_obj_set_pos(box_codice, 276, 16);
    lv_obj_set_size(box_codice, 732, 120);
    lv_obj_set_style_bg_color(box_codice, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_bg_opa(box_codice, 255, 0);
    lv_obj_set_style_border_width(box_codice, 0, 0);
    lv_obj_set_style_radius(box_codice, 12, 0);
    lv_obj_set_style_pad_left(box_codice, 20, 0);
    lv_obj_set_style_pad_top(box_codice, 12, 0);
    lv_obj_clear_flag(box_codice, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_codice, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_codice);
        lv_label_set_text(t, "CODICE ARTICOLO");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x00000066), 0);
        lv_obj_set_style_text_letter_space(t, 4, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_codice[idx] = lv_label_create(box_codice);
        lv_label_set_text(lbl_codice[idx], "---");
        lv_obj_set_style_text_font(lbl_codice[idx], &ui_font_my_font75, 0);
        lv_obj_set_style_text_color(lbl_codice[idx], lv_color_hex(0x000000), 0);
        lv_obj_align(lbl_codice[idx], LV_ALIGN_TOP_LEFT, 0, 26);
    }
    // ── INDICATORE ARTICOLO N/tot ─────────────────────────────
    lv_obj_t *lbl_idx = lv_label_create(scr);
    lv_label_set_text_fmt(lbl_idx, "%d/%d", idx + 1, count);
    lv_obj_set_style_text_font(lbl_idx, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_idx, lv_color_hex(0x888888), 0);
    lv_obj_align(lbl_idx, LV_ALIGN_TOP_RIGHT, -36, 28);

    // ── CICLO — blu ───────────────────────────────────────────
    lv_obj_t *box_ciclo = lv_obj_create(scr);
    lv_obj_set_pos(box_ciclo, 276, 152);
    lv_obj_set_size(box_ciclo, 200, 110);
    lv_obj_set_style_bg_color(box_ciclo, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_bg_opa(box_ciclo, 255, 0);
    lv_obj_set_style_border_width(box_ciclo, 0, 0);
    lv_obj_set_style_radius(box_ciclo, 12, 0);
    lv_obj_set_style_pad_all(box_ciclo, 14, 0);
    lv_obj_clear_flag(box_ciclo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_ciclo, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_ciclo);
        lv_label_set_text(t, "CICLO");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF99), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_ciclo[idx] = lv_label_create(box_ciclo);
        lv_label_set_text(lbl_ciclo[idx], "---");
        lv_obj_set_style_text_font(lbl_ciclo[idx], &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_ciclo[idx], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_ciclo[idx], LV_ALIGN_TOP_LEFT, 0, 28);
    }

    // ── ORD PRODUZIONE — bianco ───────────────────────────────
    lv_obj_t *box_odp = lv_obj_create(scr);
    lv_obj_set_pos(box_odp, 492, 152);
    lv_obj_set_size(box_odp, 360, 110);
    lv_obj_set_style_bg_color(box_odp, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(box_odp, 255, 0);
    lv_obj_set_style_border_width(box_odp, 0, 0);
    lv_obj_set_style_radius(box_odp, 12, 0);
    lv_obj_set_style_pad_all(box_odp, 14, 0);
    lv_obj_clear_flag(box_odp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_odp, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_odp);
        lv_label_set_text(t, "ORD PRODUZIONE");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x00000066), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_odp[idx] = lv_label_create(box_odp);
        lv_label_set_text(lbl_odp[idx], "");
        lv_obj_set_style_text_font(lbl_odp[idx], &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_odp[idx], lv_color_hex(0x000000), 0);
        lv_obj_align(lbl_odp[idx], LV_ALIGN_TOP_LEFT, 0, 28);
    }

    // ── FASE — rosso ──────────────────────────────────────────
    lv_obj_t *box_fase = lv_obj_create(scr);
    lv_obj_set_pos(box_fase, 868, 152);
    lv_obj_set_size(box_fase, 140, 110);
    lv_obj_set_style_bg_color(box_fase, lv_color_hex(0xE11D48), 0);
    lv_obj_set_style_bg_opa(box_fase, 255, 0);
    lv_obj_set_style_border_width(box_fase, 0, 0);
    lv_obj_set_style_radius(box_fase, 12, 0);
    lv_obj_set_style_pad_all(box_fase, 14, 0);
    lv_obj_clear_flag(box_fase, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_fase, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_fase);
        lv_label_set_text(t, "FASE");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF99), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_fase[idx] = lv_label_create(box_fase);
        lv_label_set_text(lbl_fase[idx], "---");
        lv_obj_set_style_text_font(lbl_fase[idx], &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(lbl_fase[idx], lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(lbl_fase[idx], LV_ALIGN_TOP_LEFT, 0, 28);
    }

    // ── DESCRIZIONE ───────────────────────────────────────────
    lv_obj_t *box_descr = lv_obj_create(scr);
    lv_obj_set_pos(box_descr, 276, 278);
    lv_obj_set_size(box_descr, 732, 130);
    lv_obj_set_style_bg_color(box_descr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(box_descr, 255, 0);
    lv_obj_set_style_border_width(box_descr, 0, 0);
    lv_obj_set_style_radius(box_descr, 12, 0);
    lv_obj_set_style_pad_all(box_descr, 16, 0);
    lv_obj_clear_flag(box_descr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box_descr, LV_OBJ_FLAG_EVENT_BUBBLE);
    {
        lv_obj_t *t = lv_label_create(box_descr);
        lv_label_set_text(t, "DESCRIZIONE ARICOLO");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0x00000044), 0);
        lv_obj_set_style_text_letter_space(t, 3, 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_descr[idx] = lv_label_create(box_descr);
        lv_label_set_text(lbl_descr[idx], "---");
        lv_obj_set_style_text_font(lbl_descr[idx], &lv_font_montserrat_30, 0);
        lv_obj_set_style_text_color(lbl_descr[idx], lv_color_hex(0x000000), 0);
        lv_obj_set_width(lbl_descr[idx], 700);
        lv_label_set_long_mode(lbl_descr[idx], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(lbl_descr[idx], LV_ALIGN_TOP_LEFT, 0, 28);
    }

    // ── BOTTONE CONTROLLO QUALITA' ────────────────────────────
    lv_obj_t *btn_ctrl = lv_btn_create(scr);
    lv_obj_set_pos(btn_ctrl, 276, 424);
    lv_obj_set_size(btn_ctrl, 732, 110);
    lv_obj_set_style_bg_color(btn_ctrl, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_ctrl, lv_color_hex(0x222222), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_ctrl, 12, 0);
    lv_obj_set_style_border_width(btn_ctrl, 0, 0);
    lv_obj_set_style_shadow_width(btn_ctrl, 0, 0);
    lv_obj_add_event_cb(btn_ctrl, [](lv_event_t *e)
                        { popup_controllo_open(); }, LV_EVENT_CLICKED, NULL);
    {
        lv_obj_t *ico = lv_label_create(btn_ctrl);
        lv_label_set_text(ico, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(0xFFDD00), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 40, 0);

        lv_obj_t *lbl = lv_label_create(btn_ctrl);
        lv_label_set_text(lbl, "CONTROLLO QUALITA'");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_letter_space(lbl, 4, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *arrow = lv_label_create(btn_ctrl);
        lv_label_set_text(arrow, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(arrow, lv_color_hex(0xFFDD00), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -40, 0);
    }

    // EVENT_BUBBLE su tutti i figli diretti
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(scr); i++)
        lv_obj_add_flag(lv_obj_get_child(scr, i), LV_OBJ_FLAG_EVENT_BUBBLE);
}

// ─────────────────────────────────────────────────────────
// UPDATE PAGE 1
// ─────────────────────────────────────────────────────────
void AppBanchetto::update_page1(uint8_t idx)
{
    if (!page1_scr[idx])
        return;

    banchetto_data_t d;
    if (!banchetto_manager_get_item(idx, &d))
        return;

    if (lbl_ciclo[idx])
        lv_label_set_text(lbl_ciclo[idx], d.ciclo);
    if (lbl_codice[idx])
        lv_label_set_text(lbl_codice[idx], d.codice_articolo);
    if (lbl_odp[idx])
        lv_label_set_text_fmt(lbl_odp[idx], "%lu", d.ord_prod);
    if (lbl_fase[idx])
        lv_label_set_text(lbl_fase[idx], d.fase);
    if (lbl_descr[idx])
        lv_label_set_text(lbl_descr[idx], d.descrizione_articolo);
    if (lbl_banc[idx])
        lv_label_set_text(lbl_banc[idx], d.banchetto);

    if (d.sessione_aperta)
    {
        if (lbl_matricola[idx])
            lv_label_set_text(lbl_matricola[idx], d.matricola);
        if (lbl_sessione_stato[idx])
        {
            lv_label_set_text(lbl_sessione_stato[idx], "LOGGATO " LV_SYMBOL_OK);
            lv_obj_set_style_bg_color(lv_obj_get_parent(lbl_sessione_stato[idx]),
                                      lv_color_hex(0x16A34A), 0);
        }
    }
    else
    {
        if (lbl_matricola[idx])
            lv_label_set_text(lbl_matricola[idx], "");
        if (lbl_sessione_stato[idx])
        {
            lv_label_set_text(lbl_sessione_stato[idx], "NON LOGGATO");
            lv_obj_set_style_bg_color(lv_obj_get_parent(lbl_sessione_stato[idx]),
                                      lv_color_hex(0xE11D48), 0);
        }
    }
}

// ─────────────────────────────────────────────────────────
// UPDATE PAGE 2
// ─────────────────────────────────────────────────────────
void AppBanchetto::update_page2(uint8_t idx)
{
    if (!objects[idx].main)
        return;

    banchetto_data_t d;
    if (!banchetto_manager_get_item(idx, &d))
        return;

    // Sidebar
    if (d.sessione_aperta)
    {
        if (objects[idx].obj17)
            lv_label_set_text(objects[idx].obj17, d.matricola);
        if (objects[idx].obj19)
            lv_label_set_text(objects[idx].obj19, "LOGGATO " LV_SYMBOL_OK);
        if (objects[idx].obj18)
            lv_obj_set_style_bg_color(objects[idx].obj18, lv_color_hex(0x16A34A), 0);
    }
    else
    {
        if (objects[idx].obj17)
            lv_label_set_text(objects[idx].obj17, "0000");
        if (objects[idx].obj19)
            lv_label_set_text(objects[idx].obj19, "NON LOGGATO");
        if (objects[idx].obj18)
            lv_obj_set_style_bg_color(objects[idx].obj18, lv_color_hex(0xE11D48), 0);
    }

    if (objects[idx].obj13)
        lv_label_set_text(objects[idx].obj13, d.banchetto);

    // Indicatore posizione
    if (objects[idx].obj14)
        lv_label_set_text_fmt(objects[idx].obj14, "%d/%d", idx + 1, banchetto_manager_get_count());

    // Arc avanzamento fase
    if (objects[idx].obj5 && d.qta_totale > 0)
    {
        lv_arc_set_range(objects[idx].obj5, 0, (int16_t)d.qta_totale);
        lv_arc_set_value(objects[idx].obj5, (int16_t)d.qta_prod_fase);
    }
    if (objects[idx].obj7)
    {
        lv_label_set_text_fmt(objects[idx].obj7, "%lu", d.qta_prod_fase);
        lv_obj_align_to(objects[idx].obj7, objects[idx].obj5, LV_ALIGN_CENTER, 0, -12);
    }
    if (objects[idx].obj6)
        lv_label_set_text_fmt(objects[idx].obj6, "/ %lu", d.qta_totale);
    if (objects[idx].obj9)
        lv_label_set_text_fmt(objects[idx].obj9, "%lu", d.qta_prod_sessione);
    if (objects[idx].obj12)
        lv_label_set_text_fmt(objects[idx].obj12, "%lu/%lu",
                              d.qta_scatola, d.qta_totale_scatola);
    if (objects[idx].obj16)
        lv_label_set_text(objects[idx].obj16,
                          d.matr_scatola_corrente[0] ? d.matr_scatola_corrente : "---");

    ESP_LOGI(TAG, "Page2[%d] aggiornata: prod_fase=%lu qta_totale=%lu", idx, d.qta_prod_fase, d.qta_totale);
}

// ─────────────────────────────────────────────────────────
// HELPER
// ─────────────────────────────────────────────────────────
static void check_ordine_e_avvisa(void)
{
    banchetto_data_t d;
    if (banchetto_manager_get_data(&d) && d.ord_prod == 0)
        popup_avviso_open(LV_SYMBOL_WARNING " Nessun ordine",
                          "Nessun ordine attivo.\nTornare alla schermata principale\ne avviare un nuovo ordine.",
                          !wifi_is_connected());
}

// ─────────────────────────────────────────────────────────
// RUN
// ─────────────────────────────────────────────────────────
bool AppBanchetto::run(void)
{
    ESP_LOGI(TAG, "Run app");

    uint8_t count = banchetto_manager_get_count();
    if (count == 0)
        count = 1;

    // Costruisce e popola tutte le coppie di schermate
    create_screens(); // costruisce objects[0..count-1].main

    for (uint8_t i = 0; i < count; i++)
    {
        crea_page1(i);
        update_page1(i);
        update_page2(i);

        // Collega swipe a page2
        lv_obj_add_event_cb(objects[i].main, swipe_event_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(objects[i].main, swipe_event_cb, LV_EVENT_RELEASED, NULL);

        // Collega bottone scarti
        if (objects[i].obj0)
            lv_obj_add_event_cb(objects[i].obj0, [](lv_event_t *e)
                                { tastiera_scarti_open(); }, LV_EVENT_CLICKED, NULL);
    }

    // Costruisce page3/page4 se esiste il banchetto 233
    for (uint8_t i = 0; i < count; i++) {
        banchetto_data_t d;
        banchetto_manager_get_item(i, &d);
        if (strcmp(d.banchetto, TAGL_BANCHETTO_ID) == 0) {
            s_tagl_idx = i;
            crea_page3(i);
            crea_page4(i);
            tagliatubi_manager_set_callback(banchetto_tagl_state_cb);
            // Collega swipe a page3/page4
            lv_obj_add_event_cb(page3_scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
            lv_obj_add_event_cb(page3_scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);
            lv_obj_add_event_cb(page4_scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
            lv_obj_add_event_cb(page4_scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);
            ESP_LOGI(TAG, "Tagliatubi pages built for banchetto %s (idx %d)", TAGL_BANCHETTO_ID, i);
            break;
        }
    }

    // Costruisce page5 se esiste il banchetto 222
    for (uint8_t i = 0; i < count; i++) {
        banchetto_data_t d;
        banchetto_manager_get_item(i, &d);
        if (strcmp(d.banchetto, COLLAUDI_BANCHETTO_ID) == 0) {
            s_coll_idx = i;
            crea_page5_collaudo(i);
            lv_obj_add_event_cb(page5_scr, swipe_event_cb, LV_EVENT_PRESSING, NULL);
            lv_obj_add_event_cb(page5_scr, swipe_event_cb, LV_EVENT_RELEASED, NULL);
            ESP_LOGI(TAG, "Collaudo page built for banchetto %s (idx %d)", COLLAUDI_BANCHETTO_ID, i);
            break;
        }
    }

    // Parte sempre da page1 articolo 0
    banchetto_manager_set_current_index(0);
    lv_disp_load_scr(page1_scr[0]);
    current_scr = page1_scr[0];

    // ── BANNER OFFLINE fisso su lv_layer_top() ────────────────
    offline_banner = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(offline_banner, 820, 360);
    lv_obj_set_size(offline_banner, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(offline_banner, lv_color_hex(0xFF6600), 0);
    lv_obj_set_style_bg_opa(offline_banner, 230, 0);
    lv_obj_set_style_border_width(offline_banner, 0, 0);
    lv_obj_set_style_radius(offline_banner, 6, 0);
    lv_obj_set_style_pad_top(offline_banner, 6, 0);
    lv_obj_set_style_pad_bottom(offline_banner, 6, 0);
    lv_obj_set_style_pad_left(offline_banner, 14, 0);
    lv_obj_set_style_pad_right(offline_banner, 14, 0);
    lv_obj_clear_flag(offline_banner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    {
        lv_obj_t *lbl = lv_label_create(offline_banner);
        lv_label_set_text(lbl, LV_SYMBOL_WARNING " OFFLINE");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_26, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(lbl);
    }
    // stato iniziale
    if (wifi_is_connected())
        lv_obj_add_flag(offline_banner, LV_OBJ_FLAG_HIDDEN);

    offline_timer = lv_timer_create(offline_timer_cb, 2000, NULL);

    check_ordine_e_avvisa();
    ESP_LOGI(TAG, "App loaded — %d articoli, showing page1[0]", count);
    return true;
}

// ─────────────────────────────────────────────────────────
// RESUME
// ─────────────────────────────────────────────────────────
bool AppBanchetto::resume(void)
{
    ESP_LOGI(TAG, "Resume app");
    uint8_t count = banchetto_manager_get_count();
    for (uint8_t i = 0; i < count; i++)
    {
        update_page1(i);
        update_page2(i);
    }
    check_ordine_e_avvisa();
    return true;
}

// ─────────────────────────────────────────────────────────
// BACK
// ─────────────────────────────────────────────────────────
bool AppBanchetto::back(void)
{
    uint8_t idx = banchetto_manager_get_current_index();

    if (lv_scr_act() == objects[idx].main)
    {
        lv_scr_load_anim(page1_scr[idx], LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        current_scr = page1_scr[idx];
        return true;
    }

    // Su page1 → chiudi app, reset puntatori
    for (uint8_t i = 0; i < BANCHETTO_MAX_ITEMS; i++)
    {
        page1_scr[i] = nullptr;
        lbl_matricola[i] = lbl_ciclo[i] = lbl_codice[i] = nullptr;
        lbl_descr[i] = lbl_odp[i] = lbl_fase[i] = nullptr;
        lbl_sessione_stato[i] = lbl_banc[i] = nullptr;
    }
    page3_scr = page4_scr = nullptr;
    p3_lbl_codice = p3_lbl_descr = p3_lbl_lunghezza = nullptr;
    p3_lbl_quantita = p3_lbl_velocita = p3_lbl_pill = p3_pill = nullptr;
    p4_lbl_counter = p4_lbl_stato = p4_lbl_avanzamento = nullptr;
    s_tagl_idx = 255;
    notifyCoreClosed();
    return true;
}

// ─────────────────────────────────────────────────────────
// CLOSE
// ─────────────────────────────────────────────────────────
bool AppBanchetto::close(void)
{
    ESP_LOGI(TAG, "Close app");
    if (offline_timer) { lv_timer_del(offline_timer); offline_timer = nullptr; }
    if (offline_banner) { lv_obj_del(offline_banner); offline_banner = nullptr; }
    for (uint8_t i = 0; i < BANCHETTO_MAX_ITEMS; i++)
    {
        page1_scr[i] = nullptr;
        lbl_matricola[i] = lbl_ciclo[i] = lbl_codice[i] = nullptr;
        lbl_descr[i] = lbl_odp[i] = lbl_fase[i] = nullptr;
        lbl_sessione_stato[i] = lbl_banc[i] = nullptr;
    }
    page3_scr = page4_scr = nullptr;
    p3_lbl_codice = p3_lbl_descr = p3_lbl_lunghezza = nullptr;
    p3_lbl_quantita = p3_lbl_velocita = p3_lbl_pill = p3_pill = nullptr;
    p4_lbl_counter = p4_lbl_stato = p4_lbl_avanzamento = nullptr;
    s_tagl_idx = 255;
    tagliatubi_manager_set_callback(nullptr);
    return true;
}