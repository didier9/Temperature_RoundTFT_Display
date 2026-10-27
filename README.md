# Temperature Display using a Round TFT LCD

Subscribe to two MQTT messages (Ambient temperature and Pool temperature)
and display on a round TFT LCD with 240x240 resolution
![](demo.gif)

### Pinout

| GC9A01 | Arduino | ESP32    |
|:------:|:-------:|:--------:|
| SCL    | SCK     | 4        |
| SDA    | MOSI    | 6        |
| RES    | 8       | 8        |
| DC     | 9       | 9        |
| CS     | 10      | 10       |

### Useful documentation

[Breakout Board](https://www.buydisplay.com/1-28-inch-tft-ips-lcd-display-module-240x240-spi-for-arduino-raspberry-pi) - 
[GC9A01 Datasheet](https://www.buydisplay.com/download/ic/GC9A01A.pdf) - 
[STC12C5A driver](https://forum.arduino.cc/index.php?action=dlattach;topic=690899.0;attach=368944)
