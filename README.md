# ESP32TimeLapseFTP_MMC
## Using AI Thinker ESP32-CAM, take time lapse pics, save on MMC, serve via FTP, with OTA.

Using code from David Paiva, robo8080, and mtnbkr88 (+ others).

Built using Arduino IDE.

AI Thinker ESP32-CAM board (with OV2640 camera) takes photos at rate compiled into code,
Stores them on MMCcard (using time from NTP in name, if available),
Hosts a (very simple) FTP server to allow transfer (and deletion) of photos.

Supports OTA (Both Arduino IDE and web browser).

You should be able to clone this into your sketchbook and compile, provided all libs found).

This sketch uses SmartConfig. WiFi credentials are set using smartphone app, 
rather than being baked into the source code. I have used this method extensively and
with success. This implementation is not well tested.

This is a work in progress.

All suggestions, questions, issues, constructive criticism gladly accepted.
