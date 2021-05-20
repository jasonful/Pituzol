#ifndef _HTTP_HELPER_H
#define _HTTP_HELPER_H

#include "esp_err.h"
#include "esp_http_client.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct http_helper_result_t {
	int i_filter_string;  // index of matching filter_string
	char *result; // free this when done
} http_helper_result_t;

esp_err_t
http_helper(
    const char *url, 
    esp_http_client_method_t http_method,
    bool encrypt_body,
    const char *headers[], 
    size_t headers_len,
    const char *body,
    size_t body_len,
    const char *filter_strings[],
    size_t filter_string_count,
    http_helper_result_t **results,
    size_t *result_count,
    cJSON **cjson);

void http_helper_results_cleanup(http_helper_result_t *results, size_t result_count);

#ifdef __cplusplus
}
#endif

#endif // _HTTP_HELPER_H