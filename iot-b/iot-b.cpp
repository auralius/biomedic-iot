/**
 * @file iot-b.cpp
 * @brief Implementation of Wi‑Fi & MQTT helpers for ESP32 (Arduino core).
 */

#include "iot-b.h"
#include <cstring>

// -------------------------------------------------------------------------------------------------
// Wi‑Fi event logging (attach once)
// -------------------------------------------------------------------------------------------------
static bool s_wifiEventsAttached = false;

static void attach_wifi_events_once() {
  if (s_wifiEventsAttached) return;
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
    switch (e) {
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        LOGW("[wifi] DISCONNECTED reason=%d", info.wifi_sta_disconnected.reason);
        break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        LOGI("[wifi] CONNECTED  BSSID=%02X:%02X:%02X:%02X:%02X:%02X ch=%u",
             info.wifi_sta_connected.bssid[0], info.wifi_sta_connected.bssid[1],
             info.wifi_sta_connected.bssid[2], info.wifi_sta_connected.bssid[3],
             info.wifi_sta_connected.bssid[4], info.wifi_sta_connected.bssid[5],
             info.wifi_sta_connected.channel);
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        LOGI("[wifi] GOT IP    %s", WiFi.localIP().toString().c_str());
        break;
      default:
        LOGD("[wifi] event=%d", (int)e);
        break;
    }
  });
  s_wifiEventsAttached = true;
}

// -------------------------------------------------------------------------------------------------
// Scan & pick best BSSID for a given SSID
// -------------------------------------------------------------------------------------------------
struct BssidPick { uint8_t bssid[6] = {0}; int32_t channel = 0; int32_t rssi = -999; bool found = false; };

static BssidPick pick_best_bssid_for_ssid(const char* ssid) {
  BssidPick pick;
  const int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
  if (n <= 0) {
    LOGW("[wifi] scan found %d networks", n);
  }
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == ssid) {
      const int32_t r = WiFi.RSSI(i);
      const int32_t ch = WiFi.channel(i);
      const uint8_t* b = WiFi.BSSID(i);
      LOGD("[wifi] candidate %s RSSI=%ld ch=%ld BSSID=%02X:%02X:%02X:%02X:%02X:%02X",
           ssid, (long)r, (long)ch, b[0], b[1], b[2], b[3], b[4], b[5]);
      if (r > pick.rssi) {
        pick.rssi = r;
        pick.channel = ch;
        memcpy(pick.bssid, b, 6);
        pick.found = true;
      }
    }
  }
  WiFi.scanDelete();
  if (pick.found) {
    LOGI("[wifi] best BSSID for \"%s\": RSSI=%ld ch=%ld %02X:%02X:%02X:%02X:%02X:%02X",
         ssid, (long)pick.rssi, (long)pick.channel,
         pick.bssid[0], pick.bssid[1], pick.bssid[2],
         pick.bssid[3], pick.bssid[4], pick.bssid[5]);
  } else {
    LOGW("[wifi] no matching BSSID found for \"%s\"", ssid);
  }
  return pick;
}

// -------------------------------------------------------------------------------------------------
// Personal Wi‑Fi (WPA/WPA2 PSK)
// -------------------------------------------------------------------------------------------------
bool connect_to_home_wifi(const char *ssid, const char *password) {
  if (!ssid || !*ssid) {
    LOGE("empty SSID");
    return false;
  }

  attach_wifi_events_once();

  WiFi.persistent(false);     // avoid NVS churn
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);       // keep radio awake during auth/handshake

  // (Optional) set country (ID = Indonesia; channels 1..13). Adjust for your region.
  wifi_country_t c = { "ID", 1, 13, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&c);

  WiFi.disconnect(true, true);
  delay(200);

  BssidPick pick = pick_best_bssid_for_ssid(ssid);
  if (pick.found) {
    LOGI("Connecting to \"%s\" via best BSSID %02X:%02X:%02X:%02X:%02X:%02X ch=%ld",
         ssid,
         pick.bssid[0], pick.bssid[1], pick.bssid[2],
         pick.bssid[3], pick.bssid[4], pick.bssid[5],
         (long)pick.channel);
    WiFi.begin(ssid, password, pick.channel, pick.bssid, true);
  } else {
    LOGI("Connecting to \"%s\" (generic)", ssid);
    WiFi.begin(ssid, password);
  }

  wl_status_t st = WL_IDLE_STATUS;
  while ((st = WiFi.status()) != WL_CONNECTED) {
    delay(250);
    yield();  // keep WDT calm

    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
      LOGW("quick re-begin due to status=%d", (int)st);
      WiFi.disconnect(false, false);
      delay(150);
      static uint32_t last_scan_ms = 0;
      uint32_t now = millis();
      if (now - last_scan_ms > 10000) {
        pick = pick_best_bssid_for_ssid(ssid);
        last_scan_ms = now;
      }
      if (pick.found) WiFi.begin(ssid, password, pick.channel, pick.bssid, true);
      else            WiFi.begin(ssid, password);
    }
  }

  LOGI("connected. IP=%s RSSI=%d",
       WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// -------------------------------------------------------------------------------------------------
// WPA2‑Enterprise
// -------------------------------------------------------------------------------------------------
bool connect_to_campus_wifi(const char *ssid,
                            const char *username,
                            const char *password,
                            const char *outer_identity,
                            bool lock_to_best_bssid) {
  if (!ssid || !*ssid || !username || !*username || !password || !*password) {
    LOGE("invalid args");
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true, true);
  delay(200);

  wifi_country_t c = { "ID", 1, 13, WIFI_COUNTRY_POLICY_MANUAL };
  esp_wifi_set_country(&c);

  if (outer_identity && *outer_identity)
    ESP_ERROR_CHECK( esp_eap_client_set_identity((const uint8_t*)outer_identity, strlen(outer_identity)) );
  else
    ESP_ERROR_CHECK( esp_eap_client_set_identity((const uint8_t*)username, strlen(username)) );

  ESP_ERROR_CHECK( esp_eap_client_set_username((const uint8_t*)username, strlen(username)) );
  ESP_ERROR_CHECK( esp_eap_client_set_password((const uint8_t*)password, strlen(password)) );

  ESP_ERROR_CHECK( esp_eap_client_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_PAP) );
  ESP_ERROR_CHECK( esp_eap_client_set_ca_cert(nullptr, 0) );
  ESP_ERROR_CHECK( esp_wifi_sta_enterprise_enable() );

  BssidPick pick = {};
  if (lock_to_best_bssid) {
    pick = pick_best_bssid_for_ssid(ssid);
    if (pick.found) {
      LOGI("locking to %s RSSI=%ld dBm BSSID %02X:%02X:%02X:%02X:%02X:%02X ch=%ld",
           ssid, (long)pick.rssi,
           pick.bssid[0], pick.bssid[1], pick.bssid[2],
           pick.bssid[3], pick.bssid[4], pick.bssid[5],
           (long)pick.channel);
      WiFi.begin(ssid, "", pick.channel, pick.bssid, true);
    } else {
      LOGW("scan didn’t find '%s'; generic connect", ssid);
      WiFi.begin(ssid);
    }
  } else {
    LOGI("connecting to '%s' without BSSID lock", ssid);
    WiFi.begin(ssid);
  }

  wl_status_t st;
  uint32_t last_scan_ms = 0;
  while ((st = WiFi.status()) != WL_CONNECTED) {
    delay(1000);
    yield();

    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
      LOGW("quick re-begin due to status=%d", (int)st);
      WiFi.disconnect(false, false);
      delay(150);

      if (lock_to_best_bssid) {
        uint32_t now = millis();
        if (now - last_scan_ms > 10000) {
          pick = pick_best_bssid_for_ssid(ssid);
          last_scan_ms = now;
        }
        if (pick.found) WiFi.begin(ssid, "", pick.channel, pick.bssid, true);
        else            WiFi.begin(ssid);
      } else {
        WiFi.begin(ssid);
      }
    }
  }

  LOGI("connected. IP=%s RSSI=%d",
       WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// -------------------------------------------------------------------------------------------------
// Time sync (SNTP) for TLS
// -------------------------------------------------------------------------------------------------
static bool mqtt_sync_time_internal() {
  static bool ntpConfigured = false;
  if (!ntpConfigured) {
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    ntpConfigured = true;
    LOGI("NTP config issued");
  }

  time_t now = 0;
  time(&now);
  if (now >= kMinGoodEpoch) {
    LOGI("time already synced: %ld", (long)now);
    return true;
  }

  // Loop until SNTP gives a good epoch
  while (true) {
    delay(250);
    yield();
    time(&now);
    if (now >= kMinGoodEpoch) {
      LOGI("time synced: %ld", (long)now);
      return true;
    }
  }
}

bool mqtt_sync_time_if_needed() {
  time_t now = 0;
  time(&now);
  if (now >= kMinGoodEpoch) return true;
  return mqtt_sync_time_internal();
}

// -------------------------------------------------------------------------------------------------
// Secure client configuration
// -------------------------------------------------------------------------------------------------
extern const char hivemq_ca_cert[];

void mqtt_configure_secure_client(WiFiClientSecure& net, bool verify_cert, const char *cert) {
  mqtt_sync_time_if_needed();
  if (verify_cert) {
    net.setCACert(cert);
    LOGI("secure client: CA cert set");
  } else {
    net.setInsecure();
    LOGW("secure client: setInsecure()");
  }
  net.setHandshakeTimeout(15);
}

// -------------------------------------------------------------------------------------------------
// MQTT init (secure/plain)
// -------------------------------------------------------------------------------------------------
void mqtt_init(PubSubClient& client, WiFiClientSecure& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE) {
  client.setClient(net);
  client.setServer(host, port);
  client.setCallback(callback);
  client.setBufferSize(MQTT_BUFFER_SIZE);
  LOGI("MQTT init (secure) host=%s port=%u", host, port);
}

void mqtt_init(PubSubClient& client, WiFiClient& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE) {
  client.setClient(net);
  client.setServer(host, port);
  client.setCallback(callback);
  client.setBufferSize(MQTT_BUFFER_SIZE);
  LOGI("MQTT init (plain) host=%s port=%u", host, port);
}

// -------------------------------------------------------------------------------------------------
// MQTT connect/publish/subscribe/loop
// -------------------------------------------------------------------------------------------------
bool mqtt_connect(PubSubClient& client, const MqttConfig& cfg,
                  uint8_t max_retries, uint32_t backoff_ms) {
  if (!cfg.server || !cfg.client_id) {
    LOGE("mqtt_connect: missing server/client_id");
    return false;
  }

  uint32_t attempt = 1;
  while (true) {
    bool ok = false;

    if (cfg.username && cfg.password) {
      if (cfg.topic && cfg.payload) {
        ok = client.connect(cfg.client_id, cfg.username, cfg.password,
                            cfg.topic, 0, cfg.retain, cfg.payload);
      } else {
        ok = client.connect(cfg.client_id, cfg.username, cfg.password);
      }
    } else {
      if (cfg.topic && cfg.payload) {
        ok = client.connect(cfg.client_id, nullptr, nullptr,
                            cfg.topic, 0, cfg.retain, cfg.payload);
      } else {
        ok = client.connect(cfg.client_id);
      }
    }

    if (ok) {
      LOGI("MQTT connected");
      return true;
    }

    LOGW("MQTT connect failed (state=%d) attempt %lu", client.state(), (unsigned long)attempt);

    if (max_retries != 0 && attempt >= max_retries) {
      LOGE("MQTT connect giving up after %lu attempts", (unsigned long)attempt);
      return false;
    }

    delay(backoff_ms * attempt);
    yield();
    attempt++;
  }
}

bool mqtt_publish(PubSubClient& client, const char* topic,
                  const String& payload, bool retained) {
  if (!topic) {
    LOGE("publish: null topic");
    return false;
  }
  bool ok = client.publish(topic, payload.c_str(), retained);
  if (!ok) LOGW("publish failed (state=%d)", client.state());
  return ok;
}

bool mqtt_subscribe(PubSubClient& client, const char* topic) {
  if (!topic) {
    LOGE("subscribe: null topic");
    return false;
  }
  bool ok = client.subscribe(topic);
  if (!ok) LOGW("subscribe failed for '%s' (state=%d)", topic, client.state());
  else     LOGI("subscribed to '%s'", topic);
  return ok;
}

void mqtt_loop(PubSubClient& client) {
  client.loop();
}

// -------------------------------------------------------------------------------------------------
// Embedded Root CA (example for HiveMQ Cloud). Define here to live in flash.
// -------------------------------------------------------------------------------------------------
// Root CA for secure MQTT (HiveMQ Cloud). Update as needed.
const char hivemq_ca_cert[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

// -------------------------------------------------------------------------------------------------
// Embedded Root CA (example for HiveMQ Cloud). Define here to live in flash.
// -------------------------------------------------------------------------------------------------
// Root CA for secure MQTT (HiveMQ Cloud). Update as needed.
const char emqx_ca_cert[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";