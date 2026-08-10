#include "wifi_export.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "dns_server.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "png.h"
#include "tinydraw/geometry.h"

namespace tinydraw::esp32 {
namespace {

constexpr char kSsid[] = "TinyDraw";
constexpr std::size_t kPixelCount = static_cast<std::size_t>(kCanvasWidth * kCanvasHeight);

std::span<const std::uint16_t> export_canvas;
std::array<png_byte, static_cast<std::size_t>(kCanvasWidth * 3)> png_row;

constexpr char kPage[] = R"HTML(<!doctype html>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TinyDraw</title>
<style>
body{font-family:-apple-system,sans-serif;margin:0;padding:24px;background:#f6f6f6;color:#202020;text-align:center}
main{max-width:520px;margin:auto}img{display:block;width:100%;height:auto;background:white;border:1px solid #ccc;border-radius:12px}
a,button{display:inline-block;margin:18px 5px;padding:14px 20px;border:0;border-radius:10px;background:#2f80ed;color:white;font:inherit;text-decoration:none}
p{line-height:1.4}
</style><main><h1>TinyDraw</h1><p>Long-press the drawing and choose <b>Save to Photos</b>.</p>
<img id="drawing" src="/drawing.png" alt="TinyDraw drawing">
<a href="/drawing.png" download="tinydraw.png">Open image</a><button onclick="drawing.src='/drawing.png?t='+Date.now()">Refresh</button></main>)HTML";

struct PngWriteContext {
  httpd_req_t* request = nullptr;
  bool failed = false;
};

void png_write(png_structp png, png_bytep data, png_size_t length) {
  auto& context = *static_cast<PngWriteContext*>(png_get_io_ptr(png));
  if (httpd_resp_send_chunk(context.request, reinterpret_cast<const char*>(data), length) !=
      ESP_OK) {
    context.failed = true;
    png_error(png, "HTTP write failed");
  }
}

void png_flush(png_structp) {}

png_voidp png_allocate(png_structp, png_alloc_size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void png_release(png_structp, png_voidp memory) { heap_caps_free(memory); }

esp_err_t send_page(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, kPage, HTTPD_RESP_USE_STRLEN);
}

esp_err_t root_handler(httpd_req_t* request) { return send_page(request); }

esp_err_t drawing_handler(httpd_req_t* request) {
  if (export_canvas.size() < kPixelCount) {
    return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Drawing unavailable");
  }

  httpd_resp_set_type(request, "image/png");
  httpd_resp_set_hdr(request, "Content-Disposition", "inline; filename=\"tinydraw.png\"");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");

  png_structp png = png_create_write_struct_2(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr,
                                              nullptr, png_allocate, png_release);
  png_infop info = png == nullptr ? nullptr : png_create_info_struct(png);
  if (png == nullptr || info == nullptr) {
    png_destroy_write_struct(&png, &info);
    return ESP_FAIL;
  }

  PngWriteContext context{.request = request};
  if (setjmp(png_jmpbuf(png)) != 0) {
    png_destroy_write_struct(&png, &info);
    return context.failed ? ESP_FAIL
                          : httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                                "PNG encoding failed");
  }

  png_set_write_fn(png, &context, png_write, png_flush);
  png_set_IHDR(png, info, kCanvasWidth, kCanvasHeight, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_FILTER_NONE);
  png_set_compression_level(png, 1);
  png_write_info(png, info);

  for (int y = 0; y < kCanvasHeight; ++y) {
    for (int x = 0; x < kCanvasWidth; ++x) {
      const std::uint16_t pixel = export_canvas[static_cast<std::size_t>(y * kCanvasWidth + x)];
      const std::uint8_t red = static_cast<std::uint8_t>((pixel >> 11U) & 0x1FU);
      const std::uint8_t green = static_cast<std::uint8_t>((pixel >> 5U) & 0x3FU);
      const std::uint8_t blue = static_cast<std::uint8_t>(pixel & 0x1FU);
      const auto offset = static_cast<std::size_t>(x * 3);
      png_row[offset] = static_cast<png_byte>((red << 3U) | (red >> 2U));
      png_row[offset + 1U] = static_cast<png_byte>((green << 2U) | (green >> 4U));
      png_row[offset + 2U] = static_cast<png_byte>((blue << 3U) | (blue >> 2U));
    }
    png_write_row(png, png_row.data());
  }
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t not_found_handler(httpd_req_t* request, httpd_err_code_t) { return send_page(request); }

bool initialize_networking() {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() != ESP_OK) {
      return false;
    }
    result = nvs_flash_init();
  }
  if (result != ESP_OK) {
    return false;
  }
  result = esp_netif_init();
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return false;
  }
  result = esp_event_loop_create_default();
  return result == ESP_OK || result == ESP_ERR_INVALID_STATE;
}

}  // namespace

bool start_wifi_export(std::span<const std::uint16_t> canvas) {
  if (canvas.size() < kPixelCount || !initialize_networking() ||
      esp_netif_create_default_wifi_ap() == nullptr) {
    return false;
  }
  export_canvas = canvas.first(kPixelCount);

  wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&initialization) != ESP_OK) {
    return false;
  }
  wifi_config_t configuration{};
  std::memcpy(configuration.ap.ssid, kSsid, sizeof(kSsid) - 1U);
  configuration.ap.ssid_len = sizeof(kSsid) - 1U;
  configuration.ap.channel = 1;
  configuration.ap.max_connection = 2;
  configuration.ap.authmode = WIFI_AUTH_OPEN;
  if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_AP, &configuration) != ESP_OK || esp_wifi_start() != ESP_OK) {
    return false;
  }

  httpd_config_t server_configuration = HTTPD_DEFAULT_CONFIG();
  server_configuration.stack_size = 8192;
  server_configuration.max_open_sockets = 4;
  server_configuration.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &server_configuration) != ESP_OK) {
    return false;
  }
  const httpd_uri_t root{
      .uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = nullptr};
  const httpd_uri_t drawing{
      .uri = "/drawing.png", .method = HTTP_GET, .handler = drawing_handler, .user_ctx = nullptr};
  if (httpd_register_uri_handler(server, &root) != ESP_OK ||
      httpd_register_uri_handler(server, &drawing) != ESP_OK ||
      httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler) != ESP_OK) {
    return false;
  }

  dns_server_config_t dns_configuration{};
  dns_configuration.num_of_entries = 1;
  dns_configuration.item[0].name = "*";
  dns_configuration.item[0].if_key = "WIFI_AP_DEF";
  if (start_dns_server(&dns_configuration) == nullptr) {
    return false;
  }
  esp_log_level_set("example_dns_redirect_server", ESP_LOG_ERROR);
  std::printf(
      "TINYDRAW_EXPORT_OK ssid=%s url=http://192.168.4.1 internal_free=%lu psram_free=%lu\n", kSsid,
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  return true;
}

}  // namespace tinydraw::esp32
