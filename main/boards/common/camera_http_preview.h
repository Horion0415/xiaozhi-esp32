#pragma once

#include <cstdint>

#include <esp_http_server.h>

class EspVideo;

class CameraHttpPreviewServer {
public:
    explicit CameraHttpPreviewServer(EspVideo* camera, uint16_t port = 8080);
    ~CameraHttpPreviewServer();

    bool Start();
    bool IsRunning() const { return server_ != nullptr; }
    uint16_t port() const { return port_; }

private:
    static esp_err_t IndexHandler(httpd_req_t* req);
    static esp_err_t StreamHandler(httpd_req_t* req);

    esp_err_t HandleIndex(httpd_req_t* req);
    esp_err_t HandleStream(httpd_req_t* req);

    EspVideo* camera_ = nullptr;
    httpd_handle_t server_ = nullptr;
    uint16_t port_ = 8080;
};
