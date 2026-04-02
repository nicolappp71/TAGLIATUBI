#ifndef COLLAUDO_MANAGER_H
#define COLLAUDO_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════
// STATE MACHINE
// ═══════════════════════════════════════════════════════════
typedef enum {
    COLLAUDO_STATE_CHECKIN,       // attesa badge operatore
    COLLAUDO_STATE_SCAN_MOTORE,   // operatore ok, attesa barcode motore
    COLLAUDO_STATE_IN_CORSO,      // parametri caricati, collaudo attivo
} collaudo_state_t;

// ═══════════════════════════════════════════════════════════
// STRUTTURA DATI MOTORE
// ═══════════════════════════════════════════════════════════
typedef struct {
    char  codice_tipo[4];         // 3 cifre tipo (es. "999")
    char  matricola[8];           // 7 cifre matricola (es. "1234567")
    char  descrizione[64];        // DescrizioneModello dal DB

    float carico_consumo_min;
    float carico_consumo_max;
    float carico_giri_min;
    float carico_giri_max;

    float minimo_consumo_min;
    float minimo_consumo_max;
    float minimo_giri_min;
    float minimo_giri_max;

    float top_consumo_min;
    float top_consumo_max;
    float top_giri_min;
    float top_giri_max;
} collaudo_motore_t;

// ═══════════════════════════════════════════════════════════
// STRUTTURA DATI OPERATORE
// ═══════════════════════════════════════════════════════════
typedef struct {
    char badge[16];
    char nome[64];
} collaudo_operatore_t;

// ═══════════════════════════════════════════════════════════
// FUNZIONI PUBBLICHE
// ═══════════════════════════════════════════════════════════

void             collaudo_manager_init(void);
collaudo_state_t collaudo_manager_get_state(void);
void             collaudo_manager_set_state(collaudo_state_t state);

// Badge operatore — PLACEHOLDER (endpoint non ancora disponibile)
esp_err_t        collaudo_manager_badge_in(const char *badge);

// Barcode motore → chiama collaudoDataIn.php
esp_err_t        collaudo_manager_scan_barcode(const char *barcode);

// Accesso dati
bool             collaudo_manager_get_motore(collaudo_motore_t *out);
bool             collaudo_manager_get_operatore(collaudo_operatore_t *out);

// Reset sessione
void             collaudo_manager_reset(void);

#ifdef __cplusplus
}
#endif

#endif // COLLAUDO_MANAGER_H
