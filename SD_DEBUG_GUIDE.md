# SD Card Debug Guide for USBone

## Current Issue
The device is showing "Test1" and "Test2" macros, which indicates the SD card initialization is failing, causing the device to fall back to hardcoded test macros.

## Debugging Steps

### 1. Check Serial Monitor Output
When the device boots, you should see detailed SD initialization messages:
```
Initializing SD card...
SD pins configured
SD_MMC.begin() succeeded
SD Card Type: SDHC
SD Card Size: 8192MB
Files in root directory:
  FILE: 256 bytes - macros.txt
SD card initialization complete
```

If you see errors like:
- `SD_MMC.setPins() failed`
- `SD_MMC.begin() failed`
- `No SD card attached`

This indicates a hardware or compatibility issue.

### 2. Test the Web Server Status
Access: `http://[device-ip]/test` (no authentication required)

This will show:
- SD Card Available: Yes/No
- Number of macros loaded
- First macro name

### 3. Common Issues and Solutions

#### Issue: SD_MMC.begin() failed
**Possible causes:**
1. **SD card not properly inserted** - Reinsert the card
2. **Incompatible SD card** - Try a different card (SDHC recommended)
3. **Wiring issues** - Check pin connections:
   - CMD: GPIO 15
   - CLK: GPIO 14
   - D0: GPIO 16
   - D1: GPIO 18
   - D2: GPIO 17
   - D3: GPIO 21

#### Issue: SD card initializes but macros don't load
**Check:**
1. File name must be exactly `macros.txt` (case sensitive)
2. File must be in root directory (not in a folder)
3. File format must be correct:
   ```
   Name1:Content1
   Name2:Content2
   SENSITIVE:Name3:Content3
   ```

#### Issue: Web UI shows "Failed to load macros"
This happens when:
1. SD card failed to initialize (check serial monitor)
2. No macros file exists on SD card
3. File permissions issue

### 4. Alternative Initialization (If Standard Fails)

The code now tries multiple SD initialization modes:
1. 1-bit mode (more compatible but slower)
2. 4-bit mode (faster but less compatible)

### 5. Testing SD Card Separately

Create a simple test file on your SD card:
1. Name it `test.txt`
2. Put it in the root directory
3. The serial monitor will list all files during initialization

### 6. Format Requirements

- **File System**: FAT32 recommended
- **Card Size**: 32GB or less recommended
- **Card Type**: SDHC preferred
- **Speed Class**: Class 10 or better

### 7. Emergency Fallback

If SD card continues to fail:
1. The device will use Test1/Test2 macros
2. WiFi mode will still work
3. You can still use the device for basic testing

### 8. Check Encryption Migration

If you had a working `macros.txt` file:
1. The system should automatically encrypt it to `macros.enc`
2. The original `macros.txt` is deleted after successful migration
3. Check serial monitor for migration messages:
   ```
   Loading plain text macros for migration...
   Migrating to encrypted format...
   Migration successful, removing plain text file...
   ```

### 9. Reset Everything

If all else fails:
1. Format SD card as FAT32
2. Create fresh `macros.txt` with simple content:
   ```
   Test:Hello World
   Email:test@example.com
   ```
3. Power cycle the device
4. Check serial monitor for detailed output

### 10. Hardware Considerations

Some ESP32-S3 boards have issues with SD_MMC in 4-bit mode. The updated code automatically falls back to 1-bit mode if 4-bit fails.

## What the Serial Monitor Should Show (Success Case)

```
=== USBone WiFi Starting ===
RGB LED OK
LCD OK
Button OK
Initializing SD card...
SD pins configured
SD_MMC.begin() succeeded
SD Card Type: SDHC
SD Card Size: 8192MB
Files in root directory:
  FILE: 256 bytes - macros.txt
SD card initialization complete
SD OK
Crypto system initialized
Loading macros from SD...
Has encrypted file: false
Has plain text file: true
Loading plain text macros for migration...
Plain text content length: 256
Migrating to encrypted format...
Migration successful, removing plain text file...
Processed 10 lines
Loaded 3 macros (1 sensitive)
First macro name: Test
First macro preview: Hello World
=== Ready ===
```

## Quick Fix Attempts

1. **Try different SD card** - Some cards have compatibility issues
2. **Reduce SD card speed** - Already implemented in code
3. **Use 1-bit mode only** - Already auto-fallback in code
4. **Check power supply** - Insufficient power can cause SD failures
5. **Clean SD card contacts** - Dust/oxidation can cause issues
