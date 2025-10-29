#include <Arduino.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "iot-b.h"
#include "password.h"

static const char* MQTT_HOST = "bcf21ddabed1412aaf638a09b4732f50.s1.eu.hivemq.cloud";
static const uint16_t MQTT_PORT = 8883;
static const char* MQTT_TOPIC = "audio/mono";

WiFiClientSecure net;
PubSubClient mqtt(net);

// ===== I2S pins & audio =====
static const gpio_num_t PIN_I2S_BCK = GPIO_NUM_26;
static const gpio_num_t PIN_I2S_WS = GPIO_NUM_25;
static const gpio_num_t PIN_I2S_DATA = GPIO_NUM_27;

static const uint32_t FS = 8000;                 // 8 kHz
static const uint32_t HALFSEC_SAMPLES = FS / 2;  // 4000 samples (0.5 s)

i2s_chan_handle_t rx_chan;

void on_mqtt(char*, byte*, unsigned int) {}  // not used

void setup() {
  Serial.begin(115200);
  delay(200);

  // Wi-Fi
  connect_to_campus_wifi(CAMPUS_WIFI_SSID, CAMPUS_WIFI_USER, CAMPUS_WIFI_PASS);
  //connect_to_home_wifi(HOME_WIFI_SSID, HOME_WIFI_PASS);

  // MQTT (TLS)
  mqtt_configure_secure_client(net, true, hivemq_ca_cert);
  mqtt_init(mqtt, net, MQTT_HOST, MQTT_PORT, on_mqtt);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(15);
  mqtt.setBufferSize(256);

  MqttConfig cfg;
  cfg.server = MQTT_HOST;
  cfg.port = MQTT_PORT;
  cfg.client_id = "Device01";
  cfg.username = "Device01";
  cfg.password = "Device01";
  mqtt_connect(mqtt, cfg);

  // I2S RX (TRUE MONO slots)
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(FS),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_32BIT,
      I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = PIN_I2S_BCK,
      .ws = PIN_I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din = PIN_I2S_DATA },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;  // INMP441 L/R LOW

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

// ---------- simple 5-tap LPF state ----------
static int32_t z1 = 0, z2 = 0, z3 = 0, z4 = 0;  // previous raw samples (int16 promoted)

// Binomial 5-tap LPF: y = (x + 4*z1 + 6*z2 + 4*z3 + z4) / 16, with rounding
static inline int16_t lpf5_binomial(int16_t x16) {
  int32_t x = x16;
  int32_t acc = x + (z1 << 2) + (z2 * 6) + (z3 << 2) + z4;
  // rounding for /16
  acc = (acc + 8) >> 4;
  // shift history
  z4 = z3;
  z3 = z2;
  z2 = z1;
  z1 = x;
  // clip to int16
  if (acc > 32767) acc = 32767;
  if (acc < -32768) acc = -32768;
  return (int16_t)acc;
}

void loop() {
  static uint32_t next_ms = 0;
  static uint32_t block_id = 0;
  if (next_ms == 0) next_ms = millis() + 500;

  const size_t CHUNK = 256;  // words per DMA read
  int32_t buf32[CHUNK];
  size_t bytes_read = 0;

  static int16_t block_i16[HALFSEC_SAMPLES];
  size_t collected = 0;

  while (collected < HALFSEC_SAMPLES) {
    if (i2s_channel_read(rx_chan, buf32, sizeof(buf32),
                         &bytes_read, pdMS_TO_TICKS(100))
        != ESP_OK)
      continue;

    size_t words = bytes_read / sizeof(int32_t);
    for (size_t w = 0; w < words && collected < HALFSEC_SAMPLES; ++w) {
      // extract signed 24-bit sample from 32-bit slot
      int32_t v24 = (int32_t)buf32[w] >> 8;
      int16_t s16 = (int16_t)v24;  // 24→16
      // low-pass filter it
      int16_t y16 = lpf5_binomial(s16);
      block_i16[collected++] = y16;
    }
  }

  // publish: 4B block_id + 4000×int16 (8000 bytes)
  mqtt_loop(mqtt);
  mqtt_publish_stream_2seg(
    mqtt,
    MQTT_TOPIC,
    (const uint8_t*)&block_id, sizeof(block_id),
    (const uint8_t*)block_i16, sizeof(block_i16),
    /*retained=*/false,
    /*chunk_bytes=*/256);
  block_id++;

  // steady 0.5 s cadence
  int32_t wait = (int32_t)next_ms - (int32_t)millis();
  if (wait > 0) delay(wait);
  next_ms += 500;
}
