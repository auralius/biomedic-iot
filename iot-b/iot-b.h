#pragma once
/**
 * @file iot-b.h
 * @brief Wi‑Fi & MQTT helper utilities for ESP32 (Arduino core).
 *
 * This header provides helpers to connect to personal (WPA/WPA2 PSK)
 * and campus-style WPA2‑Enterprise networks, plus  wrappers around
 * PubSubClient for secure (TLS) and plain MQTT connections.
 *
 * @author Auralius Manurung and ChatGPT
 * @version 1.0
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_eap_client.h>
#include <time.h>

// -------------------------------------------------------------------------------------------------
// Logging helpers (function-name tagged)
// -------------------------------------------------------------------------------------------------
/** @brief Error log (tag = current function). */
#define LOGE(...) ESP_LOGE(__func__, __VA_ARGS__)
/** @brief Warning log (tag = current function). */
#define LOGW(...) ESP_LOGW(__func__, __VA_ARGS__)
/** @brief Info log (tag = current function). */
#define LOGI(...) ESP_LOGI(__func__, __VA_ARGS__)
/** @brief Debug log (tag = current function). */
#define LOGD(...) ESP_LOGD(__func__, __VA_ARGS__)
/** @brief Verbose log (tag = current function). */
#define LOGV(...) ESP_LOGV(__func__, __VA_ARGS__)

#ifndef MQTT_BUFFER_SIZE
/// Default MQTT buffer size for PubSubClient (bytes).
#define MQTT_BUFFER_SIZE 1024
#endif

// -------------------------------------------------------------------------------------------------
// Time & Certificates
// -------------------------------------------------------------------------------------------------

/** @brief Minimum acceptable UNIX epoch (2023‑01‑01). */
static const time_t kMinGoodEpoch = 1672531200;

/**
 * @brief Root CA for secure MQTT (HiveMQ and EMQX Cloud example). Update for your broker as needed.
 *
 * CA is kept in flash as const. PubSubClient + WiFiClientSecure will read it during TLS setup.
 */
extern const char hivemq_ca_cert[];
extern const char emqx_ca_cert[];

// -------------------------------------------------------------------------------------------------
// Wi‑Fi helpers
// -------------------------------------------------------------------------------------------------

/**
 * @brief Connect to a personal/home Wi‑Fi network (WPA/WPA2 PSK).
 *
 * Strategy:
 * 1. Perform a scan for the target SSID and pick the strongest BSSID (AP) and channel.
 * 2. Attempt association locked to that BSSID+channel, with fallback to generic connect.
 * 3. Retry on transient failures until connected (blocking).
 *
 * @param ssid     Wi‑Fi SSID (non-null, non-empty).
 * @param password Wi‑Fi password.
 * @return true on successful connection (function blocks until connected), false on invalid args.
 */
bool connect_to_home_wifi(const char *ssid, const char *password);

/**
 * @brief Connect to a campus/enterprise WPA2 network using EAP (TTLS/PEAP style).
 *
 * Requires esp_eap_client to be present in the Arduino-ESP32 core.
 * If @p lock_to_best_bssid is true, the function will scan and lock to the best BSSID.
 *
 * @param ssid            Enterprise SSID.
 * @param username        EAP inner identity/username.
 * @param password        EAP password.
 * @param outer_identity  Optional "anonymous" outer identity (if empty, @p username is used).
 * @param lock_to_best_bssid Whether to scan/lock to strongest BSSID for the SSID.
 * @return true on successful connection (function blocks until connected), false on invalid args.
 */
bool connect_to_campus_wifi(const char *ssid,
                            const char *username,
                            const char *password,
                            const char *outer_identity = nullptr,
                            bool lock_to_best_bssid = true);

// -------------------------------------------------------------------------------------------------
// MQTT helpers (PubSubClient)
// -------------------------------------------------------------------------------------------------

/**
 * @brief Configuration struct for MQTT connect.
 */
struct MqttConfig {
  const char* server    = nullptr;   //!< MQTT broker hostname.
  uint16_t    port      = 8883;      //!< Port: 8883 (TLS) or 1883 (plaintext).
  const char* client_id = nullptr;   //!< Client ID (required).
  const char* username  = nullptr;   //!< Username (optional).
  const char* password  = nullptr;   //!< Password (optional).

  // Optional Last Will
  const char* topic     = nullptr;   //!< Last Will topic (optional).
  const char* payload   = nullptr;   //!< Last Will payload (optional).
  bool        retain    = true;      //!< Last Will retain flag.
};

/**
 * @brief Ensure system time is sane via SNTP, useful before TLS handshakes.
 *
 * Safe to call multiple times. If time is already sane (>= kMinGoodEpoch), returns immediately.
 * Blocks until time is synced otherwise.
 *
 * @return true if time is already sane or gets synced successfully.
 */
bool mqtt_sync_time_if_needed();

/**
 * @brief Configure @ref WiFiClientSecure with either a CA certificate or setInsecure().
 *
 * Call after Wi‑Fi has connected. If @p verify_cert is true, a compiled-in Root CA
 * will be installed; otherwise setInsecure() is used (not recommended for production).
 *
 * @param net          Secure client reference.
 * @param verify_cert  Whether to enable CA verification.
 */
void mqtt_configure_secure_client(WiFiClientSecure& net, bool verify_cert = true, const char* cert = hivemq_ca_cert);

/**
 * @brief Initialize a secure MQTT client.
 *
 * Sets the network client, server host/port, callback, and buffer size.
 */
void mqtt_init(PubSubClient& client, WiFiClientSecure& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE);

/**
 * @brief Initialize a plaintext MQTT client.
 *
 * Sets the network client, server host/port, callback, and buffer size.
 */
void mqtt_init(PubSubClient& client, WiFiClient& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE);

/**
 * @brief Connect to MQTT broker with linear backoff.
 *
 * If @p max_retries == 0, retries forever. Otherwise attempts up to @p max_retries times.
 *
 * @param client      PubSubClient instance.
 * @param cfg         Connection configuration.
 * @param max_retries Maximum tries (0 = infinite).
 * @param backoff_ms  Delay between attempts multiplied by current attempt number.
 * @return true on successful connection, false if max retries exceeded or invalid cfg.
 */
bool mqtt_connect(PubSubClient& client, const MqttConfig& cfg,
                  uint8_t max_retries = 10, uint32_t backoff_ms = 500);

/**
 * @brief Publish a message.
 * @param client   PubSubClient.
 * @param topic    Topic string.
 * @param payload  Message payload (Arduino String).
 * @param retained Retain flag.
 * @return true on success.
 */
bool mqtt_publish(PubSubClient& client, const char* topic,
                  const String& payload, bool retained = true);

/**
 * @brief Subscribe to a topic.
 * @return true on success.
 */
bool mqtt_subscribe(PubSubClient& client, const char* topic);

/**
 * @brief Run PubSubClient loop (call frequently in your main loop).
 */
void mqtt_loop(PubSubClient& client);

