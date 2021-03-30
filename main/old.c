
esp_err_t 
pandora_login(
	pandora_handle_t pandora,
	char *username, 
	char *password)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = {"authToken"} ;
	char* body = NULL;
	const size_t body_max = 256;
	size_t body_len;
	
	CHK(get_non_auth_headers(pandora));

	body = malloc(body_max);
	CHKB(body);
	body_len = snprintf(body, body_max, "{\"existingAuthToken\": null, \"username\": \"%s\", \"password\": \"%s\", \"keepLoggedIn\": true}", 
			 			username, password);
	CHKB(body_len < body_max);

	CHK(http_helper("https://www.pandora.com/api/v1/auth/login", 
					  HTTP_METHOD_POST, 
					  false,
					  pandora->headers, pandora->headers_len,
					  body, body_len,
					  filter_strings, 1, 
					  &results, &results_len, NULL));

	CHKB(results_len >= 1);

	CHK(add_header(pandora, "X-AuthToken", results[0].result));

error:
	free(body);
	http_helper_results_cleanup(results, results_len);
	return err;
}


esp_err_t
pandora_get_playlist_REST(
	pandora_handle_t pandora,
	const pandora_station_t* station,
	char ***urls,
	size_t *urls_len)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = { "\"audioURL\"" };
	char *body;
	const size_t body_max = 256;
	size_t body_len;
	int i;

	body = malloc(body_max);
	CHKB(body);
	body_len = snprintf(body, body_max, 
		     "{ \"stationId\": \"%s\",  \"isStationStart\": false, \"fragmentRequestReason\": \"Normal\", \"audioFormat\": \"aacplus\" }",
		     station->id);
	CHKB(body_len < body_max);

	CHK(http_helper("https://www.pandora.com/api/v1/playlist/getFragment", 
					  HTTP_METHOD_POST, 
					  false,
					  pandora->headers, pandora->headers_len,
					  body, strlen(body),
					  filter_strings, countof(filter_strings), 
					  &results, &results_len, NULL));

	*urls = calloc(results_len, sizeof((*urls)[0]));
	*urls_len = results_len;

	for (i=0; i < results_len; i++) {
		(*urls)[i] = strdup(results[i].result);
	}

error:
	http_helper_results_cleanup(results, results_len);
	free(body);
	return err;
}