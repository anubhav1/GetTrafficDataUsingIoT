/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "iot_button.h"
#include "nvs_flash.h"
#include "board_esp32_devkitc.h"
#include "esp_log.h"


static const char *TAG = "Wifi station";


static void button_press_3sec_cb(void *arg)
{   
    ESP_LOGI(TAG, "Erasing NVS");
    nvs_flash_erase();
    esp_restart();
}

static void configure_push_button(int gpio_num, void (*btn_cb)(void *))
{
    button_handle_t btn_handle = iot_button_create(JUMPSTART_BOARD_BUTTON_GPIO, JUMPSTART_BOARD_BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        iot_button_add_on_press_cb(btn_handle, 3, button_press_3sec_cb, NULL);
    }
}


void btn_driver_init()
{
    configure_push_button(JUMPSTART_BOARD_BUTTON_GPIO, button_press_3sec_cb);

}

