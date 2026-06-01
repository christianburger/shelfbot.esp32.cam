#pragma once
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <idf_c_includes.hpp>

class HttpServer {
public:
    static HttpServer& get_instance() {
        static HttpServer instance;
        return instance;
    }

    HttpServer(const HttpServer&)            = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    esp_err_t start();
    esp_err_t stop();
    [[nodiscard]] bool is_running() const { return server_ != nullptr; }

private:
    HttpServer()  = default;
    ~HttpServer() = default;

    httpd_handle_t server_ = nullptr;
    static const char* TAG;

    static esp_err_t register_uri_handlers(httpd_handle_t server);

    // Page handlers
    static esp_err_t root_handler(httpd_req_t* req);
    static esp_err_t capture_handler(httpd_req_t* req);
    static esp_err_t stream_handler(httpd_req_t* req);

    // API handlers
    static esp_err_t health_handler(httpd_req_t* req);
    static esp_err_t status_handler(httpd_req_t* req);
};

#endif // HTTP_SERVER_H
