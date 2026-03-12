#include "offline_journal.h"
#include "http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define JOURNAL_PATH      "/sdcard/offline_journal.jsonl"
#define JOURNAL_TMP_PATH  "/sdcard/offline_journal.tmp"
static const char *TAG = "JOURNAL";

// ─────────────────────────────────────────────────────────────────────────────

void offline_journal_append(const char *json_line)
{
    FILE *f = fopen(JOURNAL_PATH, "a");
    if (!f) { ESP_LOGE(TAG, "Impossibile aprire journal"); return; }
    fprintf(f, "%s\n", json_line);
    fclose(f);
    ESP_LOGI(TAG, "Registrato: %.120s", json_line);
}

// ─────────────────────────────────────────────────────────────────────────────

int offline_journal_count(void)
{
    FILE *f = fopen(JOURNAL_PATH, "r");
    if (!f) return 0;
    int count = 0;
    char line[16];
    while (fgets(line, sizeof(line), f))
        if (strlen(line) > 2) count++;
    fclose(f);
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────

void offline_journal_print_all(void)
{
    int total = offline_journal_count();
    if (total == 0) {
        ESP_LOGI(TAG, "Nessun journal offline su SD");
        return;
    }

    FILE *f = fopen(JOURNAL_PATH, "r");
    if (!f) return;

    ESP_LOGI(TAG, "══════════════════════════════════════════════════");
    ESP_LOGI(TAG, "  JOURNAL OFFLINE — %d operazioni pendenti", total);
    ESP_LOGI(TAG, "══════════════════════════════════════════════════");

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strlen(line) < 5) continue;

        cJSON *j = cJSON_Parse(line);
        if (!j) { ESP_LOGI(TAG, "  [?] riga non valida"); continue; }

        const char *ts   = cJSON_GetStringValue(cJSON_GetObjectItem(j, "ts"))        ?: "--:--:--";
        const char *op   = cJSON_GetStringValue(cJSON_GetObjectItem(j, "op"))        ?: "?";
        const char *banc = cJSON_GetStringValue(cJSON_GetObjectItem(j, "banchetto")) ?: "-";
        const char *mat  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "matricola")) ?: "-";
        int idx          = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "idx"));

        if (strcmp(op, "login") == 0) {
            const char *nome = cJSON_GetStringValue(cJSON_GetObjectItem(j, "nome"))    ?: "";
            const char *cog  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "cognome")) ?: "";
            ESP_LOGI(TAG, "  [%s] LOGIN    banc=%-4s  idx=%d  mat=%-4s  (%s %s)",
                     ts, banc, idx, mat, nome, cog);
        } else if (strcmp(op, "logout") == 0) {
            ESP_LOGI(TAG, "  [%s] LOGOUT   banc=%-4s  idx=%d  mat=%-4s",
                     ts, banc, idx, mat);
        } else if (strcmp(op, "versa") == 0) {
            int qta = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "qta"));
            ESP_LOGI(TAG, "  [%s] VERSA    banc=%-4s  idx=%d  qta=%d",
                     ts, banc, idx, qta);
        } else if (strcmp(op, "scarto") == 0) {
            int qta = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(j, "qta"));
            ESP_LOGI(TAG, "  [%s] SCARTO   banc=%-4s  idx=%d  qta=%d",
                     ts, banc, idx, qta);
        } else if (strcmp(op, "barcode") == 0) {
            const char *bc = cJSON_GetStringValue(cJSON_GetObjectItem(j, "barcode")) ?: "-";
            ESP_LOGI(TAG, "  [%s] BARCODE  banc=%-4s  idx=%d  bc=%s",
                     ts, banc, idx, bc);
        } else {
            ESP_LOGI(TAG, "  [%s] %-8s  banc=%-4s  idx=%d", ts, op, banc, idx);
        }

        cJSON_Delete(j);
    }

    fclose(f);
    ESP_LOGI(TAG, "══════════════════════════════════════════════════");
}

// ─────────────────────────────────────────────────────────────────────────────

int offline_journal_replay(void)
{
    int total = offline_journal_count();
    if (total == 0) {
        ESP_LOGI(TAG, "Nessuna operazione offline da inviare");
        return 0;
    }

    ESP_LOGI(TAG, "Replay journal: %d operazioni pendenti", total);

    FILE *src = fopen(JOURNAL_PATH, "r");
    if (!src) return 0;

    FILE *tmp = fopen(JOURNAL_TMP_PATH, "w");
    if (!tmp) { fclose(src); return 0; }

    int sent = 0;
    bool failed = false;
    char line[1024];

    while (fgets(line, sizeof(line), src)) {
        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strlen(line) < 5) continue;

        if (failed) {
            fprintf(tmp, "%s\n", line);
            continue;
        }

        cJSON *j = cJSON_Parse(line);
        if (!j) { fprintf(tmp, "%s\n", line); continue; }

        const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(j, "url"));
        const char *op  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "op"));

        if (!url || !op) {
            ESP_LOGW(TAG, "Riga journal senza url/op, scartata");
            cJSON_Delete(j);
            continue;
        }

        ESP_LOGI(TAG, "Replay [%s]: %s", op, url);

        int response_code = 0;
        char *body = NULL;
        esp_err_t err = http_get_request(url, &response_code, &body);
        if (body) free(body);

        if (err == ESP_OK && response_code == 200) {
            ESP_LOGI(TAG, "Replay [%s] OK", op);
            sent++;
        } else {
            ESP_LOGW(TAG, "Replay [%s] fallito (code:%d) — fermo qui", op, response_code);
            fprintf(tmp, "%s\n", line);
            failed = true;
        }

        cJSON_Delete(j);
    }

    fclose(src);
    fclose(tmp);

    remove(JOURNAL_PATH);
    rename(JOURNAL_TMP_PATH, JOURNAL_PATH);

    ESP_LOGI(TAG, "Replay completato: %d inviati, %d rimasti", sent, total - sent);
    return sent;
}
