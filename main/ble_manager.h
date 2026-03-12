#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inizializza lo stack BLE NimBLE e avvia il task di ricezione.
 * Per ora esegue solo uno scan e logga i dispositivi trovati.
 */
void ble_manager_init(void);
const char* banchetto_manager_get_banchetto_id(void);

// Sospende/riprende la scan BLE (usato dal wifi_manager durante scan WiFi)
void ble_manager_pause_scan(void);
void ble_manager_resume_scan(void);
#ifdef __cplusplus
}
#endif