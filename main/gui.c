/* LVGL Example project
 *
 * Basic project to test LVGL on ESP32 based projects.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "driver/gpio.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#include "lvgl_helpers.h"

#if 0
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    #if defined CONFIG_LV_USE_DEMO_WIDGETS
        #include "lv_examples/src/lv_demo_widgets/lv_demo_widgets.h"
    #elif defined CONFIG_LV_USE_DEMO_KEYPAD_AND_ENCODER
        #include "lv_examples/src/lv_demo_keypad_and_encoder/lv_demo_keypad_and_encoder.h"
    #elif defined CONFIG_LV_USE_DEMO_BENCHMARK
        #include "lv_examples/src/lv_demo_benchmark/lv_demo_benchmark.h"
    #elif defined CONFIG_LV_USE_DEMO_STRESS
        #include "lv_examples/src/lv_demo_stress/lv_demo_stress.h"
    #else
        #error "No demo application selected."
    #endif
#endif
#endif

/*********************
 *      DEFINES
 *********************/
#define TAG "demo"
#define LV_TICK_PERIOD_MS 1 /* increase this ?? */

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);

/**********************
 *   APPLICATION MAIN
 **********************/
void gui_init(
    char* options) 
{

    /* If you want to use a task to create the graphic, you NEED to create a Pinned task
     * Otherwise there can be problem such as memory corruption and so on.
     * NOTE: When not using Wi-Fi nor Bluetooth you can pin the guiTask to core 0 */
    xTaskCreatePinnedToCore(guiTask, "gui", 4096*2, 
        /*parameters*/options, 
        /*uxPriority*/tskIDLE_PRIORITY, 
        /*pvCreatedTask*/NULL, 
        /*xCoreID*//*1*/tskNO_AFFINITY);
}



static void 
roller_event_handler(lv_obj_t * obj, lv_event_t event)
{
    printf("roller_event %d\n", event);
    if(event == LV_EVENT_VALUE_CHANGED) {
        char buf[64];
        lv_roller_get_selected_str(obj, buf, sizeof(buf));
        printf("Selected month: %s\n", buf);
    }
}



static bool 
input_device_read_callback(
    lv_indev_drv_t* drv, 
    lv_indev_data_t* data)
{
    #ifdef CONFIG_ESP_AI_THINKER_V2_2_BOARD
        #define NUM_KEYS 6
        const gpio_num_t key_to_gpio[NUM_KEYS] = { 
            GPIO_NUM_36, // 1 works
            GPIO_NUM_13, // 2 Used for MTCK 
            GPIO_NUM_19, // 3 works.
            GPIO_NUM_23, // 4         on pin header. connected to red led on pcb
            GPIO_NUM_18, // 5         on pin header. 
            GPIO_NUM_5 };// 6 works.  on pin header. 
    #else
        #error Need keyboard mapping for audio board
    #endif

    static int prev_states[NUM_KEYS];
    static int prev_key; // the last key returned
    const uint32_t key_to_lvgl_key[NUM_KEYS] = {LV_KEY_LEFT, 0, LV_KEY_ENTER, LV_KEY_ESC, 0, LV_KEY_RIGHT};

    // If we don't find a key that actually changed state,
    // just return the same key we returned last time.
    data->key = prev_key;
    data->state = prev_states[prev_key];

    // But look for one that changed state
    for (int i=0; i < NUM_KEYS; i++) {
        gpio_set_direction(key_to_gpio[i], GPIO_MODE_INPUT);/////
        int new_state = gpio_get_level(key_to_gpio[i]) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
        if (new_state != prev_states[i]) {
            printf("Key %d changed to %s\n", i+1, new_state == LV_INDEV_STATE_PR ? "Pressed" : "Released");
            // state changed, so can return this key
            data->key = key_to_lvgl_key[i];
            data->state = new_state;
            // Remember state
            prev_states[i] = new_state;
            prev_key = i;
        }
    }

    return false; /*No buffering now so no more data read*/
}


static void
setup_input_device(
    lv_obj_t *obj)
{
    lv_indev_drv_t indev_drv;

    // Create input device
    lv_indev_drv_init(&indev_drv);      
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = input_device_read_callback;
    // Register the driver in LVGL and save the created input device object
    lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);

    // Associate input device with gui object
    lv_group_t * group = lv_group_create();
    lv_group_add_obj(group, obj);
    lv_indev_set_group(my_indev, group);
}



void 
create_widgets(
    char* options)
{

    lv_obj_t * roller = lv_roller_create(/*parent*/lv_scr_act(), NULL);

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_INFINITE);
    //printf("\nReceived options:\n%s\n", options);
    free(options);
    //lv_obj_add_style(roller, LV_CONT_PART_MAIN, &style_box);
    //lv_obj_set_style_local_value_str(roller, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, "Roller");
    //lv_roller_set_auto_fit(roller, false);
    lv_roller_set_align(roller, LV_LABEL_ALIGN_LEFT);
    lv_roller_set_visible_row_count(roller, 9);
    lv_obj_set_width(roller, LV_HOR_RES_MAX);
    lv_obj_set_event_cb(roller, roller_event_handler);

    setup_input_device(roller);
}




/* Creates a semaphore to handle concurrent call to lvgl stuff
 * If you wish to call *any* lvgl function from other threads/tasks
 * you should lock on the very same semaphore! */
SemaphoreHandle_t xGuiSemaphore;

static void 
guiTask(
    void *pvParameter) 
{
    char *options = (char*)pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

#if defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_IL3820         \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_JD79653A    \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_UC8151D     \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_SSD1306

    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;
#endif

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
    disp_drv.set_px_cb = disp_driver_set_px;
#endif

    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    create_widgets(options);

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}


static void lv_tick_task(void *arg) {
    (void) arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}
