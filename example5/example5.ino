// Lembar Kerja 2
// Auralius Manurung and ChatGPT

#include "iot-b.h"
#include <math.h>
#include <Arduino.h>

#include <Wire.h>
#include <Adafruit_MPU6050.h>

#define SDA_PIN       21
#define SCL_PIN       22
#define I2C_FREQ      100000UL

Adafruit_MPU6050 mpu;

// -------- WiFi / MQTT config --------
//const char* WIFI_SSID = "TelU-Connect";
//const char* WIFI_USER = "badarawuhi";
//const char* WIFI_PSWD = "badarawuhi.badabass";
const char* WIFI_SSID = "Polo";
const char* WIFI_PSWD = "pabl0picas0";

const char* MQTT_HOST = "bcf21ddabed1412aaf638a09b4732f50.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT_TLS = 8883;

WiFiClientSecure net;
PubSubClient mqtt(net);

MqttConfig cfg;

// -------- Topics --------
const char* ACC_TOPIC    = "Device01/acc";
const char* GYRO_TOPIC   = "Device01/gyro";
const char* STATUS_TOPIC = "Device01/status";

// -------- Batch config --------
const uint16_t FS         = 250;             // samples per second
const uint32_t DT_US      = 1000000UL / FS;  // period in microseconds
const uint16_t BATCH_SIZE = 40;              // 0.5 s per packet

// Data frame
#pragma pack(push, 1)
struct Frame {
  float x[BATCH_SIZE];
  float y[BATCH_SIZE];
  float z[BATCH_SIZE];
};
#pragma pack(pop)

// -------- MQTT callback (optional) --------
void on_mqtt(char* topic, byte* payload, unsigned int length) {
  LOGI("MQTT[%s] %u bytes", topic, length);
}

void setup() {
  Serial.begin(115200);
  delay(50);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ);
  Wire.setTimeout(100);

  // Init MPU6050
  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("MPU6050 init failed");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  randomSeed(esp_random());

  //connect_to_campus_wifi(WIFI_SSID, WIFI_USER, WIFI_PSWD);  // WiFi TelU-Connect
  connect_to_home_wifi(WIFI_SSID, WIFI_PSWD);

  mqtt_configure_secure_client(net, true, hivemq_ca_cert);
  mqtt_init(mqtt, net, MQTT_HOST, MQTT_PORT_TLS, on_mqtt);

  // Payload ~254 bytes + topic/etc -> give headroom
  mqtt.setBufferSize(512);

  cfg.server    = MQTT_HOST;
  cfg.port      = MQTT_PORT_TLS;
  cfg.client_id = "Device01";
  cfg.username  = "Device01";
  cfg.password  = "Device01";

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

  static Frame acc_frame;   
  static Frame gyro_frame; 
  static uint16_t n = 0;  // how many samples filled

  // keep MQTT alive / reconnect if needed
  if (!mqtt.connected()) {
    if (mqtt_connect(mqtt, cfg)) {
      mqtt_publish(mqtt, STATUS_TOPIC, "Device 1 reconnected (250 Hz)", true);
    }
  }
  mqtt_loop(mqtt);

  sensors_event_t a,g,t; 
  mpu.getEvent(&a,&g,&t);

  uint32_t now_us = micros();
  if ((uint32_t)(now_us - last_us) >= DT_US) { // every 4ms
    last_us += DT_US;  // fixed-interval stepping (handles wrap)

    if (n == 0) {  // mark batch start at first sample
      batch_start_us = now_us;
    }

    acc_frame.x[n] = a.acceleration.x;
    acc_frame.y[n] = a.acceleration.y;
    acc_frame.z[n] = a.acceleration.z;

    gyro_frame.x[n] = g.gyro.x;
    gyro_frame.y[n] = g.gyro.y;
    gyro_frame.z[n] = g.gyro.z;

    n = n + 1;

    // publish when batch full
    if (n >= BATCH_SIZE) {
      mqtt_publish(mqtt,
                   ACC_TOPIC,
                   (const byte*) &acc_frame,
                   sizeof(acc_frame),
                   /*retain*/ false);
      mqtt_publish(mqtt,
                   GYRO_TOPIC,
                   (const byte*) &gyro_frame,
                   sizeof(gyro_frame),
                   /*retain*/ false);
      n = 0;
    }
  }
}
