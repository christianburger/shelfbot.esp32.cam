#pragma once
#include <idf_c_includes.hpp>

enum class ShelfbotState : uint8_t { STARTING, RUNNING, ERROR, SHUTDOWN, COUNT };

// TIME_SYNC sits between DISCOVERING and CONNECTED:
//   OFF → DISCOVERING → TIME_SYNC → CONNECTED → DISCONNECTED → DISCOVERING …
enum class MicrorosState : uint8_t {
    OFF,
    DISCOVERING,
    TIME_SYNC,     // agent reachable, waiting for clock sync via rmw_uros_sync_session()
    CONNECTED,
    ERROR,
    DISCONNECTED,
    COUNT
};

enum class WifiManagerState : uint8_t { OFF, CONNECTING, CONNECTED, ERROR, DISCONNECTED, COUNT };

inline const char* stateToString(ShelfbotState s) {
    switch(s) {
        case ShelfbotState::STARTING:  return "starting";
        case ShelfbotState::RUNNING:   return "running";
        case ShelfbotState::ERROR:     return "error";
        case ShelfbotState::SHUTDOWN:  return "shutdown";
        default: return "unknown";
    }
}
inline const char* stateToString(MicrorosState s) {
    switch(s) {
        case MicrorosState::OFF:          return "off";
        case MicrorosState::DISCOVERING:  return "discovering";
        case MicrorosState::TIME_SYNC:    return "time_sync";
        case MicrorosState::CONNECTED:    return "connected";
        case MicrorosState::ERROR:        return "error";
        case MicrorosState::DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}
inline const char* stateToString(WifiManagerState s) {
    switch(s) {
        case WifiManagerState::OFF:          return "off";
        case WifiManagerState::CONNECTING:   return "connecting";
        case WifiManagerState::CONNECTED:    return "connected";
        case WifiManagerState::ERROR:        return "error";
        case WifiManagerState::DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

// Transition matrices (only needed for validation, optional)
constexpr std::array<std::array<bool, static_cast<size_t>(ShelfbotState::COUNT)>, static_cast<size_t>(ShelfbotState::COUNT)> shelfbot_transitions = {{
    {{false, true,  true,  false}},
    {{false, false, true,  true }},
    {{false, false, false, true }},
    {{false, false, false, false}}
}};
