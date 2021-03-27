
typedef struct pandora_station_t {
	char *id;
	char *name;
} pandora_station_t;

typedef struct pandora_track_t {
	char *song_title;
	char *audio_url;
}  pandora_track_t;

typedef struct pandora_t *pandora_handle_t;


pandora_handle_t pandora_init();
esp_err_t pandora_login(pandora_handle_t pandora, char *username, char *password);
esp_err_t pandora_get_stations(pandora_handle_t pandora, const pandora_station_t **stations, size_t *station_count);
esp_err_t pandora_get_playlist(pandora_handle_t pandora, const pandora_station_t* station, char ***urls, size_t *urls_len);
esp_err_t pandora_play_track(pandora_handle_t pandora, const pandora_track_t *track);
esp_err_t pandora_cleanup(pandora_handle_t pandora);
