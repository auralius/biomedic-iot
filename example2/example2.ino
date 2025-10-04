// Lembar Kerja 2
// Auralius Manurung and ChatGPT

#include "iot-b.h"
#include <math.h>
#include <Arduino.h>

// -------- WiFi / MQTT config --------

const char* WIFI_SSID = "TelU-Connect";
const char* WIFI_USER = "auraliusoberman";
const char* WIFI_PSWD = "Lenovo@x270";

const char* MQTT_HOST = "4a48091aa0ac475d93f3f294b735ba3d.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT_TLS = 8883;

WiFiClientSecure net;
PubSubClient mqtt(net);

MqttConfig cfg;

// -------- Topics --------
const char* ECG_TOPIC = "Device01/ecg-minbin";
const char* STATUS_TOPIC = "Device01/status";

// -------- ECG config for ML --------
const uint16_t ECG_FS = 250;                    // samples per second (ML sweet spot)
const uint32_t ECG_DT_US = 1000000UL / ECG_FS;  // period in microseconds
const uint16_t BATCH_SIZE = 125;                // 0.5 s per packet

volatile float ECG_HR_BPM = 72.0f;
const float ECG_NOISE_MV = 0.01f;  // ~10 µV noise

// -------- ECG model --------
static inline float gauss(float x, float mu, float sigma) {
  float z = (x - mu) / sigma;
  return expf(-0.5f * z * z);
}

float ecg_model_phase(float phase, float t_seconds) {
  float baseline = 0.05f * sinf(2.0f * M_PI * 0.33f * t_seconds);
  float p = 0.10f * gauss(phase, 0.20f, 0.025f);
  float q = -0.15f * gauss(phase, 0.25f, 0.010f);
  float r = 1.00f * gauss(phase, 0.30f, 0.012f);
  float s = -0.25f * gauss(phase, 0.35f, 0.012f);
  float t = 0.35f * gauss(phase, 0.55f, 0.050f);
  return baseline + p + q + r + s + t;
}

float ecg_sample(float t_seconds, float bpm) {
  float period = 60.0f / fmaxf(bpm, 30.0f);
  float phase = fmodf(t_seconds, period) / period;
  float y = ecg_model_phase(phase, t_seconds);
  float noise = ((float)random(-100, 101) / 100.0f) * ECG_NOISE_MV;
  return y + noise;  // mV
}

// -------- Wire format: [ float ts_sec | BATCH_SIZE x int16 (µV) ] --------
#pragma pack(push, 1)
struct EcgFrame {
  float ts_sec;                 // start time (seconds) of FIRST sample in the batch
  int16_t samples[BATCH_SIZE];  // raw ECG in microvolts (µV)
};
#pragma pack(pop)

// -------- MQTT callback (optional) --------
void on_mqtt(char* topic, byte* payload, unsigned int length) {
  LOGI("MQTT[%s] %u bytes\n", topic, length);
}

void setup() {
  Serial.begin(115200);
  delay(50);

  randomSeed(esp_random());

  connect_to_campus_wifi(WIFI_SSID, WIFI_USER, WIFI_PSWD);  // WiFi TelU-Connect
  //connect_to_home_wifi(WIFI_SSID, WIFI_PSWD);

  mqtt_configure_secure_client(net, true, hivemq_ca_cert);
  mqtt_init(mqtt, net, MQTT_HOST, MQTT_PORT_TLS, on_mqtt);

  // Payload ~254 bytes + topic/etc -> give headroom
  mqtt.setBufferSize(512);

  cfg.server = MQTT_HOST;
  cfg.port = MQTT_PORT_TLS;
  cfg.client_id = "Device01";
  cfg.username = "Device01";
  cfg.password = "Device01";

  while (!mqtt_connect(mqtt, cfg)) {
    Serial.println("mqtt_connect fails, retrying...");
    delay(1000);
  }
  mqtt_publish(mqtt, STATUS_TOPIC, "Device 1 online (250 Hz, 0.5 s/frame)", true);
}

void loop() {
  static uint32_t last_us = micros();
  static uint64_t start_us_epoch = micros();  // reference start (for t_seconds)
  static uint32_t batch_start_us = 0;

  static EcgFrame frame;  // single buffer (header + samples)
  static uint16_t n = 0;  // how many samples filled

  // keep MQTT alive / reconnect if needed
  if (!mqtt.connected()) {
    if (mqtt_connect(mqtt, cfg)) {
      mqtt_publish(mqtt, STATUS_TOPIC, "Device 1 reconnected (250 Hz)", true);
    }
  }
  mqtt_loop(mqtt);

  uint32_t now_us = micros();
  if ((uint32_t)(now_us - last_us) >= ECG_DT_US) { // every 4ms
    last_us += ECG_DT_US;  // fixed-interval stepping (handles wrap)

    if (n == 0) {  // mark batch start at first sample
      batch_start_us = now_us;
      frame.ts_sec = (batch_start_us - (uint32_t)start_us_epoch) / 1e6f;  // seconds since start
    }

    // Generate one ECG sample at current time (seconds since start)
    float t_sec = (now_us - (uint32_t)start_us_epoch) / 1e6f;
    float mv = ecg_sample(t_sec, ECG_HR_BPM);

    // mV -> µV (int16), clamp
    long uv = lroundf(mv * 1000.0f);
    if (uv < -32768) uv = -32768;
    if (uv > 32767) uv = 32767;
    frame.samples[n++] = (int16_t)uv;

    // publish when batch full
    if (n >= BATCH_SIZE) {
      mqtt_publish(mqtt,
                   ECG_TOPIC,
                   (const byte*) &frame,
                   sizeof(frame),
                   /*retain*/ false);
      n = 0;
    }
  }
}
