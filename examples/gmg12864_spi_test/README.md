# GMG12864-06D SPI smoke test

This is a standalone PlatformIO project. It does not alter the production Inventatory Scan R1 firmware.

## Wiring

Connect the display to the ESP32-S3-Nano as follows. Power the module from **3.3 V** and connect a common ground.

| Display signal | ESP32-S3 GPIO |
| --- | --- |
| SCL | 13 (A6) |
| RS (data/command) | 11 (A4) |
| RSE (reset) | 12 (A5) |
| CS | 21 (D10) |
| SI | 14 (A7) |

## Run

Open this `gmg12864_spi_test` folder as its own PlatformIO project, then build/upload the `waveshare_esp32s3_nano` environment. The test uses portrait orientation (64 x 128 pixels): it should show a title, a success message, and an uptime counter that changes every second. The serial monitor at 115200 baud reports the same updates.

The selected portrait direction is `U8G2_R1` (90 degrees clockwise from the module's default landscape orientation). If the device's physical mounting requires the opposite vertical direction, change it to `U8G2_R3`.

## Dependency license

The test uses [U8g2](https://github.com/olikraus/u8g2), licensed under BSD 2-Clause. That license permits commercial use, modification, and distribution provided its notice and disclaimer are retained.
