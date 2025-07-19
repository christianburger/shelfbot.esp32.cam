#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_http_server.h"
#include "esp_camera.h"

// HTTP handlers
esp_err_t root_handler(httpd_req_t *req);
esp_err_t capture_handler(httpd_req_t *req);
esp_err_t stream_handler(httpd_req_t *req);
esp_err_t status_handler(httpd_req_t *req);
esp_err_t hardware_info_handler(httpd_req_t *req);

#endif // CONTROLLER_H