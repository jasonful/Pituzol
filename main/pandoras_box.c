/* Pandora's Box

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "aac_decoder.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"

#if __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#else
#define ESP_IDF_VERSION_VAL(major, minor, patch) 1
#endif

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
#include "esp_netif.h"
#else
#include "tcpip_adapter.h"
#endif

#include "chk_error.h"
#include "pandora_service.h"
#include "gui.h"

static const char *TAG = "PANDORAS BOX";

#define PANDORA_USE_DAC 0
#define PANDORA_MP3_DECODER

#if 0
esp_err_t
test_pandora(
    char ***urls,
    size_t *urls_len)
{
    esp_err_t err;
    pandora_station_t *stations = NULL;
    size_t stations_len = 0;
    pandora_track_t *tracks = NULL;
    size_t tracks_len = 0;
    int i_track = 0;

    pandora_handle_t pandora = pandora_init();
    if (!pandora)
    {
        ESP_LOGE(TAG, "pandora_init failed");
        err = ESP_FAIL;
        goto error;
    }

    err = pandora_login(pandora, CONFIG_PANDORA_USERNAME, CONFIG_PANDORA_PASSWORD);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "pandora_login failed");
        goto error;
    }

    CHK(pandora_get_stations(pandora, &stations, &stations_len));

    for (int i=0; i < stations_len; i++) {
        printf ("Station %d: name = %s token=%s\n", i, stations[i].name, stations[i].token);
    }

    CHK(pandora_get_tracks(pandora, stations, &tracks, &tracks_len));
    for (int i = 0; i < tracks_len; i++) {
        printf ("Track %d: song = %s artist = %s audio_url = %s\n", i, tracks[i].song, tracks[i].artist, tracks[i].audio_url);
    }

    *urls_len = 1;
    *urls = calloc(*urls_len, sizeof(**urls));
    **urls = strdup(tracks[i_track].audio_url);

error:
    pandora_stations_cleanup(stations, stations_len);
    pandora_tracks_cleanup(tracks, tracks_len);
    return err;
}
#endif


static void 
setup_gui(
    pandora_helper_handle_t pandora_helper)
{
    pandora_station_t *stations;
    size_t stations_len = 0;
    size_t options_len = 1;
    size_t i;
    char *options;

    if (ESP_OK != pandora_helper_get_stations(pandora_helper, &stations, &stations_len))
    {
        return;
    }

    // Concatenate all the station names into on string, delimited by newlines
    for (i=0; i < stations_len; i++) {
        options_len += strlen(stations[i].name) + 1;
    }

    options = calloc(1, options_len);

    for (i=0; i < stations_len; i++) {
        strcat(options, stations[i].name);
        if (i < stations_len - 1) {
            strcat(options,"\n");
        }
    }

    gui_init(options);

    // Do not free options here because ownership gets transferred to a different thread.
}


void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
    ESP_ERROR_CHECK(esp_netif_init());
#else
    tcpip_adapter_init();
#endif

    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader, i2s_stream_writer;
    pandora_helper_handle_t pandora_helper = NULL;
    char *audio_url = NULL;


 #if !PANDORA_USE_DAC
    ESP_LOGI(TAG, "[ 1 ] Start audio codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
#endif
    
    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_stream_reader = http_stream_init(&http_cfg);

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
#if PANDORA_USE_DAC
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_INTERNAL_DAC_CFG_DEFAULT();
#else
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
#endif
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

#ifdef PANDORA_MP3_DECODER
    ESP_LOGI(TAG, "[2.3] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    audio_element_handle_t mp3_decoder = mp3_decoder_init(&mp3_cfg);
#endif

#ifdef PANDORA_AAC_DECODER
    aac_decoder_cfg_t aac_dec_cfg  = DEFAULT_AAC_DECODER_CONFIG();
    audio_element_handle_t aac_decoder = aac_decoder_init(&aac_dec_cfg);
    audio_element_set_tag(aac_decoder, "m4a");
    //esp_audio_codec_lib_add(player, AUDIO_CODEC_TYPE_DECODER, aac_decoder_init(&aac_dec_cfg));
#endif 
    
    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
#ifdef PANDORA_AAC_DECODER
    audio_pipeline_register(pipeline, aac_decoder,        "m4a");
#endif
#ifdef PANDORA_MP3_DECODER
    audio_pipeline_register(pipeline, mp3_decoder,        "mp3");
#endif
    audio_pipeline_register(pipeline, i2s_stream_writer,  "i2s");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->decoder-->i2s_stream-->[codec_chip]");
#ifdef PANDORA_AAC_DECODER
    const char *link_tag[3] = {"http", "m4a", "i2s"};
#endif
#ifdef PANDORA_MP3_DECODER
    const char *link_tag[3] = {"http", "mp3", "i2s"};
#endif
    audio_pipeline_link(pipeline, &link_tag[0], 3);
  
    ESP_LOGI(TAG, "[ 3 ] Start and wait for Wi-Fi network");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    esp_periph_handle_t wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
    ESP_LOGI(TAG, "is Connected = %08x", periph_wifi_is_connected(wifi_handle));

#if 0
    char **audio_urls = NULL;
    size_t audio_urls_len = 0;
    err = test_pandora(&audio_urls, &audio_urls_len);
    ESP_LOGI(TAG, "test_pandora returned %08x", err);

    CHK(err);
    CHKB(audio_urls_len);

    if (audio_urls_len > 0) {
        for (int i = 0; i < audio_urls_len; i++) {
            ESP_LOGI(TAG, "[%d] = %s", i, audio_urls[i]);
        }
        audio_element_set_uri(http_stream_reader, audio_urls[0]);
        // TODO: Free urls
    }
#endif
    pandora_helper = pandora_helper_init(CONFIG_PANDORA_USERNAME, CONFIG_PANDORA_PASSWORD);
    CHK(pandora_helper_get_next_track(pandora_helper, &audio_url));
    audio_element_set_uri(http_stream_reader, audio_url);
    printf("audio_url = %s\n", audio_url);


    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    setup_gui(pandora_helper);

    while (true) {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
            continue;
        }

        ESP_LOGI(TAG, "EVENT: Source type = %d  Source = %p  Cmd = %d", msg.source_type, msg.source, msg.cmd);

#ifdef PANDORA_MP3_DECODER
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }
#endif // MP3_DECODER

#ifdef PANDORA_AAC_DECODER
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT
            && msg.source == (void *) aac_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(aac_decoder, &music_info);

            ESP_LOGI(TAG, "[ * ] Receive music info from aac decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);

            audio_element_setinfo(i2s_stream_writer, &music_info);
            i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            continue;
        }
#endif

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (/*((int)msg.data == AEL_STATUS_STATE_STOPPED) || */ ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGI(TAG, "[ * ] Finished event received");

            if (ESP_OK == pandora_helper_get_next_track(pandora_helper, &audio_url)) {
                // Restart the pipeline with the next url.
                audio_element_set_uri(http_stream_reader, audio_url);
            
                audio_pipeline_stop(pipeline);
                audio_pipeline_wait_for_stop(pipeline);
                audio_pipeline_terminate(pipeline);
                audio_pipeline_reset_ringbuffer(pipeline);
                audio_pipeline_reset_elements(pipeline);
                audio_pipeline_run(pipeline);
                continue;            
            } else {
                // Something went wrong, can't get next track, just bail.
                ESP_LOGE(TAG, "Could not get next track");
                break;
            }
        }
    }
 

    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
#ifdef PANDORA_MP3_DECODER
    audio_pipeline_unregister(pipeline, mp3_decoder);
#endif
#ifdef PANDORA_AAC_DECODER
    audio_pipeline_unregister(pipeline, aac_decoder);
#endif

    audio_pipeline_remove_listener(pipeline);

    /* Stop all peripherals before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(i2s_stream_writer);
#ifdef MP3_DECODER
    audio_element_deinit(mp3_decoder);
#endif
#ifdef PANDORA_AAC_DECODER
    audio_element_deinit(aac_decoder);
#endif

    esp_periph_set_destroy(set);
error:;
}
