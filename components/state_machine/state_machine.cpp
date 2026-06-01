#include <state_machine.hpp>
#include <state_machine_lifecycle.hpp>

static const char* TAG = "StateMachine";

// Static members
std::mutex StateMachine::mutex_;
std::unordered_map<std::string, StateMachine::ModuleState> StateMachine::modules_;
TaskHandle_t StateMachine::status_task_handle_ = nullptr;
bool StateMachine::task_running_ = false;

void StateMachine::init() {
    if (task_running_) {
        ESP_LOGW(TAG, "State machine already initialised");
        return;
    }
    task_running_ = true;
    xTaskCreate(status_dump_task, "state_dump", 4096, nullptr, 1, &status_task_handle_);
    ESP_LOGI(TAG, "State machine initialised");
}

bool StateMachine::setInitial(const std::string& module, const std::string& initial_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (modules_.find(module) != modules_.end()) {
        ESP_LOGW(TAG, "Module '%s' already exists, ignoring setInitial", module.c_str());
        return false;
    }
    modules_.emplace(module, ModuleState(initial_state));
    ESP_LOGI(TAG, "Module '%s' initial state: %s", module.c_str(), initial_state.c_str());
    return true;
}

bool StateMachine::changeState(const std::string& module, const std::string& new_state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = modules_.find(module);
    if (it == modules_.end()) {
        ESP_LOGE(TAG, "Module '%s' not found", module.c_str());
        return false;
    }
    const std::string& old_state = it->second.current_state;
    if (old_state == new_state) {
        ESP_LOGD(TAG, "Module '%s' already in state %s", module.c_str(), new_state.c_str());
        return true;
    }
    it->second.current_state = new_state;
    ESP_LOGI(TAG, "Module '%s' state: %s -> %s", module.c_str(), old_state.c_str(), new_state.c_str());
    return true;
}

const std::string& StateMachine::getState(const std::string& module) {
    std::lock_guard<std::mutex> lock(mutex_);
    static const std::string empty;
    auto it = modules_.find(module);
    if (it == modules_.end()) return empty;
    return it->second.current_state;
}

void StateMachine::status_dump_task(void* arg) {
    while (task_running_) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        dumpAllStates();
    }
    vTaskDelete(nullptr);
}

void StateMachine::dumpAllStates() {
    std::lock_guard<std::mutex> lock(mutex_);
    ESP_LOGI(TAG, "=== Module States ===");
    for (const auto& pair : modules_) {
        ESP_LOGI(TAG, "  %-20s : %s", pair.first.c_str(), pair.second.current_state.c_str());
    }
}