/*
  ESP32 → RTP/UDP ECG Sender (1 Hz packets, little-endian payload)
  - Uses helpers from iot-b.h / iot-b.cpp:
      connect_to_campus_wifi / connect_to_home_wifi
      rtp_write_header_be, udp_warmup, udp_send
  - Exactly ONE RTP packet per second
  - Payload: int16 little-endian ECG samples (µV)
  - SAMPLES_PER_PKT defines both batch size and Fs (since 1 packet/sec)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "iot-b.h"  // your helper header

/******** Wi-Fi (choose one) ********/
const char* WIFI_SSID = "Polo";
const char* WIFI_PSWD = "pabl0picas0";  // (inner password)

/* If you prefer home Wi-Fi, comment the campus connect and use:
 *   connect_to_home_wifi(WIFI_SSID, WIFI_PSWD, false);
 */

/******** RTP / UDP target ********/
const char* TARGET_IP = "192.168.18.27";  // receiver IP (ESP32/Python PC)
const uint16_t TARGET_PORT = 6000;        // must match receiver

/******** ECG config (1 packet/sec) ********/
const uint16_t SAMPLES_PER_PKT = 250;      // 125 or 250, etc.
const uint32_t FS_HZ = SAMPLES_PER_PKT;    // 1 packet/sec ⇒ Fs = samples/packet
const uint32_t DT_US = 1000000UL / FS_HZ;  // sample period (µs)

volatile float ECG_HR_BPM = 72.0f;  // simulated heart rate
const float ECG_NOISE_MV = 0.01f;   // ~10 µV noise

/******** RTP state ********/
static uint16_t rtp_seq = 1;
static uint32_t rtp_ts = 0;                   // RTP clock = FS_HZ
static const uint32_t RTP_SSRC = 0xA1B2C3D4;  // arbitrary
static const uint8_t RTP_PT = 97;             // dynamic PT

WiFiUDP udp;

/******** ECG model ********/
static inline float gauss(float x, float mu, float sigma) {
  float z = (x - mu) / sigma;
  return expf(-0.5f * z * z);
}

static float ecg_model_phase(float phase, float t_seconds) {
  float baseline = 0.05f * sinf(2.0f * M_PI * 0.33f * t_seconds);
  float p = 0.10f * gauss(phase, 0.20f, 0.025f);
  float q = -0.15f * gauss(phase, 0.25f, 0.010f);
  float r = 1.00f * gauss(phase, 0.30f, 0.012f);
  float s = -0.25f * gauss(phase, 0.35f, 0.012f);
  float t = 0.35f * gauss(phase, 0.55f, 0.050f);
  return baseline + p + q + r + s + t;
}

static float ecg_sample(float t_seconds, float bpm) {
  float period = 60.0f / fmaxf(bpm, 30.0f);
  float phase = fmodf(t_seconds, period) / period;
  float y = ecg_model_phase(phase, t_seconds);
  float noise = ((float)random(-100, 101) / 100.0f) * ECG_NOISE_MV;
  return y + noise;  // volts in mV units (we’ll convert to µV int16)
}

/******** Sketch ********/
void setup() {
  Serial.begin(115200);
  delay(250);

  randomSeed(esp_random());

  // Wi-Fi (campus WPA2-Enterprise)
  //connect_to_campus_wifi(WIFI_SSID, WIFI_USER, WIFI_PSWD);
  connect_to_home_wifi(WIFI_SSID, WIFI_PSWD, true);

  // Optional: steadier Wi-Fi timing
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Warm up ARP/route: a tiny UDP packet to the receiver
  udp_warmup(udp, TARGET_IP, TARGET_PORT);

  LOGI("ESP32 RTP ECG sender ready (1 packet/sec).");
  LOGI("IP: ");
  LOGI(WiFi.localIP());
}

void loop() {
  static uint32_t last_us = micros();
  static uint64_t t0_us   = micros();

  static int16_t  batch[SAMPLES_PER_PKT];  // µV samples
  static uint16_t n = 0;                   // filled count

  // 1 Hz send deadline (initialized on first run)
  static uint32_t next_send_ms = 0;
  if (next_send_ms == 0) next_send_ms = millis() + 1000;

  // --- Sampling at FS_HZ = SAMPLES_PER_PKT ---
  const uint32_t now_us = micros();
  if ((uint32_t)(now_us - last_us) >= DT_US) {
    last_us += DT_US;  // fixed step to avoid drift

    const float t_sec = (now_us - (uint32_t)t0_us) / 1e6f;
    const float mv    = ecg_sample(t_sec, ECG_HR_BPM);  // model outputs **millivolts**
    long uv = lroundf(mv * 1000.0f);                    // mV → µV
    if (uv < -32768) uv = -32768;
    if (uv >  32767) uv =  32767;
    if (n < SAMPLES_PER_PKT) batch[n++] = (int16_t)uv;
  }

  // --- Send when full OR when the 1-second deadline hits ---
  const int32_t overdue = (int32_t)(millis() - next_send_ms);
  if (n >= SAMPLES_PER_PKT || overdue >= 0) {

    // Top-up if timing jitter left us short (keep packet size constant)
    while (n < SAMPLES_PER_PKT) {
      uint32_t now2 = micros();
      float t_sec2  = (now2 - (uint32_t)t0_us) / 1e6f;
      float mv2     = ecg_sample(t_sec2, ECG_HR_BPM);    // mV
      long uv2      = lroundf(mv2 * 1000.0f);            // mV → µV
      if (uv2 < -32768) uv2 = -32768;
      if (uv2 >  32767) uv2 =  32767;
      batch[n++] = (int16_t)uv2;
    }

    // Build + send RTP
    const size_t RTP_HDR = 12;
    const size_t PAYLOAD = SAMPLES_PER_PKT * sizeof(int16_t);
    uint8_t pkt[RTP_HDR + PAYLOAD];

    rtp_write_header_be(pkt, rtp_seq, rtp_ts, RTP_SSRC, RTP_PT, /*marker=*/true);
    memcpy(pkt + RTP_HDR, batch, PAYLOAD);
    udp_send(udp, TARGET_IP, TARGET_PORT, pkt, sizeof(pkt));

    // Advance RTP state
    rtp_seq++;
    rtp_ts += SAMPLES_PER_PKT;

    // Reset batch and schedule next exact second (catch up if we fell behind)
    n = 0;
    next_send_ms += 1000;
    uint32_t now_ms2 = millis();
    while ((int32_t)(now_ms2 - next_send_ms) >= 0) next_send_ms += 1000;

    // Optional lightweight per-packet log:
    // int16_t mn=32767, mx=-32768; for (uint16_t i=0;i<SAMPLES_PER_PKT;i++){ mn=min(mn,batch[i]); mx=max(mx,batch[i]); }
    // static uint32_t last_send_ms=0; uint32_t now_ms=millis();
    // LOGI("sent seq=%u ts=%lu  range=%d..%d uV  dt=%u ms\n",
    //     (unsigned)(rtp_seq-1), (unsigned long)(rtp_ts-SAMPLES_PER_PKT),
    //     (int)mn, (int)mx, (unsigned)(last_send_ms? (now_ms-last_send_ms):0));
    // last_send_ms = now_ms;
  }
}


