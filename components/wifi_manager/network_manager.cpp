#include <network_manager.hpp>
#include <controller.hpp>

const char* NetworkManager::TAG = "NetworkManager";

// ---------------------------------------------------------------------------
// Configuration — adjust via sdkconfig / Kconfig in production
// ---------------------------------------------------------------------------

static constexpr const char* WIFI_SSID             = "dlink-30C0";
static constexpr const char* WIFI_PASS             = "ypics98298";
static constexpr uint32_t    WIFI_RECONNECT_DELAY_MS = 1000;
static constexpr uint32_t    TASK_STACK_SIZE        = 8192;
static constexpr UBaseType_t TASK_PRIORITY          = 5;
static constexpr BaseType_t  TASK_CORE_ID           = 0;
static constexpr uint8_t     MAX_URI_HANDLERS       = 5;
static constexpr uint8_t     MAX_OPEN_SOCKETS       = 2;

// ---------------------------------------------------------------------------
// Wi-Fi event handler
// ---------------------------------------------------------------------------

void NetworkManager::wifi_event_handler(void*            arg,
                                        esp_event_base_t event_base,
                                        int32_t          event_id,
                                        void*            event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi station started — connecting");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "Disconnected — retrying in %lu ms",
                         static_cast<unsigned long>(WIFI_RECONNECT_DELAY_MS));
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
                esp_wifi_connect();
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto* ev = static_cast<const ip_event_got_ip_t*>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

// ---------------------------------------------------------------------------
// Wi-Fi STA init
// ---------------------------------------------------------------------------

esp_err_t NetworkManager::wifi_init_sta() {
    esp_netif_t* sta = esp_netif_create_default_wifi_sta();
    if (!sta) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        return ESP_FAIL;
    }

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr, nullptr));

    wifi_config_t wifi_cfg = {};
    strlcpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
            WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    strlcpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
            WIFI_PASS, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(12));

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------

esp_err_t NetworkManager::start_http_server(QueueHandle_t frame_queue) {
    Controller::get_instance().init(frame_queue);

    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.stack_size        = TASK_STACK_SIZE;
    config.task_priority     = TASK_PRIORITY;
    config.core_id           = TASK_CORE_ID;
    config.max_uri_handlers  = MAX_URI_HANDLERS;
    config.max_open_sockets  = MAX_OPEN_SOCKETS;
    config.lru_purge_enable  = true;

    httpd_handle_t server = nullptr;
    const esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET, .handler = Controller::root_handler,          .user_ctx = nullptr },
        { .uri = "/capture",  .method = HTTP_GET, .handler = Controller::capture_handler,       .user_ctx = nullptr },
        { .uri = "/stream",   .method = HTTP_GET, .handler = Controller::stream_handler,        .user_ctx = nullptr },
        { .uri = "/status",   .method = HTTP_GET, .handler = Controller::status_handler,        .user_ctx = nullptr },
        { .uri = "/hardware", .method = HTTP_GET, .handler = Controller::hardware_info_handler, .user_ctx = nullptr },
    };
    for (const auto& r : routes) {
        httpd_register_uri_handler(server, &r);
    }

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t NetworkManager::init(QueueHandle_t frame_queue) {
    ESP_LOGI(TAG, "Initialising network manager");

    esp_err_t err = wifi_init_sta();
    if (err != ESP_OK) {
        return err;
    }

    return start_http_server(frame_queue);
}

void NetworkManager::network_task(void* arg) {
    const auto* queue = static_cast<QueueHandle_t*>(arg);
    ESP_ERROR_CHECK(NetworkManager::get_instance().init(queue ? *queue : nullptr));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
