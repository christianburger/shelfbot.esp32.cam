#include <idf_c_includes.hpp>
#include <wifi_manager.hpp>
#include <microros_sync.hpp>
#include <network_manager.hpp>
#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>
#include <camera_manager.hpp>
#include <shelfbot_timestamp.hpp>
#include <firmware_version.hpp>

static const char* TAG = "main";
static QueueHandle_t frame_queue = nullptr;

// ---------------------------------------------------------------------------
// Camera frame callback
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Wait for micro-ROS time sync (or timeout into CONNECTED/ERROR)
// ---------------------------------------------------------------------------

static void wait_for_microros_time_sync() {
    ESP_LOGI(TAG, "Waiting for micro-ROS time sync...");
    const std::string discovering = stateToString(MicrorosState::DISCOVERING);
    const std::string time_sync   = stateToString(MicrorosState::TIME_SYNC);
    const std::string off         = stateToString(MicrorosState::OFF);

    while (true) {
        const std::string state = StateMachine::getState("microros");
        if (state != off && state != discovering && state != time_sync) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    const std::string final_state = StateMachine::getState("microros");
    ESP_LOGI(TAG, "micro-ROS state after sync wait: %s", final_state.c_str());
}

// ---------------------------------------------------------------------------
// app_main
// ---------------------------------------------------------------------------

extern "C" void app_main() {
    // Print firmware version as the very first log line so every boot is
    // immediately identifiable in the serial monitor / log files.
    FirmwareVersion::print_version(TAG);

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

    // Early clock check — expected to fail on cold boot (no battery-backed RTC).
    if (!shelfbot::ShelfbotTimestamp::isEpochValid()) {
        ESP_LOGW(TAG, "Clock not synchronized — epoch invalid at startup (expected)");
    }

    // -----------------------------------------------------------------------
    // State machine: init the framework, then register only the modules that
    // main.cpp owns.  Every other module registers itself inside its own
    // component's init function:
    //   "camera"       → CameraControl constructor   (camera_control component)
    //   "microros"     → MicrorosSync::init()        (microros_sync component)
    //   "wifi_manager" → wifi_manager_init()         (wifi_manager component)
    // -----------------------------------------------------------------------
    StateMachine::init();
    StateMachine::setInitial("shelfbot", stateToString(ShelfbotState::STARTING));

    frame_queue = xQueueCreate(10, sizeof(CameraCommon::CameraFrame*));
    configASSERT(frame_queue);

    // Wi-Fi init also:
    //  • registers "wifi_manager" in the state machine
    //  • prints the firmware version to its own log tag
    //  • starts SNTP automatically on the on_ip_event callback
    ESP_ERROR_CHECK(wifi_manager_init());

    // MicrorosSync::init() registers "microros" in the state machine.
    MicrorosSync::getInstance().init();
    MicrorosSync::getInstance().start();

    // CameraManager::initialize() creates CameraControl, whose constructor
    // registers "camera" in the state machine.
    ESP_ERROR_CHECK(CameraManager::getInstance().initialize());
    if (!CameraManager::getInstance().waitUntilReady(5000)) {
        ESP_LOGE(TAG, "Camera not ready after 5 seconds");
    } else {
        ESP_LOGI(TAG, "Camera hardware ready");
    }

    // HTTP server — waits internally for wifi_manager state == CONNECTED.
    ESP_ERROR_CHECK(NetworkManager::get_instance().init(frame_queue));

    // Wait for micro-ROS time sync before starting capture so the first
    // published frame already carries a valid epoch stamp.
    wait_for_microros_time_sync();

    ESP_LOGI(TAG, "Starting continuous capture");
    CameraManager::getInstance().startCapture(on_camera_frame);

    StateMachine::changeState("shelfbot", stateToString(ShelfbotState::RUNNING));
    ESP_LOGI(TAG, "app_main done — all tasks running");
}
