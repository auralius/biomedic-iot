#pragma once
/**
 * @file iot-b.h
 * @brief Tiny Wi‑Fi + MQTT helper layer for ESP32/Arduino sketches (TLS and plaintext).
 *
 * This header exposes a small set of utilities to bring Wi‑Fi online, set up
 * MQTT (either over TLS with @c WiFiClientSecure or plaintext with @c WiFiClient),
 * and publish data efficiently (including chunked/streamed payloads).
 *
 * It also includes a couple of RTP/UDP helpers used in some examples.
 *
 * The functions are documented with Doxygen tags so your IDE and generated docs
 * will show concise parameter and return info.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <time.h>

// ===== Logging shorthands ======================================================
// =================================================================================================
// \defgroup logging Logging macros
// Lightweight wrappers over ESP‑IDF logging, auto‑tagged with the current function name.
// -------------------------------------------------------------------------------------------------
// \addtogroup logging
// @{ 
// @brief Error log (tag = current function).
#define LOGE(...) ESP_LOGE(__func__, __VA_ARGS__)
// @brief Warning log (tag = current function).
#define LOGW(...) ESP_LOGW(__func__, __VA_ARGS__)
// @brief Info log (tag = current function).
#define LOGI(...) ESP_LOGI(__func__, __VA_ARGS__)
// @brief Debug log (tag = current function).
#define LOGD(...) ESP_LOGD(__func__, __VA_ARGS__)
// @brief Verbose log (tag = current function).
#define LOGV(...) ESP_LOGV(__func__, __VA_ARGS__)
// @} // end of logging group

// ===== MQTT buffer size used by mqtt_init =====================================
#ifndef MQTT_BUFFER_SIZE
#define MQTT_BUFFER_SIZE 1024
#endif

// ===== Minimal connection config ==============================================
/**
 * @brief Minimal connection/session configuration for @ref mqtt_connect.
 *
 * These fields are passed directly to PubSubClient during connect, including an
 * optional Last Will and Testament (LWT).
 */
struct MqttConfig {
  const char* server    = nullptr;  ///< MQTT broker hostname or IP.
  uint16_t    port      = 0;        ///< Broker port (e.g., 1883 or 8883).
  const char* client_id = nullptr;  ///< Client ID string (must be unique per broker).
  const char* username  = nullptr;  ///< Optional username for authentication.
  const char* password  = nullptr;  ///< Optional password for authentication.
  const char* topic     = nullptr;  ///< Optional LWT topic.
  const char* payload   = nullptr;  ///< Optional LWT payload.
  bool        retain    = false;    ///< LWT retain flag.
};

// ===== Wi‑Fi helpers ===========================================================
/**
 * @brief Connect to a standard WPA/WPA2 home Wi‑Fi network.
 *
 * @param ssid  Access point SSID.
 * @param pswd  Access point password.
 * @param lock_to_best_bssid Reserved/unused (kept for API compatibility).
 * @return true on successful association and DHCP; false on timeout/failure.
 *
 * @note This is a convenience wrapper; you may still call @c WiFi.begin directly.
 */
bool connect_to_home_wifi(const char* ssid, const char* pswd,
                          bool lock_to_best_bssid = false);

// ===== Time sync for TLS =======================================================
/**
 * @brief Ensure SNTP time is sane before using TLS (avoids “cert not yet valid”).
 *
 * If the current epoch is older than a threshold (e.g., 2023‑01‑01), this function
 * triggers SNTP and waits until time is synced.
 *
 * @return true if time is already valid or becomes valid; false otherwise.
 */
bool mqtt_sync_time_if_needed();

// ===== TLS config (CA or insecure) ============================================
/**
 * @brief PEM‑encoded CA certificates for common demo brokers (if you need them).
 *
 * Include your own CA bundle in production. These are declared here; define them in
 * your @c iot-b.cpp or another TU.
 */
extern const char hivemq_ca_cert[];
extern const char emqx_ca_cert[];

/**
 * @brief Configure a @c WiFiClientSecure for verified or insecure TLS.
 *
 * @param net         The secure client to configure.
 * @param verify_cert When true, set the provided @p cert as CA root. When false, use @c setInsecure().
 * @param cert        PEM‑encoded CA certificate (null‑terminated). Ignored if @p verify_cert is false.
 *
 * @note This function also ensures time is synced so certificate validation won’t fail due to bad RTC.
 */
void mqtt_configure_secure_client(WiFiClientSecure& net,
                                  bool verify_cert,
                                  const char* cert);

// ===== MQTT init (TLS & plaintext) ============================================
/**
 * @brief Bind a @c PubSubClient to a @c WiFiClientSecure and set broker/handlers.
 *
 * @param client   PubSubClient instance to configure.
 * @param net      The underlying secure transport.
 * @param host     Broker hostname or IP.
 * @param port     Broker port (typically 8883 for TLS).
 * @param callback Optional message callback (use nullptr if unused).
 */
void mqtt_init(PubSubClient& client, WiFiClientSecure& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE);

/**
 * @brief Bind a @c PubSubClient to a plaintext @c WiFiClient and set broker/handlers.
 *
 * @param client   PubSubClient instance to configure.
 * @param net      The underlying plaintext transport.
 * @param host     Broker hostname or IP.
 * @param port     Broker port (typically 1883 for plaintext).
 * @param callback Optional message callback (use nullptr if unused).
 */
void mqtt_init(PubSubClient& client, WiFiClient& net,
               const char* host, uint16_t port, MQTT_CALLBACK_SIGNATURE);

// ===== MQTT connect/publish/loop ==============================================
/**
 * @brief Connect to the broker using the provided configuration, with simple retry/backoff.
 *
 * @param client       The PubSubClient to connect.
 * @param cfg          Connection/session parameters (host is already set by @ref mqtt_init).
 * @param max_retries  Maximum attempts before giving up. Use 0 for infinite retries.
 * @param backoff_ms   Linear backoff in milliseconds between retries (multiplied by attempt count).
 * @return true if connected; false if it ultimately failed.
 */
bool mqtt_connect(PubSubClient& client, const MqttConfig& cfg,
                  uint8_t max_retries = 10, uint32_t backoff_ms = 500);

/**
 * @brief Publish a small text payload (convenience overload).
 *
 * @param client   PubSubClient instance.
 * @param topic    Destination topic.
 * @param payload  Null‑terminated UTF‑8 string.
 * @param retained MQTT retained flag.
 * @return true on success; false on error.
 */
bool mqtt_publish(PubSubClient& client, const char* topic,
                  const char* payload, bool retained = true);

/**
 * @brief Publish a small binary payload (convenience overload).
 *
 * @param client   PubSubClient instance.
 * @param topic    Destination topic.
 * @param payload  Pointer to bytes.
 * @param length   Number of bytes to send.
 * @param retained MQTT retained flag.
 * @return true on success; false on error.
 */
bool mqtt_publish(PubSubClient& client, const char* topic,
                  const byte* payload, unsigned int length,
                  bool retained);

/**
 * @brief Stream a large payload in fixed‑size chunks to avoid big TLS writes.
 *
 * @param client      PubSubClient instance.
 * @param topic       Destination topic.
 * @param payload     Pointer to the full payload buffer.
 * @param length      Total bytes to publish.
 * @param retained    MQTT retained flag.
 * @param chunk_bytes Max bytes per @c write() call (e.g., 256–1024 for Wi‑Fi stability).
 * @return true if all bytes were published; false otherwise.
 *
 * @note Internally uses @c beginPublish / @c write / @c endPublish. Ideal for images/frames.
 */
bool mqtt_publish_stream(PubSubClient& client,
                         const char* topic,
                         const uint8_t* payload,
                         size_t length,
                         bool retained,
                         size_t chunk_bytes);

/**
 * @brief Stream a payload composed of two segments (header + body) without copying.
 *
 * @param client      PubSubClient instance.
 * @param topic       Destination topic.
 * @param seg1        Pointer to segment 1 (e.g., header). Pass nullptr if @p len1 is 0.
 * @param len1        Size of segment 1 in bytes.
 * @param seg2        Pointer to segment 2 (e.g., body). Pass nullptr if @p len2 is 0.
 * @param len2        Size of segment 2 in bytes.
 * @param retained    MQTT retained flag.
 * @param chunk_bytes Max bytes per @c write() call.
 * @return true on success; false on failure.
 *
 * @note Useful when you want to prepend a tiny header without building a new buffer.
 */
bool mqtt_publish_stream_2seg(PubSubClient& client,
                              const char* topic,
                              const uint8_t* seg1, size_t len1,
                              const uint8_t* seg2, size_t len2,
                              bool retained = false,
                              size_t chunk_bytes = 1024);

/**
 * @brief Subscribe to a topic.
 * @param client PubSubClient instance.
 * @param topic  Topic filter to subscribe to.
 * @return true on success; false on failure.
 */
bool mqtt_subscribe(PubSubClient& client, const char* topic);

/**
 * @brief Drive the PubSubClient state machine (keeps the connection alive, dispatches callbacks).
 *
 * Call this regularly from your @c loop().
 *
 * @param client PubSubClient instance.
 */
void mqtt_loop(PubSubClient& client);

// ===== Hard reset overloads (TLS & plaintext) =================================
/**
 * @brief Force‑close MQTT and the underlying TLS socket.
 *
 * @param client PubSubClient instance.
 * @param net    WiFiClientSecure transport to reset.
 * @note Useful after failed writes or broker timeouts to avoid half‑open sockets.
 */
void mqtt_hard_reset(PubSubClient& client, WiFiClientSecure& net);

/**
 * @brief Force‑close MQTT and the underlying plaintext socket.
 *
 * @param client PubSubClient instance.
 * @param net    WiFiClient transport to reset.
 */
void mqtt_hard_reset(PubSubClient& client, WiFiClient& net);

// ==============================================================================
// RTP / UDP helpers                                                             
// ==============================================================================
/**
 * @brief Write a 12‑byte RTP header (big‑endian fields) into @p p.
 *
 * @param p      Destination buffer (must be at least 12 bytes).
 * @param seq    RTP sequence number.
 * @param ts     RTP timestamp.
 * @param ssrc   SSRC identifier.
 * @param pt     Payload type (7‑bit).
 * @param marker Set true to mark a significant event (M‑bit).
 */
void rtp_write_header_be(uint8_t* p,
                         uint16_t seq, uint32_t ts, uint32_t ssrc,
                         uint8_t pt, bool marker = false);

/**
 * @brief Build an RTP packet with a little‑endian PCM payload into @p out.
 *
 * @param samples  Pointer to PCM samples (int16, little‑endian).
 * @param n        Number of samples.
 * @param out      Destination buffer.
 * @param out_cap  Capacity of @p out in bytes.
 * @param seq      RTP sequence number.
 * @param ts       RTP timestamp.
 * @param ssrc     SSRC identifier.
 * @param pt       Payload type.
 * @param marker   Marker bit.
 * @return Number of bytes written (0 on error).
 */
size_t rtp_build_packet_le(const int16_t* samples,
                           uint16_t n,
                           uint8_t* out,
                           size_t out_cap,
                           uint16_t seq,
                           uint32_t ts,
                           uint32_t ssrc,
                           uint8_t  pt,
                           bool     marker = false);

/**
 * @brief Advance RTP sequence and timestamp counters.
 *
 * @param seq     In/out sequence number (increments by @p seq_inc).
 * @param ts      In/out timestamp (increments by @p ts_inc).
 * @param ts_inc  Timestamp increment.
 * @param seq_inc Sequence increment (defaults to 1).
 */
void rtp_advance(uint16_t* seq,
                 uint32_t* ts,
                 uint32_t  ts_inc,
                 uint16_t  seq_inc = 1);

/**
 * @brief Send a UDP datagram.
 *
 * @param udp  WiFiUDP socket.
 * @param ip   Destination IPv4 string (e.g., "192.168.1.10").
 * @param port Destination port.
 * @param pkt  Pointer to bytes to send.
 * @param len  Number of bytes to send.
 * @return true if all bytes were written; false otherwise.
 */
bool udp_send(WiFiUDP& udp, const char* ip, uint16_t port,
              const uint8_t* pkt, size_t len);

/**
 * @brief Send a tiny UDP packet to warm up ARP/NDP tables before real traffic.
 *
 * @param udp  WiFiUDP socket.
 * @param ip   Destination IPv4 string.
 * @param port Destination port.
 */
void udp_warmup(WiFiUDP& udp, const char* ip, uint16_t port);
