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
#define PANDORA_URL "https://tuner.pandora.com/services/json/?"
#define BADADDR ((void*)0xcccccccc)

typedef struct pandora_t {
	const char *		headers[PANDORA_HEADERS_MAX] ;
	size_t				headers_len;
	int 				time_offset;
	char *				user_auth_token;
	unsigned long       user_id;
	char *				partner_auth_token;
	unsigned long       partner_id;
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
					  NULL, 0,
					  NULL, 0,
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
pandora_partner_login(
	pandora_handle_t pandora)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = {"\"syncTime\"", "\"partnerAuthToken\"", "\"partnerId\""} ;
	size_t body_len;
	const char *cryptedTimestamp;
	char *decryptedTimestamp = NULL;
	size_t decryptedSize = 0;

	const char *body = "{ \"username\": \"android\", \"password\": \"AC7IBG09A3DTSYM4R41UJWL07VLN8JI7\", \"deviceModel\": \"android-generic\", \"version\": \"5\"}";
	body_len = strlen(body);

	CHK(http_helper(PANDORA_URL "method=auth.partnerLogin", 
					  HTTP_METHOD_POST, 
					  false, // partner login is not encrypted
					  pandora->headers, pandora->headers_len,
					  body, body_len,
					  filter_strings, countof(filter_strings), 
					  &results, &results_len, NULL));

	CHKB(results_len == countof(filter_strings));

	assert(results[0].i_filter_string == 0);
	cryptedTimestamp = results[0].result;
	const time_t realTimestamp = time(NULL);

	decryptedTimestamp = BlowfishDecryptString(cryptedTimestamp, &decryptedSize);

	if (decryptedTimestamp && decryptedSize > 4) {
		/* skip four bytes garbage(?) at beginning */
		const unsigned long timestamp = strtoul (decryptedTimestamp+4, NULL, 0);
		pandora->time_offset = (long int) realTimestamp - (long int) timestamp;
	}

	pandora->partner_auth_token = strdup(results[1].result);
	pandora->partner_id = strtoul(results[2].result, NULL, 10);
	ESP_LOGI(TAG, "Partner auth token = %s\nPartner id = %lu", pandora->partner_auth_token, pandora->partner_id);
error:
	free (decryptedTimestamp);
	http_helper_results_cleanup(results, results_len);
	return err;
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
	const char* filter_strings[] = {"\"stat\"", "\"userAuthToken\"", "\"userId\"", "\"message\""} ;
	char* body = NULL;
	const size_t body_max = 1024; 
	size_t body_len;
	char *url;
	const size_t url_max=200;
	size_t url_len;

	url = malloc(url_max);
	CHKB(url);
	url_len = snprintf(url, url_max, 
						PANDORA_URL "method=auth.userLogin&auth_token=%s&partner_id=%lu",
						pandora->partner_auth_token, pandora->partner_id);
	CHKB(url_len < url_max);

	body = malloc(body_max);
	CHKB(body);
	body_len = snprintf(body, body_max,
				 "{ \"loginType\": \"user\", \"username\": \"%s\", \"password\": \"%s\", \"partnerAuthToken\": \"%s\", \"syncTime\": %d }", 
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
	CHK(add_header(pandora, "X-AuthToken", pandora->user_auth_token));

	pandora->user_id = strtoul(results[2].result, NULL, 10);

error:
	http_helper_results_cleanup(results, results_len);
	free(url);
	free(body);
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




esp_err_t pandora_get_tracks(
	pandora_handle_t pandora,
	const pandora_station_t *station,
	pandora_track_t **tracks, 
	size_t *tracks_len)
{
	esp_err_t err;
	http_helper_result_t *results = NULL;
	size_t results_len = 0;
	const char* filter_strings[] = {"\"songName\"", "\"artistName\"", "\"additionalAudioUrl\"", "\"message\""} ;
	char* body = NULL;
	const size_t body_max = 512; 
	size_t body_len;
	char *url;
	const size_t url_max=200;
	size_t url_len;
	int i, i_song= 0, i_artist = 0, i_url = 0;

	url = malloc(url_max);
	CHKB(url);
	url_len = snprintf(url, url_max, 
						PANDORA_URL "method=station.getPlaylist&auth_token=%s&partner_id=%lu&user_id=%lu",
						pandora->user_auth_token, pandora->partner_id, pandora->user_id);
	CHKB(url_len < url_max);

	body = malloc(body_max);
	CHKB(body);
	body_len = snprintf(body, body_max,
				"{\"userAuthToken\": \"%s\", \"additionalAudioUrl\": \"HTTP_128_MP3\", \"syncTime\": %d, \"stationToken\": \"%s\"}",
				pandora->user_auth_token, synctime(pandora), station->token);
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

	*tracks_len = results_len / (countof(filter_strings) - 1); // subtract 1 for "message"
	*tracks = calloc(*tracks_len, sizeof(**tracks));

	for (i=0; i < results_len; i++) {
		switch (results[i].i_filter_string) {
			case 0: 
				(*tracks)[i_song++].song = strdup(results[i].result);
				break;
			case 1: 
				(*tracks)[i_artist++].artist = strdup(results[i].result);
				break;
			case 2: 
				(*tracks)[i_url++].audio_url = strdup(results[i].result);
				break;
			default:
				CHKB(0);
		} 
	}
	CHKB(i_song == i_artist  &&  i_song == i_url);

    if (0 == strcmp((*tracks)[0].song, "Multiple Streams")) {
    	(*tracks_len)--;
    	memmove((*tracks), (*tracks) + 1, sizeof(**tracks) * (*tracks_len));
    }

error:
	http_helper_results_cleanup(results, results_len);
	free(url);
	free(body);
	return err;
}



esp_err_t 
pandora_get_stations(
	pandora_handle_t pandora, 
	pandora_station_t **stations, 
	size_t *stations_len)
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

	*stations_len = results_len / countof(filter_strings);
	*stations = calloc(*stations_len, sizeof((*stations)[0]));

	for (i=0; i < results_len; i++) {
		switch (results[i].i_filter_string) {
			case 0: 
				(*stations)[iStationId++  ].token = strdup(results[i].result + strlen("ST:0:"));
				break;
			case 1: 
				(*stations)[iStationName++].name = strdup(results[i].result);
				break;
			default:
				CHKB(0);
		} 
	}
	CHKB(iStationId == iStationName);

error:
	http_helper_results_cleanup(results, results_len);
	return err;
}


esp_err_t
pandora_playback_paused(
	pandora_handle_t pandora)
{
	esp_err_t err;
	char *body = "{ \"sync\": false }";

	CHK(http_helper("https://www.pandora.com/api/v1/station/playbackPaused", 
					  HTTP_METHOD_POST, 
					  false,
					  pandora->headers, pandora->headers_len,
					  body, strlen(body),
					  NULL, 0, 
					  NULL, NULL, NULL));
error:
	return err;
}


//////// Cleanup functions:

void 
pandora_stations_cleanup(
	pandora_station_t *stations,
	size_t stations_len)
{
	int i;

	for (i=0; i < stations_len; i++) {
		free(stations[i].token);
		stations[i].token = BADADDR;
		free(stations[i].name);
		stations[i].name = BADADDR;
	}
	free (stations);
}


void 
pandora_tracks_cleanup(
	pandora_track_t *tracks,
	size_t tracks_len)
{
	int i;

	for (i=0; i < tracks_len; i++) {
		free(tracks[i].song);
		tracks[i].song = BADADDR;
		free(tracks[i].artist);
		tracks[i].artist = BADADDR;
		free(tracks[i].audio_url);
		tracks[i].audio_url = BADADDR;
	}
	free (tracks);
}


void 
pandora_cleanup(
	pandora_handle_t pandora)
{
	size_t i = 0;

	if (!pandora) {
		return;
	}
	for (i=0; i < pandora->headers_len; i++) {
		free((void*)pandora->headers[i]);
	}
	free(pandora->partner_auth_token);
}