# USBone W - PlatformIO Project

This project has been converted from Arduino IDE to PlatformIO for the Waveshare ESP32-S3-LCD-1.47 board.

## Board Specifications
- **MCU**: ESP32-S3 with dual-core Xtensa LX7 @ 240MHz
- **Flash**: 16MB QSPI
- **PSRAM**: 8MB OPI
- **Display**: 1.47" LCD (172x320) with ST7789 driver
- **Features**: USB HID support, SD card slot, RGB LED

## Configuration Highlights

### Board Settings (matching Arduino IDE)
- **Board**: ESP32-S3 Dev Module (configured as esp32-s3-devkitc-1-n16r8v)
- **PSRAM**: OPI PSRAM enabled
- **USB Mode**: USB-OTG with HID support
- **USB CDC On Boot**: Disabled (for HID keyboard functionality)
- **CPU Frequency**: 240MHz (WiFi)
- **Flash Mode**: QIO 80MHz
- **Flash Size**: 16MB with custom partition scheme
- **Partition Scheme**: Custom (3MB app / 9.9MB FATFS)

### Pin Definitions
- LCD pins configured via build flags
- SD card using MMC mode on pins 14-18, 21
- Button on GPIO 0

## Building and Uploading

1. **Build the project**:
   ```bash
   pio run
   ```

2. **Upload to board**:
   ```bash
   pio run -t upload
   ```

3. **Monitor serial output**:
   ```bash
   pio device monitor
   ```

## Features
- WiFi Access Point mode (SSID: USBone, Password: usbone01)
- Web interface for script management
- USB HID keyboard emulation
- SD card support for storing scripts
- LCD display with Adafruit GFX library support
- RGB LED status indicator

## Libraries Used
- Adafruit GFX Library (display graphics)
- ESPAsyncWebServer (web interface)
- AsyncTCP (async network operations)
- Native ESP32-S3 USB HID support

## Notes
- The main sketch has been renamed from `USBone_1_5_wifi_FIXEDworking.ino` to `main.cpp`
- Custom partition table `default_16MB.csv` provides optimal storage allocation
- USB HID mode requires USB CDC to be disabled on boot
