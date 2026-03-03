#include "http_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "mode.h"

// #define CASA // Decommenta quando lavori da casa

// #ifdef CASA
// #define SERVER_BASE "http://192.168.1.58:10000"
// #else
// #define SERVER_BASE "http://intranet.cifarelli.loc"
// #endif

static const char *TAG = "HTTP_CLIENT";

#define MAX_HTTP_RECV_BUFFER 8192
#define HTTP_TIMEOUT_MS 5000

// Buffer per ricevere la risposta
static char response_buffer[MAX_HTTP_RECV_BUFFER];
static int response_len = 0;

/**
 * @brief Event handler per HTTP client
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // Ricevi dati della risposta
        if (response_len + evt->data_len < MAX_HTTP_RECV_BUFFER)
        {
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buffer[response_len] = '\0';
        }
        else
        {
            ESP_LOGW(TAG, "Risposta troppo grande, troncata");
        }
        break;

    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "Header: %s: %s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;

    default:
        break;
    }
    return ESP_OK;
}
esp_err_t http_get_server_time(int *ore, int *minuti)
{
    // Usiamo SERVER_BASE definito in mode.h
    const char *path = "/iot/orario.php";
    char url[256];
    snprintf(url, sizeof(url), "%s%s", SERVER_BASE, path);

    int response_code = 0;
    char *response_body = NULL;

    esp_err_t err = http_get_request(url, &response_code, &response_body);

    if (err == ESP_OK && response_body != NULL)
    {
        // Cerchiamo i due punti ':' nella stringa "16 February 13:56"
        char *p = strchr(response_body, ':');
        if (p != NULL && (p - response_body) >= 2)
        {
            *ore = atoi(p - 2);
            *minuti = atoi(p + 1);
            free(response_body);
            return ESP_OK;
        }
        free(response_body);
    }
    return ESP_FAIL;
}
esp_err_t http_get_request(const char *url, int *response_code, char **response_body)
{
    if (!url || !response_code || !response_body)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🌐 HTTP GET: %s", url);

    // Reset buffer
    response_len = 0;
    memset(response_buffer, 0, MAX_HTTP_RECV_BUFFER);

    // Configura HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "✗ Errore inizializzazione client HTTP");
        return ESP_FAIL;
    }

    // Esegui richiesta GET
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        *response_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "✓ HTTP Status: %d", *response_code);
        ESP_LOGI(TAG, "  Content-Length: %d", content_length);

        // Copia la risposta
        if (response_len > 0)
        {
            *response_body = malloc(response_len + 1);
            if (*response_body != NULL)
            {
                memcpy(*response_body, response_buffer, response_len);
                (*response_body)[response_len] = '\0';
                ESP_LOGI(TAG, "  Response: %s", *response_body);
            }
        }
        else
        {
            *response_body = NULL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "✗ HTTP GET fallito: %s", esp_err_to_name(err));
        *response_code = 0;
        *response_body = NULL;
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t http_post_request(const char *url, const char *post_data, int *response_code, char **response_body)
{
    if (!url || !post_data || !response_code || !response_body)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "🌐 HTTP POST: %s", url);
    ESP_LOGI(TAG, "  Data: %s", post_data);

    // Reset buffer
    response_len = 0;
    memset(response_buffer, 0, MAX_HTTP_RECV_BUFFER);

    // Configura HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "✗ Errore inizializzazione client HTTP");
        return ESP_FAIL;
    }

    // Imposta metodo POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    // Imposta header Content-Type per JSON
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Imposta i dati POST
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Esegui richiesta POST
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        *response_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "✓ HTTP Status: %d", *response_code);
        ESP_LOGI(TAG, "  Content-Length: %d", content_length);

        // Copia la risposta
        if (response_len > 0)
        {
            *response_body = malloc(response_len + 1);
            if (*response_body != NULL)
            {
                memcpy(*response_body, response_buffer, response_len);
                (*response_body)[response_len] = '\0';
                ESP_LOGI(TAG, "  Response: %s", *response_body);
            }
        }
        else
        {
            *response_body = NULL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "✗ HTTP POST fallito: %s", esp_err_to_name(err));
        *response_code = 0;
        *response_body = NULL;
    }

    esp_http_client_cleanup(client);
    return err;
}