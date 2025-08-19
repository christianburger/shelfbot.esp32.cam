#include "network_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "controller.h"
#include "esp_psram.h"
#include "mdns.h"

static const char *TAG = "network_manager";
static httpd_handle_t server = NULL;

// --- FreeRTOS event group to signal when we are connected ---
static EventGroupHandle_t s_wifi_event_group;

// --- Event bits for connection status ---
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// --- Retry logic ---
static int s_retry_num = 0;
#define WIFI_MAXIMUM_RETRY 10

static void initialise_mdns(void)
{
    mdns_init();
    mdns_hostname_set("shelfbot-camera");
    mdns_instance_name_set("Shelfbot Camera");
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "failed to connect to the AP");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void network_task(void *pvParameters) {
    // Register this task with the watchdog
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    s_wifi_event_group = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization started. Waiting for connection...");

    while(1) {
        // Always feed the watchdog
        esp_task_wdt_reset();

        EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);

        if (server == NULL) { // Only check for connection if server is not started
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "connected to ap SSID:%s", WIFI_SSID);
                
                initialise_mdns();

                httpd_config_t config = HTTPD_DEFAULT_CONFIG();
                config.stack_size = NETWORK_TASK_STACK_SIZE;
                config.task_priority = NETWORK_TASK_PRIORITY;
                config.core_id = NETWORK_TASK_CORE_ID;
                config.max_uri_handlers = 5;
                config.max_open_sockets = 2;
                config.lru_purge_enable = true;
                
                if (httpd_start(&server, &config) == ESP_OK) {
                    httpd_uri_t uri_root = { .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = NULL };
                    httpd_uri_t uri_capture = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = NULL };
                    httpd_uri_t uri_stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
                    httpd_uri_t uri_status = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = NULL };
                    httpd_uri_t uri_hardware = { .uri = "/hardware", .method = HTTP_GET, .handler = hardware_info_handler, .user_ctx = NULL };

                    httpd_register_uri_handler(server, &uri_root);
                    httpd_register_uri_handler(server, &uri_capture);
                    httpd_register_uri_handler(server, &uri_stream);
                    httpd_register_uri_handler(server, &uri_status);
                    httpd_register_uri_handler(server, &uri_hardware);

                    ESP_LOGI(TAG, "Web server started successfully on port 80");
                } else {
                    ESP_LOGE(TAG, "Failed to start web server!");
                }
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGE(TAG, "Failed to connect to SSID:%s. Halting network task.", WIFI_SSID);
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}