#pragma once
#include <idf_c_includes.hpp>

class StateMachine {
public:
    struct ModuleState {
        std::string current_state;
        ModuleState() = default;
        explicit ModuleState(const std::string& state) : current_state(state) {}
    };
    static void init();
    static bool setInitial(const std::string& module, const std::string& initial_state);
    static bool changeState(const std::string& module, const std::string& new_state);
    static const std::string& getState(const std::string& module);
private:
    static std::mutex mutex_;
    static std::unordered_map<std::string, ModuleState> modules_;
    static TaskHandle_t status_task_handle_;
    static bool task_running_;
    static void status_dump_task(void* arg);
    static void dumpAllStates();
};