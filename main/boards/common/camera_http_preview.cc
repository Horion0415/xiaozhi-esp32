#include "camera_http_preview.h"

#include <esp_check.h>
#include <esp_log.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "application.h"
#include "esp_video.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr char TAG[] = "CamPreview";
constexpr char kStreamContentType[] = "multipart/x-mixed-replace;boundary=frame";
constexpr char kStreamBoundary[] = "\r\n--frame\r\n";
constexpr char kCacheControl[] = "no-store, no-cache, must-revalidate, max-age=0";
constexpr uint8_t kJpegQuality = 80;
constexpr TickType_t kPausedPollInterval = pdMS_TO_TICKS(100);

}  // namespace

CameraHttpPreviewServer::CameraHttpPreviewServer(EspVideo* camera, uint16_t port)
    : camera_(camera), port_(port) {
}

CameraHttpPreviewServer::~CameraHttpPreviewServer() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

bool CameraHttpPreviewServer::Start() {
    if (server_ != nullptr) {
        return true;
    }
    if (camera_ == nullptr) {
        ESP_LOGE(TAG, "Camera preview server has no camera");
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = port_ + 1;
    config.stack_size = 8192;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP preview server on port %u", port_);
        server_ = nullptr;
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = IndexHandler,
        .user_ctx = this,
    };
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = StreamHandler,
        .user_ctx = this,
    };

    if (httpd_register_uri_handler(server_, &index_uri) != ESP_OK ||
        httpd_register_uri_handler(server_, &stream_uri) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HTTP preview handlers");
        httpd_stop(server_);
        server_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Camera preview server started on port %u", port_);
    return true;
}

esp_err_t CameraHttpPreviewServer::IndexHandler(httpd_req_t* req) {
    return static_cast<CameraHttpPreviewServer*>(req->user_ctx)->HandleIndex(req);
}

esp_err_t CameraHttpPreviewServer::StreamHandler(httpd_req_t* req) {
    return static_cast<CameraHttpPreviewServer*>(req->user_ctx)->HandleStream(req);
}

esp_err_t CameraHttpPreviewServer::HandleIndex(httpd_req_t* req) {
    std::string html = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Camera Preview</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f4efe7;
      --panel: rgba(255,255,255,0.86);
      --ink: #1f2a30;
      --muted: #5d6b73;
      --accent: #d96b3b;
      --edge: rgba(31,42,48,0.12);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: "IBM Plex Sans", "Noto Sans", sans-serif;
      color: var(--ink);
      background:
        radial-gradient(circle at top left, rgba(217,107,59,0.24), transparent 32%),
        linear-gradient(135deg, #f7f2ea 0%, #e9f0ef 100%);
      display: grid;
      place-items: center;
      padding: 20px;
    }
    main {
      width: min(960px, 100%);
      background: var(--panel);
      border: 1px solid var(--edge);
      border-radius: 24px;
      box-shadow: 0 20px 60px rgba(31,42,48,0.12);
      overflow: hidden;
      backdrop-filter: blur(10px);
    }
    header {
      padding: 24px 28px 12px;
    }
    h1 {
      margin: 0;
      font-size: clamp(28px, 5vw, 42px);
      line-height: 1;
      letter-spacing: -0.04em;
    }
    p {
      margin: 10px 0 0;
      color: var(--muted);
      line-height: 1.5;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      margin-top: 16px;
      padding: 8px 12px;
      border-radius: 999px;
      background: rgba(217,107,59,0.12);
      color: var(--accent);
      font-size: 13px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.08em;
    }
    .preview {
      padding: 16px 20px 24px;
    }
    .frame {
      overflow: hidden;
      border-radius: 18px;
      border: 1px solid var(--edge);
      background: #101719;
      aspect-ratio: 3 / 4;
      display: grid;
      place-items: center;
    }
    img {
      display: block;
      width: 100%;
      height: 100%;
      object-fit: contain;
      background: #101719;
    }
    code {
      font-family: "IBM Plex Mono", "JetBrains Mono", monospace;
      font-size: 0.95em;
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>Camera Preview</h1>
      <p>)";
    html += BOARD_NAME;
    html += R"( MJPEG stream is running without authentication. Open <code>/stream</code> directly if you only need the raw feed.</p>
      <div class="badge">Live MJPEG</div>
    </header>
    <section class="preview">
      <div class="frame">
        <img src="/stream" alt="Live camera preview">
      </div>
    </section>
  </main>
</body>
</html>)";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", kCacheControl);
    return httpd_resp_send(req, html.c_str(), html.size());
}

esp_err_t CameraHttpPreviewServer::HandleStream(httpd_req_t* req) {
    if (camera_ == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera is unavailable");
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, kStreamContentType), TAG, "Failed to set stream content type");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Cache-Control", kCacheControl), TAG, "Failed to set cache control");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"), TAG, "Failed to set CORS header");

    int sockfd = httpd_req_to_sockfd(req);
    bool paused_for_voice = false;
    ESP_LOGI(TAG, "Preview stream opened: sock=%d", sockfd);

    while (true) {
        if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            if (!paused_for_voice) {
                paused_for_voice = true;
                ESP_LOGI(TAG, "Preview paused for voice interaction: sock=%d", sockfd);
            }
            vTaskDelay(kPausedPollInterval);
            continue;
        }

        if (paused_for_voice) {
            paused_for_voice = false;
            ESP_LOGI(TAG, "Preview resumed: sock=%d", sockfd);
        }

        std::string jpeg_data;
        if (!camera_->CaptureToJpeg(jpeg_data, kJpegQuality)) {
            ESP_LOGW(TAG, "Preview capture failed: sock=%d", sockfd);
            break;
        }

        char part_header[96];
        int header_len = snprintf(part_header, sizeof(part_header),
                                  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                  static_cast<unsigned>(jpeg_data.size()));
        if (header_len <= 0) {
            ESP_LOGW(TAG, "Failed to format MJPEG part header");
            break;
        }

        if (httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary)) != ESP_OK ||
            httpd_resp_send_chunk(req, part_header, header_len) != ESP_OK ||
            httpd_resp_send_chunk(req, jpeg_data.data(), jpeg_data.size()) != ESP_OK) {
            break;
        }
    }

    (void)httpd_resp_send_chunk(req, nullptr, 0);
    ESP_LOGI(TAG, "Preview stream closed: sock=%d", sockfd);
    return ESP_OK;
}
