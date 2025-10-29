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

__Example 3 (RTP/RTCP over UDP)__
- Connect to the server on the local network.
- Simulate a 1-channel ECG.
- Send the signal to the server (binary data frame; publish rate 2 Hz; 125 samples per frame).
- Plot the ECG signal in Python (Matplotlib).

__Example 4__
- Connect to HiveMQ.
- Read audio from the microphone.
- Publish to HiveMQ (binary data frame; publish rate 2 Hz; mono 8 kHz audio).
- Run a Python subscriber on a computer with speakers.
- Play the audio through the speakers.
