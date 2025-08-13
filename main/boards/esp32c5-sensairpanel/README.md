# ESP32-C5 Sensairpanel Board

This board integrates:
- GC9A01 1.28" 240x240 circular display (SPI)
- ADC microphone input
- I2S PDM speaker output with PA control
- 6x WS2812 RGB LEDs (GPIO0)
- Power control (GPIO7)

Pins (from BSP):
- LCD: DATA0=GPIO8, PCLK=GPIO9, DC=GPIO10, BL=GPIO24
- Audio: ADC channel=4, PDM P=GPIO27, PDM N=GPIO4, PA=GPIO23
- LED: GPIO0 (6 LEDs)
- Power: GPIO7 