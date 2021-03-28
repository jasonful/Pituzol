#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "cJSON.h" 
#include "esp_http_client.h"
#include "chk_error.h"
#include "crypt.h"
#include "http_helper.h"

static const char *TAG = "HTTP_HELPER";

typedef struct http_helper_user_data_t {
    const char **filter_strings;
    size_t filter_string_count;
    http_helper_result_t **results;
    size_t *result_count;
    char *data;
    size_t data_len;
    cJSON **cjson;
} http_helper_user_data_t;



static void 
add_result(
        http_helper_user_data_t *u, // where to add the result
        int i,                      // filter_string index
        const char *start,          // start of found string
        const char *end)            // past end of found string
{
    http_helper_result_t *results;
    size_t count = ++(*u->result_count);
    size_t length;
    char *r;
    
    // Add one to the array
    *u->results = realloc(*u->results, count * sizeof(**u->results));
    results = *u->results;

    if (!results) {
        ESP_LOGE (TAG, "realloc failed in add_result");
        return;
    }

    // Fill the new array element
    results[count-1].i_filter_string = i;

    length = end - start;
    r = malloc(length + 1);

    if (!r) {
        ESP_LOGE (TAG, "alloc failed in add_result");
        return;
    }
    results[count-1].result = r; 
    strncpy(r, start, length);
    r[length] = '\0';
    ESP_LOGI(TAG, "Added result %s", r);
}


esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{
    http_helper_user_data_t *u;
    char *found_filter;
    char *start;
    char *end;
    char *start_search;
    int i;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            //ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
            //ESP_LOGI(TAG, "Key: %s", evt->header_key);
            //ESP_LOGI(TAG, "Value: %s", evt->header_value);
            printf("%.*s", evt->data_len, (char*)evt->data);
            u = (http_helper_user_data_t *)evt->user_data;

            for (i=0; i < u->filter_string_count; i++) {

                // First, look in the Header key
                if (0 == strcmp(u->filter_strings[i], evt->header_key)) {
                    add_result(u, i, evt->header_value, evt->header_value + strlen(evt->header_value));
                }

                // Then, look in the Header value
                found_filter = strstr(evt->header_value, u->filter_strings[i]);
                if (found_filter) {
                    start = found_filter + strlen(u->filter_strings[i]) + 1; // skip "filter="
                    end = start;
                    while (*end != '\0' && *end != ';') {
                        end++;
                    }
                    add_result(u, i, start, end);
                }
            }
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }
            u = (http_helper_user_data_t *)evt->user_data;
            u->data = realloc(u->data, u->data_len + evt->data_len);
            memcpy(u->data + u->data_len, (char*)evt->data, evt->data_len);
            u->data_len += evt->data_len;

            break;

        case HTTP_EVENT_ON_FINISH:
            u = (http_helper_user_data_t *)evt->user_data;
            //ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            //printf("%.*s", u->data_len, u->data);

            if (u->cjson) {
                *(u->cjson) = cJSON_Parse(u->data);
            }
 
            for (i=0; i < u->filter_string_count; i++) {
                // Look for all occurences of u->filter_string[i]
                // and add a result for each, assuming JSON format.
                start_search = u->data;
                do {
                    found_filter = strnstr(start_search, u->filter_strings[i], u->data_len - (start_search - u->data));
                    if (found_filter) {
                        start = found_filter + strlen(u->filter_strings[i]);
                        while (*start == '"' || *start == ':' || *start == ' ') {
                            start++;
                        } 
                        end = start + 1;
                        while (*end != '"' && 
                               end < u->data + u->data_len) {
                            end++;
                        }
                        start_search = end;
                        add_result(u, i, start, end);
                    }
                } while (found_filter && 
                         start_search < u->data + u->data_len);
            }
            free (u->data);
            u->data = NULL;
            u->data_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:
            //ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}


esp_err_t
http_helper(
    const char *url, 
    esp_http_client_method_t http_method,
    bool encrypted,
    const char *headers[], 
    size_t headers_len,
    const char *body,
    size_t body_len,
    const char *filter_strings[],
    size_t filter_string_count,
    http_helper_result_t **results,
    size_t *result_count,
    cJSON **cjson)
{
    esp_err_t err = ESP_OK;
    int i = 0;
    char *encrypted_body = NULL;
    
    ESP_LOGI(TAG, "Entering http_helper CONFIG_LOG_DEFAULT_LEVEL=%08x,  LOG_LOCAL_LEVEL=%08x", CONFIG_LOG_DEFAULT_LEVEL, LOG_LOCAL_LEVEL);
    ESP_LOGI(TAG, "url= %s", url);
    if (headers) {
        while (i < headers_len) {
            ESP_LOGI(TAG, "Header: Key=%s Value=%s", headers[i], headers[i+1]);
            i += 2;
        }
    }
    for (i = 0; i < filter_string_count; i++)
    {
        //ESP_LOGI(TAG, "Filter string: %s", filter_strings[i]);
    }

    http_helper_user_data_t user_data = {
        .filter_strings = filter_strings,
        .filter_string_count = filter_string_count,
        .results = results,
        .result_count = result_count,
        .cjson = cjson,
    };

    if (results) {
        *results = NULL;
        *result_count = 0;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = http_method,
        .event_handler = _http_event_handle,
        .user_data = &user_data,
        .skip_cert_common_name_check = true,
        .buffer_size_tx = DEFAULT_HTTP_BUF_SIZE * 2,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    CHKB(client);

    if (headers)
    {
        i = 0;
        while (i < headers_len) {
            esp_http_client_set_header(client, headers[i], headers[i+1]);
            i += 2;
        }
    }

    if (body) {
        if (encrypted) {
            encrypted_body = BlowfishEncryptString(body);
            esp_http_client_set_post_field(client, encrypted_body, strlen(encrypted_body));
        } else {
            esp_http_client_set_post_field(client, body, body_len ? body_len : strlen(body));
        }
        ESP_LOGI(TAG, "Body: %s", body);
    }

    err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        //ESP_LOGI(TAG, "Status = %d, content_length = %d", esp_http_client_get_status_code(client), esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "perform failed %08x", err);
    }

error:
    if (client) {
        esp_http_client_cleanup(client);
    }
    free(encrypted_body);
    return err;
}


void 
http_helper_results_cleanup(
    http_helper_result_t *results, 
    size_t result_count)
{
    for (int i = 0; i < result_count; i++) {
        free(results[i].result);
    }
     free(results);
}