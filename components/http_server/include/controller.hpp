#pragma once
#ifndef CONTROLLER_HPP
#define CONTROLLER_HPP

#include <idf_c_includes.hpp>

class Controller {
public:
    static Controller& get_instance() {
        static Controller instance;
        return instance;
    }

    Controller(const Controller&)            = delete;
    Controller& operator=(const Controller&) = delete;

    // Called once from app_main before any handler is registered.
    // Provides the queue that camera_task pushes frames onto.
    void init(QueueHandle_t frame_queue);

    // HTTP handlers — registered directly with esp_http_server
    static esp_err_t root_handler(httpd_req_t* req);
    static esp_err_t capture_handler(httpd_req_t* req);
    static esp_err_t stream_handler(httpd_req_t* req);
    static esp_err_t status_handler(httpd_req_t* req);
    static esp_err_t hardware_info_handler(httpd_req_t* req);

private:
    Controller()  = default;
    ~Controller() = default;

    static const char* TAG;

    // Shared frame queue — set once via init(), read by handlers
    static QueueHandle_t frame_queue_;
};

#endif // CONTROLLER_HPP
