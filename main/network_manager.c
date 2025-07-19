#include "network_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "controller.h"
#include "esp_psram.h"

static const char *TAG = "network_manager";
static httpd_handle_t server = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "wifi_event_handler ==> WiFi station started");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "wifi_event_handler ==> Connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "wifi_event_handler ==> Disconnected from AP - attempting reconnection");
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
                esp_wifi_connect();
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (event_base == IP_EVENT) {
          ESP_LOGI(TAG, "wifi_event_handler ==> event_base == IP_EVENT");
        }
        if (event_base == IP_EVENT_STA_GOT_IP) {
          ESP_LOGI(TAG, "wifi_event_handler ==> event_base == IP_EVENT_STA_GOT_IP");
        }
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }

    ESP_LOGI(TAG, "wifi_event_handler ==> LEAVING ");
}

static void wifi_init_sta(void) {
    ESP_LOGI(TAG, "=== WiFi Station Initialization Starting ===");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(12));
}
void network_task(void *pvParameters) {
    // Register this task with the watchdog
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    
    wifi_init_sta();
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = NETWORK_TASK_STACK_SIZE;
    config.task_priority = NETWORK_TASK_PRIORITY;
    config.core_id = NETWORK_TASK_CORE_ID;
    config.max_uri_handlers = 5;
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t uri_capture = {
            .uri       = "/capture",
            .method    = HTTP_GET,
            .handler   = capture_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t uri_stream = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t uri_status = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };
        httpd_uri_t uri_hardware = {
          .uri       = "/hardware",
          .method    = HTTP_GET,
          .handler   = hardware_info_handler,
          .user_ctx  = NULL
        };

        httpd_register_uri_handler(server, &uri_root);
        httpd_register_uri_handler(server, &uri_capture);
        httpd_register_uri_handler(server, &uri_stream);
        httpd_register_uri_handler(server, &uri_status);
        httpd_register_uri_handler(server, &uri_hardware);

        ESP_LOGI(TAG, "Web server started successfully");
    }

    while(1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t network_manager_init(void) {
    ESP_LOGI(TAG, "Initializing network manager");
    return ESP_OK;
}
