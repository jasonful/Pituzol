#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_log.h"

#include "chk_error.h"
#include "crypt.h"
#include "http_helper.h"
#include "pandora_service.h"

static const char *TAG = "PANDORA_SERVICE";

#define countof(x) (sizeof(x)/sizeof(x[0]))
#define PANDORA_HEADERS_MAX (8 * 2)

typedef struct pandora_t {
	pandora_station_t *	stations;
	size_t 				stations_len;
	pandora_track_t *	tracks;
	size_t 				tracks_len;
	const char *		headers[PANDORA_HEADERS_MAX] ;
	size_t				headers_len;
	int 				time_offset;
	char *				user_auth_token;
	char *				partner_auth_token;
	int                 partner_id;
} pandora_t;


pandora_handle_t pandora_init()
{
	pandora_t *pandora = calloc(1, sizeof(*pandora));

    return pandora;
}


static esp_err_t 
add_header(
	pandora_handle_t pandora,
	const char *key,
	const char *value)
{
	esp_err_t err = ESP_OK;

	CHKB(pandora->headers_len < PANDORA_HEADERS_MAX - 1);

	pandora->headers[pandora->headers_len++] = strdup(key);
	pandora->headers[pandora->headers_len++] = strdup(value);
error:
	return err;
}


static esp_err_t 
get_non_auth_headers(
	pandora_handle_t pandora)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	const char* filter_strings[] = { "csrftoken", "Set-Cookie"} ;	
	size_t i = 0, results_len = 0;

	CHK(add_header(pandora, "Content-Type", "application/json"));

	CHK(http_helper("https://www.pandora.com", HTTP_METHOD_HEAD, false,
					  /*headers*/NULL, 0,
					  /*body*/NULL, 0,
					  filter_strings, 2, 
					  &results, &results_len, NULL));
 
	CHKB(results_len >= 1);

	// Mainly we care about csrftoken, but we'll store all the cookies.
	for (i=0; i < results_len; i++) {

		if (0 == results[i].i_filter_string) {
			CHK(add_header(pandora, "X-CsrfToken", results[i].result));
		}
		else if (1 == results[i].i_filter_string) {
			CHK(add_header(pandora, "Cookie", results[i].result));
		}
	}

error:
	http_helper_results_cleanup(results, results_len);
	return err;
}


static int
synctime(
	pandora_handle_t pandora)
{
	assert(sizeof(time_t) == sizeof(int));
	return time(NULL) - pandora->time_offset;
}



esp_err_t 
pandora_user_login(
	pandora_handle_t pandora,
	char *username, 
	char *password)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = {"stat", "userAuthToken", "message"} ;
	char* body = NULL;
	const size_t body_max = 1024;
	size_t body_len;
	char *url;
	const size_t url_max=100;
	size_t url_len;

	url = malloc(url_max);
	CHKB(url);
	url_len = snprintf(url, url_max, "https://tuner.pandora.com/services/json/?method=auth.userLogin&partner_id=%d", pandora->partner_id);
	CHKB(url_len < url_max);

	body = malloc(body_max);
	CHKB(body);
	body_len = snprintf(body, body_max,
		 "{\
		    \"loginType\": \"user\",\
		    \"username\": \"%s\",\
		    \"password\": \"%s\",\
		    \"partnerAuthToken\": \"%s\",\
		    \"includePandoraOneInfo\":false,\
		    \"includeAdAttributes\":false,\
		    \"includeSubscriptionExpiration\":false,\
		    \"includeStationArtUrl\":false,\
		    \"returnStationList\":false,\
		    \"returnGenreStations\":false,\
		    \"syncTime\": %d\
		}", 
			 			username, password, pandora->partner_auth_token, synctime(pandora));

	CHKB(body_len < body_max);

	CHK(http_helper(url, 
				  HTTP_METHOD_POST, 
				  true, // encrypted
				  pandora->headers, pandora->headers_len,
				  body, body_len,
				  filter_strings, countof(filter_strings), 
				  &results, &results_len, NULL));

	if (0 == strcmp(results[0].result, "fail"))
	{
		ESP_LOGI(TAG, "login failed: %s", results[1].result);
		CHKB(false);
	}

	pandora->user_auth_token = strdup(results[1].result);
	ESP_LOGI(TAG, "User auth token = %s", pandora->user_auth_token);


error:
	http_helper_results_cleanup(results, results_len);
	free(body);
	return err;
}


esp_err_t 
pandora_partner_login(
	pandora_handle_t pandora)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = {"syncTime", "partnerAuthToken", "partnerId"} ;
	size_t body_len;
	const char *cryptedTimestamp;
	char *decryptedTimestamp = NULL;
	size_t decryptedSize = 0;

	const char *body = "{ \"username\": \"android\", \"password\": \"AC7IBG09A3DTSYM4R41UJWL07VLN8JI7\", \"deviceModel\": \"android-generic\", \"version\": \"5\"}";
	body_len = strlen(body);

	CHK(http_helper("https://tuner.pandora.com/services/json/?method=auth.partnerLogin", 
					  HTTP_METHOD_POST, 
					  false, // partner login is not encrypted
					  pandora->headers, pandora->headers_len,
					  body, body_len,
					  filter_strings, countof(filter_strings), 
					  &results, &results_len, NULL));

	CHKB(results_len == countof(filter_strings));

	assert(results[0].i_filter_string == 0);
	cryptedTimestamp = results[0].result;
printf("cryptedTimestamp = %s\n", cryptedTimestamp);
	const time_t realTimestamp = time(NULL);

	decryptedTimestamp = BlowfishDecryptString(cryptedTimestamp, &decryptedSize);


printf("decryptedTimestamp = ");
for (int i = 0; i < decryptedSize; i++) {
	printf("%c", decryptedTimestamp[i]);
}
printf("\n");

	if (decryptedTimestamp && decryptedSize > 4) {
		/* skip four bytes garbage(?) at beginning */
		const unsigned long timestamp = strtoul (decryptedTimestamp+4, NULL, 0);
		pandora->time_offset = (long int) realTimestamp - (long int) timestamp;
	}

	pandora->partner_auth_token = strdup(results[1].result);
	pandora->partner_id = atoi(results[2].result);
	ESP_LOGI(TAG, "Partner auth token = %s\nPartner id = %d", pandora->partner_auth_token, pandora->partner_id);
error:
	free (decryptedTimestamp);
	http_helper_results_cleanup(results, results_len);
	return err;
}			




esp_err_t 
pandora_login(
	pandora_handle_t pandora,
	char *username, 
	char *password)
{
	esp_err_t err;

	CHK(get_non_auth_headers(pandora));
	CHK(pandora_partner_login(pandora));
	CHK(pandora_user_login(pandora, username, password));

error:
	return err;
}


#if 0
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
#endif


static void 
stations_cleanup(
	pandora_handle_t pandora)
{
	int i;

	for (i=0; i < pandora->stations_len; i++) {
		free(pandora->stations[i].id);
		pandora->stations[i].id = NULL;
		free(pandora->stations[i].name);
		pandora->stations[i].name = NULL;
	}
	free (pandora->stations);
	pandora->stations = NULL;
	pandora->stations_len = 0;
}


esp_err_t
pandora_get_playlist(
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
		(*urls)[i] = results[i].result;
		ESP_LOGI(TAG, "audioUrl: %s", results[i].result);
	}

error:
	http_helper_results_cleanup(results, results_len);
	free(body);
	return err;
}


esp_err_t 
pandora_get_stations(
	pandora_handle_t pandora, 
	const pandora_station_t **stations, 
	size_t *station_count)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	int i, iStationId= 0, iStationName = 0;
	const char* filter_strings[] = {"\"id\"", "\"name\""};
	const char* body = "{\"query\":\"{ collection(types: [ST]" /*, pagination: {limit: 3}*/ ") { items { id ... on Station { name }}}}\"}";

	CHK(http_helper("https://www.pandora.com/api/v1/graphql/graphql", 
					  HTTP_METHOD_POST, 
					  false,
					  pandora->headers, pandora->headers_len,
					  body, strlen(body),
					  filter_strings, countof(filter_strings), 
					  &results, &results_len, NULL));

	stations_cleanup(pandora);
	pandora->stations_len = results_len / countof(filter_strings);
	pandora->stations = calloc(pandora->stations_len, sizeof(pandora->stations[0]));

	for (i=0; i < results_len; i++) {
		switch (results[i].i_filter_string) {
			case 0: 
				pandora->stations[iStationId++  ].id   = results[i].result + strlen("ST:0:");
				break;
			case 1: 
				pandora->stations[iStationName++].name = results[i].result;
				break;
			default:
				CHKB(0);
		} 
	}
	CHKB(iStationId == iStationName);

	*stations = pandora->stations;
	*station_count = iStationId;

	for (i=0; i < iStationId; i++) {
		ESP_LOGI(TAG, "Station: id = %s name = %s", pandora->stations[i].id, pandora->stations[i].name);
	}

error:
	http_helper_results_cleanup(results, results_len);
	return err;
}


esp_err_t pandora_get_tracks(
	pandora_handle_t pandora,
	const pandora_station_t *station,
	pandora_track_t **tracks, 
	size_t *track_count)
{
	return ESP_FAIL;
}

esp_err_t pandora_play_track(
	pandora_handle_t pandora, 
	const pandora_track_t *track)
{
	return ESP_FAIL;
}




esp_err_t pandora_cleanup(
	pandora_handle_t pandora)
{
	size_t i = 0;

	if (!pandora) {
		return ESP_FAIL;
	}
	for (i=0; i < pandora->headers_len; i++) {
		free((void*)pandora->headers[i]);
	}
	stations_cleanup(pandora);
	free(pandora->partner_auth_token);
	return ESP_FAIL;
}
