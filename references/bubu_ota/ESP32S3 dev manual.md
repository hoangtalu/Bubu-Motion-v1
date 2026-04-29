ESP32-S3 1.28” Round TFT-I80 Development Board

Consolidated Markdown knowledge base extracted from ESP32S3-128I80T 系列开发板开发参考 V1.0 (2025-04-15)

⸻

1. Overview
	•	MCU: ESP32-S3
	•	Display: 1.28” round IPS TFT, 240×240
	•	Display Interface: I80 / MCU parallel (8-bit)
	•	Touch: Capacitive (CST816 series)
	•	Power: USB / VIN / Li-ion battery with UPS switching
	•	Audio: I2S speaker output + I2S microphone (full duplex)
	•	Expansion: TCA6408 I2C IO expander
	•	Sensors: QMI8658 IMU, PCF85063 RTC
	•	Storage: TF (microSD) via SPI
	•	LEDs: 2× WS2812 RGB

Long-press (≥3s) on the middle side button powers ON/OFF.

⸻

2. ESP32-S3 Core
	•	External Flash: 16 MB SPI Flash
	•	PSRAM: Internal OPI PSRAM (8 MB or 16 MB variants)
	•	⚠️ Must enable OPI PSRAM in firmware
	•	USB:
	•	Native USB (no USB-UART chip)
	•	D- → IO19, D+ → IO20
	•	Use USB download mode, not UART
	•	Enable USB CDC on boot for Serial output

Boot / Download Mode
	•	IO0 + RESET → enter ROM download mode
	•	IO0 is shared with touch reset → do not use as a normal button

⸻

3. Power System

Power Inputs
	•	USB (≤5.5 V)
	•	VIN pad (electrically tied to USB)
	•	BAT (single-cell Li battery)

Power Path
	•	Ideal-diode UPS switching (USB ↔ Battery)
	•	Power switch → TPS63802 buck-boost → 3.3 V rail

Power Button
	•	Middle side button controls power latch
	•	Long-press ≥3s → power ON/OFF
	•	Optional solder pads: short to auto-power-on when power applied

Power Consumption
	•	Power-off quiescent current: ~7 µA

⸻

4. Battery Charging & Measurement

Charger
	•	IC: BQ25170
	•	Default charge voltage: 4.2 V (for 3.7 V Li-ion)
	•	Max charge current: 800 mA (default ~600 mA)

Current set by:

ICHG = KISET / RISET

(Default RISET = 499 Ω)

Battery Types Supported
	•	Li-ion: 4.05 / 4.1 / 4.2 / 4.35 / 4.4 V
	•	LiFePO4: 3.5 / 3.6 / 3.7 V

Battery Voltage Sensing
	•	Divider: 2×100 kΩ
	•	ADC pin: IO1
	•	Floating when battery absent

⚠️ No battery over-discharge protection → use protected cells

⸻

5. Display (GC9A01)
	•	Resolution: 240×240
	•	Interface: I80 (8-bit parallel)

Pin Mapping

Signal	GPIO
D0–D7	IO10–IO17
RST	IO21
WR	IO3
RS / DC	IO18
CS	IO2
Backlight	IO42

Software
	•	Library: Arduino_GFX_Library
	•	Recommended version: 1.4.7

⸻

6. Capacitive Touch (CST816 series)
	•	Interface: I2C
	•	Address: 0x15

Pin Mapping

Signal	GPIO
SDA	IO8
SCL	IO9
RST	IO0 (active LOW)
INT	TCA6408 P0

⚠️ IO0 shared with boot strap → cannot be used as a normal button

Touch interrupt must be detected via TCA6408.

⸻

7. TCA6408 I2C IO Expander
	•	Address: 0x20
	•	Interface: I2C
	•	Interrupt output: IO45 (active LOW)

Port Usage

Port	Function
P0	Touch interrupt
P1	IMU INT1
P2	IMU INT2
P3	Side button UP
P4	Side button POWER
P5	Side button DOWN
P6	USB / charging detect
P7	RTC interrupt

	•	All ports are input with pull-ups
	•	Press / trigger → LOW

Registers

Register	Addr	Description
Input	0x00	Read pin states
Output	0x01	Output control
Polarity	0x02	Invert logic
Config	0x03	1=input, 0=output


⸻

8. Physical Buttons
	•	Total buttons on board: 5
	•	2 system (BOOT IO0, RESET)
	•	3 user side buttons via TCA6408

Side Buttons

Button	TCA6408	Notes
UP	P3	User input
POWER	P4	User + power latch
DOWN	P5	User input


⸻

9. IMU (QMI8658)
	•	6-axis IMU (Accel + Gyro)
	•	Interface: I2C
	•	Address: 0x6B

Connections

Signal	GPIO
SDA	IO8
SCL	IO9
INT1	TCA6408 P1
INT2	TCA6408 P2


⸻

10. RTC (PCF85063)
	•	Interface: I2C
	•	Interrupt → TCA6408 P7
	•	Supports:
	•	Minute / half-minute interrupts
	•	Low-power wake-up
	•	Backup battery

Registers (Summary)
	•	0x00–0x03: Configuration
	•	0x04–0x0A: Time data (BCD)

Supports:
	•	12 / 24 hour mode
	•	Clock offset calibration (ppm-level)

⸻

11. TF (microSD) Card
	•	Interface: SPI

Pin Mapping

Signal	GPIO
CS	IO40
CLK	IO41
MOSI	IO47
MISO	IO48

	•	Use SPI-compatible TF cards
	•	Slot is between PCB and screen (easy to miss in enclosure design)

⸻

12. Audio System (I2S)

Speaker Output
	•	Codec: MAX98357
	•	Max power: 3 W
	•	Recommended speaker: 8 Ω

Signal	GPIO
WS	IO4
BCLK	IO5
DIN	IO7

Microphone Input
	•	Mic: ICS43434 (I2S)

Signal	GPIO
WS	IO4
BCLK	IO5
DOUT	IO6

	•	Full-duplex I2S (shared clock)
	•	Keep mic port unobstructed in enclosure

⸻

13. RGB LEDs (WS2812)
	•	Count: 2
	•	Data pin: IO46
	•	LED #1 near microphone
	•	LED #2 on PCB back (behind screen)

Library: Adafruit_NeoPixel

⸻

14. Expansion Headers

Pins	Function
IO7/6/5/4	I2S (or GPIO if unused)
IO44/43	UART0 (GPS / 4G)
IO39/38	I2C2 / GPIO
IO8/9	I2C1 (shared with sensors)

⚠️ Watch for I2C address conflicts

⸻

15. Design Notes & Warnings
	•	Always enable USB CDC on boot to avoid losing USB
	•	IO0 is critical: boot strap + touch reset
	•	Use protected Li-ion batteries
	•	Enclosure must expose:
	•	Mic port
	•	TF card slot
	•	Speaker cavity

⸻

16. Summary

This board is a highly integrated ESP32-S3 platform optimized for:
	•	UI devices
	•	Wearables
	•	Interactive agents / robots
	•	Audio-visual applications

Key architectural idea:

Use I2C (TCA6408) + interrupts to multiplex many inputs while saving MCU pins

⸻

End of consolidated Markdown knowledge base.