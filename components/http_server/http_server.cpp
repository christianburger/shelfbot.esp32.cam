#include <http_server.hpp>

const char* HttpServer::TAG = "HttpServer";

// ---------------------------------------------------------------------------
// CORS helpers
// ---------------------------------------------------------------------------

static void add_cors_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin",  "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static esp_err_t options_handler(httpd_req_t* req) {
    add_cors_headers(req);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t HttpServer::start() {
    if (server_ != nullptr) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HTTP server...");
    httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn      = httpd_uri_match_wildcard;
    config.max_uri_handlers  = 12;
    config.stack_size        = 8192;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    err = register_uri_handlers(server_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register URI handlers");
        httpd_stop(server_);
        server_ = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t HttpServer::stop() {
    if (server_ == nullptr) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Stopping HTTP server...");
    esp_err_t err = httpd_stop(server_);
    if (err == ESP_OK) {
        server_ = nullptr;
    }
    return err;
}

esp_err_t HttpServer::register_uri_handlers(const httpd_handle_t server) {
    const httpd_uri_t routes[] = {
        { .uri = "/",          .method = HTTP_GET, .handler = root_handler,    .user_ctx = nullptr },
        { .uri = "/capture",   .method = HTTP_GET, .handler = capture_handler, .user_ctx = nullptr },
        { .uri = "/stream",    .method = HTTP_GET, .handler = stream_handler,  .user_ctx = nullptr },
        { .uri = "/api/health",.method = HTTP_GET, .handler = health_handler,  .user_ctx = nullptr },
        { .uri = "/api/status",.method = HTTP_GET, .handler = status_handler,  .user_ctx = nullptr },
    };
    for (const auto& r : routes) {
        httpd_register_uri_handler(server, &r);
    }

    const char* cors_endpoints[] = { "/api/health", "/api/status" };
    esp_err_t   last_err         = ESP_OK;
    for (const auto* ep : cors_endpoints) {
        const httpd_uri_t opt = {
            .uri      = ep,
            .method   = HTTP_OPTIONS,
            .handler  = options_handler,
            .user_ctx = nullptr,
        };
        last_err = httpd_register_uri_handler(server, &opt);
    }
    return last_err;
}

// ---------------------------------------------------------------------------
// Root page
// ---------------------------------------------------------------------------

esp_err_t HttpServer::root_handler(httpd_req_t* req) {
    static constexpr const char* HTML = R"HTML(<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body{font-family:Inter,Arial,sans-serif;margin:0;background:#0f172a;color:#e2e8f0}
    .wrap{max-width:900px;margin:0 auto;padding:28px}
    h1{margin:0 0 6px}
    .sub{color:#94a3b8;margin-bottom:24px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px}
    .card{background:#1e293b;border:1px solid #334155;padding:16px;border-radius:12px}
    .card h3{margin-top:0}
    a{color:#93c5fd;text-decoration:none;font-weight:600}
    a:hover{text-decoration:underline}
    code{background:#0b1220;padding:2px 6px;border-radius:4px;color:#93c5fd}
    img{width:100%;border-radius:10px;border:1px solid #334155;margin-top:16px;background:#020617}
    pre{white-space:pre-wrap;background:#020617;color:#a7f3d0;padding:10px;border-radius:8px;font-size:13px}
    button{background:#22c55e;border:0;border-radius:8px;padding:8px 16px;color:#04110a;font-weight:700;cursor:pointer;margin-top:8px}
  </style>
</head>
<body>
<div class="wrap">
  <h1>ESP32-CAM</h1>
  <p class="sub">Camera dashboard — capture, stream, and diagnostics.</p>
  <div class="grid">
    <div class="card">
      <h3>Capture</h3>
      <p>Single JPEG snapshot.</p>
      <a href="/capture"><code>GET /capture</code></a><br>
      <button onclick="document.getElementById('snap').src='/capture?t='+Date.now()">Take Photo</button>
    </div>
    <div class="card">
      <h3>Stream</h3>
      <p>MJPEG live stream.</p>
      <a href="/stream"><code>GET /stream</code></a><br>
      <button onclick="document.getElementById('sv').style.display='block';document.getElementById('sv').src='/stream'">Start Stream</button>
    </div>
    <div class="card">
      <h3>Health</h3>
      <p><a href="/api/health"><code>GET /api/health</code></a></p>
      <pre id="health">Loading...</pre>
    </div>
    <div class="card">
      <h3>Status</h3>
      <p><a href="/api/status"><code>GET /api/status</code></a></p>
      <pre id="status">Loading...</pre>
    </div>
  </div>
  <img id="snap" src="/capture" alt="Latest capture">
  <img id="sv" style="display:none" alt="Live stream">
</div>
<script>
  async function poll(url, id) {
    try {
      const d = await (await fetch(url)).json();
      document.getElementById(id).textContent = JSON.stringify(d, null, 2);
    } catch(e) {
      document.getElementById(id).textContent = 'error: ' + e;
    }
  }
  poll('/api/health', 'health');
  poll('/api/status', 'status');
  setInterval(function() { poll('/api/health', 'health'); poll('/api/status', 'status'); }, 2000);
</script>
</body>
</html>)HTML";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, HTML, HTTPD_RESP_USE_STRLEN);
}

// ---------------------------------------------------------------------------
// Capture — single JPEG frame
// ---------------------------------------------------------------------------

esp_err_t HttpServer::capture_handler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Capture request");

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture failed");
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    const esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);

    esp_camera_fb_return(fb);
    return res;
}

// ---------------------------------------------------------------------------
// Stream — MJPEG multipart
// ---------------------------------------------------------------------------

esp_err_t HttpServer::stream_handler(httpd_req_t* req) {
    ESP_LOGI(TAG, "Stream started");
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    static constexpr const char* PART_HDR =
        "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Stream: camera capture failed");
            break;
        }

        char hdr_buf[64];
        const int hdr_len = snprintf(hdr_buf, sizeof(hdr_buf), PART_HDR, fb->len);

        esp_err_t res = httpd_resp_send_chunk(req, hdr_buf, hdr_len);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf), fb->len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, "\r\n", 2);
        }

        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Stream: client disconnected");
            break;
        }
    }

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// API: health
// ---------------------------------------------------------------------------

esp_err_t HttpServer::health_handler(httpd_req_t* req) {
    add_cors_headers(req);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device",        "ESP32-CAM");
    cJSON_AddNumberToObject(root, "free_heap",      esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap",  esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_ms",      esp_timer_get_time() / 1000);
    cJSON_AddBoolToObject  (root, "psram_available", esp_psram_get_size() > 0);
    cJSON_AddNumberToObject(root, "psram_size",      esp_psram_get_size());

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    const esp_err_t ret = httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    return ret;
}

// ---------------------------------------------------------------------------
// API: status (camera sensor info)
// ---------------------------------------------------------------------------

esp_err_t HttpServer::status_handler(httpd_req_t* req) {
    add_cors_headers(req);

    sensor_t* sensor = esp_camera_sensor_get();

    cJSON* root = cJSON_CreateObject();
    if (!sensor) {
        cJSON_AddBoolToObject  (root, "camera_ready", false);
        cJSON_AddStringToObject(root, "error",        "camera sensor not initialised");
    } else {
        cJSON_AddBoolToObject  (root, "camera_ready",   true);
        cJSON_AddNumberToObject(root, "pid",             sensor->id.PID);
        cJSON_AddNumberToObject(root, "quality",         sensor->status.quality);
        cJSON_AddNumberToObject(root, "brightness",      sensor->status.brightness);
        cJSON_AddNumberToObject(root, "contrast",        sensor->status.contrast);
        cJSON_AddNumberToObject(root, "saturation",      sensor->status.saturation);
        cJSON_AddNumberToObject(root, "sharpness",       sensor->status.sharpness);
        cJSON_AddBoolToObject  (root, "hmirror",         sensor->status.hmirror);
        cJSON_AddBoolToObject  (root, "vflip",           sensor->status.vflip);
        cJSON_AddNumberToObject(root, "framesize",       sensor->status.framesize);
        cJSON_AddBoolToObject  (root, "awb",             sensor->status.awb);
        cJSON_AddBoolToObject  (root, "aec",             sensor->status.aec);
        cJSON_AddBoolToObject  (root, "agc",             sensor->status.agc);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    const esp_err_t ret = httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    return ret;
}
