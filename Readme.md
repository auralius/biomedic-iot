## Hardware

<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/hardware/test-platform.png" width="550">

<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/hardware/device.png" width="550">

## Software

Copy the `iot-b` folder to `~/Arduino/libraries/`.
Copy the `example1` folder to `~/Arduino/`.

## Demonstrations
__Example 1__
- Connect to HiveMQ.
- Publish arbitrary messages.

__Example 2__
- Connect to HiveMQ.
- Simulate a 1-channel ECG.
- Publish to HiveMQ (binary data frame; publish rate 2 Hz; 125 samples per frame).
- Plot the ECG signal in Google Colab (Paho-MQTT subscriber).
<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/dashboards/dashboard_example2.gif" width="500">

__Example 3 (RTP/RTCP over UDP)__
- Connect to the server on the local network.
- Simulate a 1-channel ECG.
- Send the signal to the server (binary data frame; publish rate 2 Hz; 125 samples per frame).
- Plot the ECG signal in Python (Matplotlib).
<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/dashboards/dashboard_example3.gif" width="500">

__Example 4__
- Connect to HiveMQ.
- Read audio from the microphone.
- Publish to HiveMQ (binary data frame; publish rate 2 Hz; mono 8 kHz audio).
- Run a Python subscriber on a computer with speakers.
- Play the audio through the speakers.

__Example 5__
- MPU6050 sensor.
- Publish to HiveMQ (binary data frame; publish rate 2 Hz; 40 samples per frame).
<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/example5/example5.png" width="280">

__Example 6 (ESP32-S3 RHYX Camera → MQTT JPEG)__
- Capture VGA (640×480) frames from the ESP32-S3 camera.
- Encode to **JPEG in software** (RGB565 → JPEG via `frame2jpg`) since RHYX camera doesn’t do HW-JPEG.
- Publish **one JPEG per MQTT message** over TLS (port 8883) to HiveMQ using `mqtt_publish_stream` (streamed in small chunks).
- Python subscriber decodes and **displays grayscale** frames in real time (Paho-MQTT + Pillow + Matplotlib).
- Typical rate: ~2 FPS by default (tuneable). JPEG **size varies** with scene/quality (e.g., 5–30 KB).
<img src="https://github.com/auralius/biomedic-iot/blob/esp32-hivemq/dashboards/dashboard_example6.gif" width="320">


