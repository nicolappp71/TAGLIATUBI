#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Inizializza lo stack BLE NimBLE e avvia il task di ricezione.
 * Per ora esegue solo uno scan e logga i dispositivi trovati.
 */
void ble_manager_init(void);

#ifdef __cplusplus
}
#endif