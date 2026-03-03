#include "AppBanchetto.hpp"
#include "esp_log.h"
#include "screens.h"
#include "fonts.h"

extern "C"
{
#include "banchetto_manager.h"
#include "tastiera.h"
}
extern "C" void popup_controllo_open(void);
extern "C" void popup_avviso_open(const char *titolo, const char *messaggio);

static const char *TAG = "AppBanchetto";

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
lv_obj_t *AppBanchetto::current_scr = nullptr;

LV_IMG_DECLARE(b2);

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
        }
        return;
    }

    if (code != LV_EVENT_RELEASED || !pressing)
        return;
    pressing = false;

    int32_t dx = pt.x - start.x;
    int32_t dy = pt.y - start.y;
    start = {0, 0};

    uint8_t idx = banchetto_manager_get_current_index();
    uint8_t count = banchetto_manager_get_count();

    // ── SWIPE ORIZZONTALE — cambia pagina (1↔2) ───────────
    if (abs(dx) > abs(dy) && abs(dx) > 80)
    {
        bool on_page1 = (lv_scr_act() == page1_scr[idx]);

        if (dx < 0 && on_page1)
        {
            // page1 → page2
            banchetto_data_t d;
            banchetto_manager_get_data(&d);
            if (!d.sessione_aperta)
            {
                popup_avviso_open(LV_SYMBOL_WARNING " Timbratura mancante",
                                  "Effettuare il login con\nil badge prima di continuare.");
                return;
            }
            lv_scr_load_anim(objects[idx].main, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
            current_scr = objects[idx].main;
        }
        else if (dx > 0 && !on_page1)
        {
            // page2 → page1
            lv_scr_load_anim(page1_scr[idx], LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
            current_scr = page1_scr[idx];
        }
        return;
    }

    // ── SWIPE VERTICALE — cambia articolo (stessa pagina) ─
    if (abs(dy) > abs(dx) && abs(dy) > 80)
    {
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
                          "Nessun ordine attivo.\nTornare alla schermata principale\ne avviare un nuovo ordine.");
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

    // Parte sempre da page1 articolo 0
    banchetto_manager_set_current_index(0);
    lv_disp_load_scr(page1_scr[0]);
    current_scr = page1_scr[0];

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
    notifyCoreClosed();
    return true;
}

// ─────────────────────────────────────────────────────────
// CLOSE
// ─────────────────────────────────────────────────────────
bool AppBanchetto::close(void)
{
    ESP_LOGI(TAG, "Close app");
    for (uint8_t i = 0; i < BANCHETTO_MAX_ITEMS; i++)
    {
        page1_scr[i] = nullptr;
        lbl_matricola[i] = lbl_ciclo[i] = lbl_codice[i] = nullptr;
        lbl_descr[i] = lbl_odp[i] = lbl_fase[i] = nullptr;
        lbl_sessione_stato[i] = lbl_banc[i] = nullptr;
    }
    return true;
}