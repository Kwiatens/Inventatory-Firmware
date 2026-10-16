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

The red and green LED pads (R+ and G+) feed GPIO 4 and GPIO 38. Measure each pad against GND with the LED lit and off before you connect it to a GPIO. A pad above 3.3 V needs a divider.
