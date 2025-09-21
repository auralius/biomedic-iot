#include "iot-b.h"

const char* WIFI_SSID = "Polo";
const char* WIFI_PASS = "pabl0picas0";

//const char* MQTT_HOST = "bcf21ddabed1412aaf638a09b4732f50.s1.eu.hivemq.cloud";
const char * MQTT_HOST = "d1220813.ala.asia-southeast1.emqxsl.com";
const uint16_t MQTT_PORT_TLS = 8883;

WiFiClientSecure net;   // or WiFiClient net for anonymous/port 1883
PubSubClient mqtt(net);

MqttConfig cfg;

// Call back when message arrives
void on_mqtt(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; ++i) msg += (char)payload[i];
  Serial.printf("MQTT[%s] %s\n", topic, msg.c_str());
}

void setup() {
  Serial.begin(115200);
  connect_to_home_wifi(WIFI_SSID, WIFI_PASS);

  // Secure client
  mqtt_configure_secure_client(net, true, emqx_ca_cert);
  mqtt_init(mqtt, net, MQTT_HOST, MQTT_PORT_TLS, on_mqtt);

  cfg.server = MQTT_HOST;
  cfg.port = MQTT_PORT_TLS;
  cfg.client_id = "Device01";
  cfg.username = "Device01";
  cfg.password = "Device01";

  while(!mqtt_connect(mqtt, cfg)) {
    Serial.println("mqtt_connect fails, retrying...");
    delay(1000);
  }

  mqtt_subscribe(mqtt, "Device01/#");
  mqtt_publish(mqtt, "Device01/status", "Device 1 online", true);
}

void loop() {
  static unsigned long last_pub_ms = 0;     // 1 s ticker
  static unsigned long last_reconn_ms = 0;  // throttle reconnect attempts
  static int k = 0;
  
  // 1) Reconnect (and resubscribe) when needed, but don't spam the broker
  if (!mqtt.connected()) {
    // try reconnect (reusing cfg)
    mqtt_connect(mqtt, cfg);
    mqtt_subscribe(mqtt, "Device01/#");
  }

  // 2) Always service MQTT as fast as possible
  mqtt_loop(mqtt);
  
  // 3) Publish exactly once per second
  unsigned long now = millis();
  if (now - last_pub_ms >= 1000UL) {
    char message[32];
    snprintf(message, sizeof(message), "Pesan ke-%d.", k);
    mqtt_publish(mqtt, "Device01/topic1", message, true);
    k++;
    last_pub_ms = now;
  }
}

