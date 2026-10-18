# Wiring notes

## GM65 scanner cable: the wire colours are wrong

The wire colours on the GM65 cable supplied with this project do not follow the usual code. Do not connect the cable by colour.

**Colours can differ between units.** On one unit the black wire is not GND. Its GND wire is green. Another unit may use other colours again, so check every cable before you connect it. This note must also go into the assembly guide.

Before you connect anything, check each wire on the scanner end with a multimeter, and find the one that goes to GND. Connect ground first. A wire on the wrong pad, including ground, will stop the scanner from working.

Keep the table below up to date after each check:

| Wire colour | Scanner pad | Connect to | Checked with a meter |
| --- | --- | --- | --- |
| Green (one unit; black is not GND on that unit) | GND | Board GND pad | reported on one unit, not yet recorded in this table |
| *to be filled in* | TX | GPIO 44 (D0/RXD) | no |
| *to be filled in* | RX | GPIO 43 (D1/TXD) | no |
| *to be filled in* | VCC | Power pad from the regulator board | no |

The scanner's signal pins are also listed in [AGENTS.md](../AGENTS.md), section 3. Check the scanner's own label or datasheet for its supply voltage before you connect VCC.

## Charger LEDs from the UPS module

Only the red LED pad (R+) is connected, to GPIO 4 through a 1 MOhm / 1 MOhm divider. The green pad (G+) is not connected: the charged state is worked out from the cell voltage. Measure the red pad against GND with the LED lit and off before you connect it.

The ESP32-S3's internal pull-down (about 45 kOhm) sits in parallel with the lower 1 MOhm, so with the lit pad at the top it reads only a few percent of the pad voltage. Measure the GPIO 4 pin with the LED lit, and check it reads high (above 2.5 V at 3.3 V), before you rely on charging.

## Charger input sense and backlight

The charger's 5 V input goes through a 1 MOhm / 1 MOhm divider. Its midpoint goes to GPIO 2 (header A1), which is read with the ADC, not as a digital input. It reads about 2.5 V with the charger plugged in and 0 V when it is not. Keep the pin free of pull resistors.

The LCD backlight transistor is driven from GPIO 38 (header MOSI, right header pin 14). Its gate needs a pull-down, so the backlight stays off while the chip sleeps.
