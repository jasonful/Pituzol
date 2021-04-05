
typedef struct pandora_station_t {
	char *token;
	char *name;
} pandora_station_t;

typedef struct pandora_track_t {
	char *song;
	char *artist;
	char *audio_url;
}  pandora_track_t;

typedef struct pandora_t *pandora_handle_t;
typedef struct pandora_helper_t *pandora_helper_handle_t;


// Pandora APIs
pandora_handle_t pandora_init();
esp_err_t pandora_login(pandora_handle_t pandora, char *username, char *password);
esp_err_t pandora_get_stations(pandora_handle_t pandora, pandora_station_t **stations, size_t *stations_len);
esp_err_t pandora_get_tracks(pandora_handle_t pandora, const pandora_station_t *station, pandora_track_t **tracks, size_t *track_count);
esp_err_t pandora_playback_paused(pandora_handle_t pandora);

void pandora_stations_cleanup(pandora_station_t *stations, size_t stations_len);
void pandora_tracks_cleanup(pandora_track_t *tracks, size_t tracks_len);
void pandora_cleanup(pandora_handle_t pandora);

// Helper APIs
pandora_helper_handle_t pandora_helper_init(const char *username, const char *password);
esp_err_t pandora_helper_get_next_track(pandora_helper_handle_t helper, char **url);
void pandora_helper_cleanup(pandora_helper_handle_t helper);


