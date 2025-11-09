// LK3_cam_VGA_RGB565_toJPEG_mqtt_nohdr.ino
// ESP32-S3 EYE → HiveMQ (TLS)
// Capture RGB565 @ VGA, convert to JPEG in software, publish one JPEG per MQTT message.
// Payload = [JPEG bytes only] (no header)

#include <Arduino.h>
#include <esp_system.h>
#include <esp_camera.h>
#include <PubSubClient.h>

// 👉 This header is part of the esp32-camera component and provides frame2jpg/fmt2jpg.
#include "img_converters.h"

#include "iot-b.h"        // your Wi-Fi/MQTT helpers + hivemq_ca_cert + mqtt_publish_stream_2seg(...)
#include "camera_pins.h"  // cam_fill_pins(...)

static const char* WIFI_SSID        = "Polo";
static const char* WIFI_PSWD        = "pabl0picas0";
static const char* MQTT_HOST        = "4a48091aa0ac475d93f3f294b735ba3d.s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT_TLS = 8883;

static const char* TOPIC_JPEG   = "Device01/cam-jpeg";
static const char* STATUS_TOPIC = "Device01/status";

// Gentle pacing (aim ~5 FPS)
static constexpr uint32_t FRAME_MIN_PERIOD_MS = 500;

// MQTT knobs
static constexpr uint32_t MQTT_SOCKET_TIMEOUT_S = 5;
static constexpr uint16_t MQTT_KEEPALIVE_S      = 60;
static constexpr size_t   MQTT_BUFFER_BYTES     = 256; // we stream payload, small buffer is fine

WiFiClientSecure net;
PubSubClient     mqtt(net);
MqttConfig       cfg;

// JPEG quality (lower number = higher quality = bigger file)
static uint8_t g_jpg_quality = 64;  // Try 14–18; adjust for your link

static void on_mqtt(char*, byte*, unsigned int) { /* no-op */ }

void setup() {
  Serial.begin(115200);
  delay(50);

  // Wi-Fi
  connect_to_home_wifi(WIFI_SSID, WIFI_PSWD);

  // TLS + MQTT
  mqtt_configure_secure_client(net, /*verify_cert=*/true, hivemq_ca_cert);
  mqtt_init(mqtt, net, MQTT_HOST, MQTT_PORT_TLS, on_mqtt);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);

  cfg.server    = MQTT_HOST;
  cfg.port      = MQTT_PORT_TLS;
  cfg.client_id = "Device01-cam";
  cfg.username  = "Device01";
  cfg.password  = "Device01";

  while (!mqtt_connect(mqtt, cfg)) {
    Serial.printf("MQTT connect failed (state=%d), retrying...\n", mqtt.state());
    delay(500);
  }
  mqtt_publish(mqtt, STATUS_TOPIC, "Device online (RGB565→JPEG, no header)", true);

  // ── Camera: RAW (RGB565) + VGA ──────────────────────────────────────────────
  camera_config_t cam;
  cam_fill_pins(cam);

  if (esp_camera_init(&cam) != ESP_OK) {
    while (true) { Serial.println("Camera init failed"); delay(1000); }
  }

  if (sensor_t* s = esp_camera_sensor_get()) {
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
  }
}

void loop() {
  // Keep MQTT alive; block until reconnected if needed
  if (!mqtt.connected()) {
    mqtt_connect(mqtt, cfg, 1);
    mqtt_publish(mqtt, STATUS_TOPIC, "Reconnected!", true);
  }
  mqtt_loop(mqtt);

  // Grab RAW frame (RGB565)
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  // Convert RAW → JPEG in software
  uint8_t* jpg_buf = nullptr;
  size_t   jpg_len = 0;

  bool ok_conv = false;
  // Easiest path when you already have a framebuffer object:
  // frame2jpg supports RGB565/GRAYSCALE/YUV422 sources.
  ok_conv = frame2jpg(fb, g_jpg_quality, &jpg_buf, &jpg_len);

  if (!ok_conv || !jpg_buf || jpg_len == 0) {
    Serial.println("frame2jpg failed");
    esp_camera_fb_return(fb);
    delay(FRAME_MIN_PERIOD_MS);
    return;
  }

  // Publish: payload is JPEG bytes only (no header)
  const bool ok_pub = mqtt_publish_stream(
    mqtt, TOPIC_JPEG,
    jpg_buf, jpg_len,      // seg2: JPEG
    /*retained=*/false,
    /*chunk_bytes=*/512
  );

  // Return resources
  esp_camera_fb_return(fb);
  free(jpg_buf);           // frame2jpg mallocs the output buffer — you must free it.

  // Gentle pacing
  delay(FRAME_MIN_PERIOD_MS);
}
