#include <network_manager.hpp>
#include <controller.hpp>
#include <wifi_manager.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>

const char* NetworkManager::TAG = "NetworkManager";

// ---------------------------------------------------------------------------
// HTTP server configuration
// ---------------------------------------------------------------------------
static constexpr uint32_t    TASK_STACK_SIZE        = 8192;
static constexpr UBaseType_t TASK_PRIORITY          = 5;
static constexpr BaseType_t  TASK_CORE_ID           = 0;
static constexpr uint8_t     MAX_URI_HANDLERS       = 12;
static constexpr uint8_t     MAX_OPEN_SOCKETS       = 5;

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
esp_err_t NetworkManager::start_http_server(QueueHandle_t frame_queue) {
    Controller::get_instance().init(frame_queue);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size        = TASK_STACK_SIZE;
    config.task_priority     = TASK_PRIORITY;
    config.core_id           = TASK_CORE_ID;
    config.max_uri_handlers  = MAX_URI_HANDLERS;
    config.max_open_sockets  = MAX_OPEN_SOCKETS;
    config.lru_purge_enable  = true;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &config);
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

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t NetworkManager::init(QueueHandle_t frame_queue) {
    ESP_LOGI(TAG, "Initialising network manager");

    // Wait for Wi‑Fi to be connected using the state machine
    ESP_LOGI(TAG, "Waiting for Wi-Fi connection (state machine) ...");
    while (true) {
        std::string wifi_state = StateMachine::getState("wifi_manager");
        if (wifi_state == stateToString(WifiManagerState::CONNECTED)) {
            ESP_LOGI(TAG, "Wi-Fi is connected");
            break;
        }
        if (wifi_state == stateToString(WifiManagerState::ERROR) ||
            wifi_state == stateToString(WifiManagerState::OFF)) {
            ESP_LOGW(TAG, "Wi-Fi in error/off state, waiting anyway...");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
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