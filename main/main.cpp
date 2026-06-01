#include <idf_c_includes.hpp>
#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <network_manager.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>
#include <camera_manager.hpp>

static const char* TAG = "main";
static QueueHandle_t frame_queue = nullptr;

static void on_camera_frame(const CameraCommon::CameraFrame& frame) {
    static uint32_t seq = 0;
    MicrorosSync::publishCompressedImage(frame.buffer, frame.length, seq++);

    CameraCommon::CameraFrame* copy = new CameraCommon::CameraFrame(frame);
    copy->buffer = new uint8_t[frame.length];
    if (!copy->buffer) {
        ESP_LOGE(TAG, "Failed to allocate copy buffer");
        delete copy;
        return;
    }
    memcpy(copy->buffer, frame.buffer, frame.length);
    if (xQueueSend(frame_queue, &copy, 0) != pdTRUE) {
        ESP_LOGD(TAG, "Frame queue full, discarding frame");
        delete[] copy->buffer;
        delete copy;
    }
}

extern "C" void app_main() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("web-cam"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-CAM HTTP Server"));
    ESP_ERROR_CHECK(mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0));
    ESP_LOGI(TAG, "mDNS initialised: http://web-cam.local");

    StateMachine::init();
    StateMachine::setInitial("shelfbot", stateToString(ShelfbotState::STARTING));
    StateMachine::setInitial("camera",   stateToString(CameraState::OFF));
    StateMachine::setInitial("microros", stateToString(MicrorosState::OFF));
    // IMPORTANT: Do NOT set initial state for "wifi_manager" here.
    // wifi_manager_init() will register itself after hardware init.

    frame_queue = xQueueCreate(10, sizeof(CameraCommon::CameraFrame*));
    configASSERT(frame_queue);

    // Wi-Fi manager – this will initialise hardware and register its state
    ESP_ERROR_CHECK(wifi_manager_init());

    MicrorosSync::getInstance().init();
    MicrorosSync::getInstance().start();

    ESP_ERROR_CHECK(CameraManager::getInstance().initialize());
    if (!CameraManager::getInstance().waitUntilReady(5000)) {
        ESP_LOGE(TAG, "Camera not ready after 5 seconds");
    } else {
        ESP_LOGI(TAG, "Camera hardware ready");
    }

    // HTTP server – will wait for Wi-Fi state machine to become CONNECTED
    ESP_ERROR_CHECK(NetworkManager::get_instance().init(frame_queue));

    ESP_LOGI(TAG, "Starting continuous capture");
    CameraManager::getInstance().startCapture(on_camera_frame);

    StateMachine::changeState("shelfbot", stateToString(ShelfbotState::RUNNING));
    ESP_LOGI(TAG, "app_main done — all tasks running");
}