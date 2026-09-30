# Plant Watering System (Arduino Uno)

Soil-moisture-only watering: pumps water when the soil drops below a threshold,
then waits 6 hours so the water can soak in before re-checking.

## Parts checklist

Cross-check against what you own. Buy the rest.

| Part | Spec / note | Have it? |
|---|---|---|
| Arduino Uno | the brain | |
| Capacitive soil moisture sensor | v1.2 or v2.0, analog AO pin. **Avoid resistive probes** (corrode in weeks) | |
| Water pump | 3–6 V submersible (runs from a USB charger) or 12 V | |
| MOSFET (IRF520) or 5 V relay module | switches the pump on/off | |
| Pump power supply | 5 V / 1–2 A USB charger (or matching your pump) | |
| Silicone tubing (4–5 mm) + reservoir | drip line from bottle to soil | |
| Breadboard + jumper wires | | |
| Arduino power | 9–12 V DC adapter or USB cable | |
| Pushbutton, LED, buzzer *(optional)* | manual water, status, faults | |

## What to buy (with your Uno already owned)

**Required (core system, ~$12–15 total):**

| Item | What to search / look for | Est. price |
|---|---|---|
| Capacitive soil moisture sensor | "Capacitive soil moisture sensor v1.2" — 3 pins (VCC/GND/AO), green PCB. **Do not** buy the metal-prong resistive type | $2–3 |
| 5 V submersible pump | "5V mini submersible water pump" — 3–6 V DC, clear plastic, with outlet fitting (~$1). If you only have a 12 V pump, get a 12 V supply instead | $1–2 |
| IRF520 MOSFET module | "IRF520 MOSFET driver module" — the blue board with screw terminals (easier than a bare transistor) | $2–3 |
| 5 V / 2 A USB power supply | any old phone charger + USB cable you cut/adapt, or a "5V 2A DC adapter" with barrel plug | $0–5 |
| Silicone tube 5 mm inner Ø | "5mm silicone tubing" — ~2 m, fits the pump outlet and makes drip lines easy | $3–4 |
| Reservoir | any clean bottle/jar you have; add a lid hole for the tube | $0 |
| Breadboard + jumper wires | 830-point breadboard + 65-piece jumper wire set (the male-to-male ones) | $4–6 |

**Strongly recommended (safety/UX, ~$2 total):**

| Item | Why |
|---|---|
| Tactile pushbutton (4-pin) | manual "water now" without opening a serial console |
| 5 V active buzzer module | faults (sensor error, pump timeout) and calibration steps |

**Skip for now:** OLED display, RTC, WiFi — the firmware doesn't need them, and it keeps the first build to ~15 min.

**Good starter kit alternative:** a "37-in-1 sensor kit for Arduino" already contains the soil sensor, relay/MOSFET, button, and buzzer — then you only buy the pump + tubing (~$5).

## Wiring

```
Arduino Uno                       Pump
  GND   o------------------------- GND (pump supply)   <-- common ground
  D4    o---- VCC of soil sensor         (pump supply -)
  A0    o---- AO   of soil sensor
  GND   o---- GND  of soil sensor
  D7    o---- IN   of relay/MOSFET       pump + o---[ relay/mosfet ]---o pump +
  D2    o---- button (to GND)            pump - o--- supply GND
  D8    o---- buzzer (+)
  D13   o---- LED (onboard)
```

- **Never power the pump from the Arduino.** It has its own supply.
- Arduino GND and pump-supply GND must be tied together.

## Flash & first run

1. Upload `plant_watering.ino` to the Uno (Arduino IDE, board: Uno, port: COM/USB).
2. Open Serial Monitor at **9600 baud**.
3. Type `cal` and follow the wizard:
   - sensor in **dry soil / air** → Enter
   - sensor in a **cup of water** → Enter
4. Readings are stored in EEPROM — calibration survives power loss.

## Serial commands

| Command | What it does |
|---|---|
| `help` | list commands |
| `read` | one live raw + moisture % reading |
| `stat` | state, moisture, threshold, calibration status |
| `water` | water now (manual) |
| `set 35` | change threshold to 35 % |
| `cal` | recalibrate dry/wet |

## How it decides

- Every 60 s: read sensor (average of 10 samples), compute % via
  `(dry - raw) / (dry - wet) × 100`.
- Below threshold → pump 5 s, then 6 h cooldown (soil soaks in, no overwatering).
- Safety: pump never runs more than 30 s; raw near 1023 ⇒ sensor error, skips watering.

## Typical troubleshooting

- **Readings stuck near 1023** — sensor power pin (D4) not wired, or bad jumper.
- **Dry and wet too close in `cal`** — check 5 V/GND on the sensor; a resistive
  probe may be old/corroded.
- **Pump never starts** — threshold vs `stat` reading, or relay IN polarity
  (some modules are active LOW — swap to the other side of the switch).
- **Pump runs but no water** — air-locked tubing; prime the line by hand.
