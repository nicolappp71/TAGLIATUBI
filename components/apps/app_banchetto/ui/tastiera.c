#include "tastiera.h"
#include "banchetto_manager.h"
#include "screens.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void myBeep(void);

static const char *TAG = "TASTIERA";

#define MAX_DIGITS 4

// ─── Stato tastiera scarti ───────────────────────────────
static lv_obj_t *overlay = NULL;
static lv_obj_t *popup = NULL;
static lv_obj_t *display = NULL;
static char input[MAX_DIGITS + 1] = {0};

// ─── Stato popup controllo ───────────────────────────────
static lv_obj_t *ctrl_overlay = NULL;
static lv_obj_t *ctrl_popup = NULL;
static lv_obj_t *avviso_popup = NULL;
// ─── Forward declarations ─────────────────────────────────
static void tastiera_close(void);
static void aggiorna_display(void);
static void cb_tasto(lv_event_t *e);
static void cb_invia(lv_event_t *e);
static void cb_annulla(lv_event_t *e);
static void cb_backspace(lv_event_t *e);
static void popup_errore_close_cb(lv_event_t *e);
static void mostra_errore_scarto(const char *msg);
static lv_obj_t *crea_tasto(lv_obj_t *parent, const char *label,
                            lv_color_t bg, lv_event_cb_t cb, void *user_data);

// ─────────────────────────────────────────────────────────
// POPUP AVVISO GENERICO
// ─────────────────────────────────────────────────────────
void popup_avviso_open(const char *titolo, const char *msg)
{
    if (avviso_popup)
    {
        lv_obj_del(avviso_popup);
        avviso_popup = NULL;
    }

    lv_obj_t *p = lv_obj_create(lv_layer_top());
    avviso_popup = p;
    lv_obj_set_size(p, 520, 300);
    lv_obj_align(p, LV_ALIGN_CENTER, 10, 30);
    lv_obj_move_foreground(p);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x141E30), 0);
    lv_obj_set_style_bg_opa(p, 255, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0xFA0000), 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_pad_all(p, 16, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(p, 30, 0);
    lv_obj_set_style_shadow_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(p, 200, 0);

    lv_obj_t *tit = lv_label_create(p);
    lv_label_set_text(tit, titolo);
    lv_obj_set_style_text_font(tit, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(tit, lv_color_hex(0xFA0000), 0);
    lv_obj_set_width(tit, LV_PCT(100));
    lv_obj_set_style_text_align(tit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tit, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl = lv_label_create(p);
    lv_label_set_text(lbl, msg);
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
    lv_obj_add_event_cb(btn, popup_errore_close_cb, LV_EVENT_CLICKED, p);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "OK");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);

    myBeep();
}

// ─────────────────────────────────────────────────────────
// POPUP CONTROLLO
// ─────────────────────────────────────────────────────────
void popup_controllo_close(void)
{
    if (ctrl_overlay)
    {
        lv_obj_del(ctrl_overlay);
        ctrl_overlay = NULL;
    }
    if (ctrl_popup)
    {
        lv_obj_del(ctrl_popup);
        ctrl_popup = NULL;
    }
}

static void ctrl_annulla_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Controllo annullato");
    banchetto_manager_set_state(BANCHETTO_STATE_CONTEGGIO); // reset stato
    popup_controllo_close();
}

void popup_controllo_open(void)
{
    if (ctrl_popup != NULL)
        return;

    // Setta lo stato PRIMA di aprire il popup
    banchetto_manager_set_state(BANCHETTO_STATE_CONTROLLO);

    // Overlay bloccante
    ctrl_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ctrl_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ctrl_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctrl_overlay, 160, 0);
    lv_obj_set_style_border_width(ctrl_overlay, 0, 0);
    lv_obj_set_style_radius(ctrl_overlay, 0, 0);
    lv_obj_clear_flag(ctrl_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctrl_overlay, LV_OBJ_FLAG_CLICKABLE);

    // Popup
    ctrl_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ctrl_popup, 520, 260);
    lv_obj_center(ctrl_popup);
    lv_obj_move_foreground(ctrl_popup);
    lv_obj_set_style_bg_color(ctrl_popup, lv_color_hex(0x141E30), 0);
    lv_obj_set_style_bg_opa(ctrl_popup, 255, 0);
    lv_obj_set_style_border_color(ctrl_popup, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_border_width(ctrl_popup, 2, 0);
    lv_obj_set_style_radius(ctrl_popup, 12, 0);
    lv_obj_set_style_pad_all(ctrl_popup, 16, 0);
    lv_obj_clear_flag(ctrl_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(ctrl_popup, 30, 0);
    lv_obj_set_style_shadow_color(ctrl_popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctrl_popup, 200, 0);

    lv_obj_t *tit = lv_label_create(ctrl_popup);
    lv_label_set_text(tit, "CONTROLLO QUALITA'");
    lv_obj_set_style_text_font(tit, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(tit, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_width(tit, LV_PCT(100));
    lv_obj_set_style_text_align(tit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tit, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *lbl = lv_label_create(ctrl_popup);

    lv_label_set_text(lbl, "Avvicina il badge al lettore");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *btn = lv_btn_create(ctrl_popup);
    lv_obj_set_size(btn, 160, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFA0000), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_add_event_cb(btn, ctrl_annulla_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Annulla");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);
}

// ─────────────────────────────────────────────────────────
// TASTIERA SCARTI
// ─────────────────────────────────────────────────────────
void tastiera_scarti_open(void)
{
    if (popup != NULL)
        return;

    memset(input, 0, sizeof(input));

    overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, 160, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(popup, 400, 500);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x141E30), 0);
    lv_obj_set_style_bg_opa(popup, 255, 0);
    lv_obj_set_style_border_color(popup, lv_color_hex(0x4A90E2), 0);
    lv_obj_set_style_border_width(popup, 2, 0);
    lv_obj_set_style_radius(popup, 16, 0);
    lv_obj_set_style_pad_all(popup, 16, 0);
    lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(popup, 40, 0);
    lv_obj_set_style_shadow_color(popup, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(popup, 200, 0);

    lv_obj_t *titolo = lv_label_create(popup);
    lv_label_set_text(titolo, "Inserisci Quantita' Scarti");
    lv_obj_set_style_text_font(titolo, &lv_font_montserrat_30, 0);
    lv_obj_set_style_text_color(titolo, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(titolo, LV_PCT(100));
    lv_obj_set_style_text_align(titolo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(titolo, LV_ALIGN_TOP_MID, 0, 0);

    display = lv_label_create(popup);
    lv_label_set_text(display, "0");
    lv_obj_set_style_text_font(display, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(display, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_style_bg_color(display, lv_color_hex(0x0D1526), 0);
    lv_obj_set_style_bg_opa(display, 255, 0);
    lv_obj_set_style_radius(display, 8, 0);
    lv_obj_set_style_pad_all(display, 10, 0);
    lv_obj_set_style_text_align(display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(display, LV_PCT(100));
    lv_obj_align(display, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *grid = lv_obj_create(popup);
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

    const char *labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++)
    {
        lv_obj_t *btn = crea_tasto(grid, labels[i], lv_color_hex(0x2C5282),
                                   cb_tasto, (void *)(intptr_t)(labels[i][0]));
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % 3, 1,
                             LV_GRID_ALIGN_STRETCH, i / 3, 1);
    }

    lv_obj_t *btn_bs = crea_tasto(grid, LV_SYMBOL_BACKSPACE, lv_color_hex(0xF39C12), cb_backspace, NULL);
    lv_obj_set_grid_cell(btn_bs, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

    lv_obj_t *btn_0 = crea_tasto(grid, "0", lv_color_hex(0x2C5282), cb_tasto, (void *)(intptr_t)'0');
    lv_obj_set_grid_cell(btn_0, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

    lv_obj_t *btn_dummy = lv_obj_create(grid);
    lv_obj_set_style_bg_opa(btn_dummy, 0, 0);
    lv_obj_set_style_border_width(btn_dummy, 0, 0);
    lv_obj_set_grid_cell(btn_dummy, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

    lv_obj_t *btn_annulla = crea_tasto(popup, "Annulla", lv_color_hex(0xFA0000), cb_annulla, NULL);
    lv_obj_set_size(btn_annulla, 160, 55);
    lv_obj_align(btn_annulla, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *btn_invia = crea_tasto(popup, "Invia", lv_color_hex(0x2ECC71), cb_invia, NULL);
    lv_obj_set_size(btn_invia, 160, 55);
    lv_obj_align(btn_invia, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

// ─────────────────────────────────────────────────────────
// CLOSE TASTIERA
// ─────────────────────────────────────────────────────────
static void tastiera_close(void)
{
    if (overlay)
    {
        lv_obj_del(overlay);
        overlay = NULL;
    }
    if (popup)
    {
        lv_obj_del(popup);
        popup = NULL;
    }
    display = NULL;
    memset(input, 0, sizeof(input));
}

// ─────────────────────────────────────────────────────────
// AGGIORNA DISPLAY
// ─────────────────────────────────────────────────────────
static void aggiorna_display(void)
{
    if (!display)
        return;
    lv_label_set_text(display, strlen(input) == 0 ? "0" : input);
}

// ─────────────────────────────────────────────────────────
// CALLBACKS TASTIERA
// ─────────────────────────────────────────────────────────
static void cb_tasto(lv_event_t *e)
{
    char digit = (char)(intptr_t)lv_event_get_user_data(e);
    size_t len = strlen(input);
    if (len >= MAX_DIGITS)
        return;
    if (len == 0 && digit == '0')
        return;
    input[len] = digit;
    input[len + 1] = '\0';
    aggiorna_display();
}

static void cb_backspace(lv_event_t *e)
{
    (void)e;
    size_t len = strlen(input);
    if (len > 0)
        input[len - 1] = '\0';
    aggiorna_display();
}

static void popup_errore_close_cb(lv_event_t *e)
{
    lv_obj_t *p = (lv_obj_t *)lv_event_get_user_data(e);
    if (p)
    {
        lv_obj_del(p);
        if (avviso_popup == p)
            avviso_popup = NULL;
    }
}
static void mostra_errore_scarto(const char *msg)
{
    popup_avviso_open(LV_SYMBOL_WARNING " Scarto non valido", msg);
}

static void cb_invia(lv_event_t *e)
{
    (void)e;
    uint32_t qta = (strlen(input) > 0) ? (uint32_t)atoi(input) : 0;
    if (qta == 0)
    {
        ESP_LOGW(TAG, "Quantita' 0, ignoro");
        return;
    }

    banchetto_data_t dati;
    if (banchetto_manager_get_data(&dati))
    {
        if (qta > dati.qta_prod_sessione && qta > dati.qta_scatola)
        {
            mostra_errore_scarto("Quantita' maggiore della\nsessione e del contenitore");
            return;
        }
        else if (qta > dati.qta_prod_sessione)
        {
            mostra_errore_scarto("Quantita' maggiore\ndella sessione");
            return;
        }
        else if (qta > dati.qta_scatola)
        {
            mostra_errore_scarto("Quantita' maggiore\ndel contenitore");
            return;
        }
    }

    ESP_LOGI(TAG, "Invia scarto: %lu", qta);
    tastiera_close();
    banchetto_manager_scarto(qta);

    // Aggiorna la page2 dell'articolo corrente
    uint8_t idx = banchetto_manager_get_current_index();
    banchetto_data_t aggiornati;
    if (banchetto_manager_get_item(idx, &aggiornati))
    {
        if (objects[idx].obj9)
            lv_label_set_text_fmt(objects[idx].obj9, "%lu", aggiornati.qta_prod_sessione);
        if (objects[idx].obj6)
            lv_label_set_text_fmt(objects[idx].obj6, "/ %lu", aggiornati.qta_totale);
        if (objects[idx].obj12)
            lv_label_set_text_fmt(objects[idx].obj12, "%lu/%lu",
                                  aggiornati.qta_scatola, aggiornati.qta_totale_scatola);
        if (objects[idx].obj5)
        {
            lv_arc_set_range(objects[idx].obj5, 0, (int16_t)aggiornati.qta_totale);
            lv_arc_set_value(objects[idx].obj5, (int16_t)aggiornati.qta_prod_fase);
        }
        if (objects[idx].obj7)
            lv_label_set_text_fmt(objects[idx].obj7, "%lu", aggiornati.qta_prod_fase);
    }
}

static void cb_annulla(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Scarto annullato");
    tastiera_close();
}

// ─────────────────────────────────────────────────────────
// HELPER CREAZIONE TASTO
// ─────────────────────────────────────────────────────────
static lv_obj_t *crea_tasto(lv_obj_t *parent, const char *label,
                            lv_color_t bg, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, 255, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);

    if (cb)
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}