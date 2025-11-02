# USBone Macro Encryption Guide

## Overview
The USBone device now includes AES-256-CBC encryption for protecting sensitive macro data stored on the SD card. This ensures that even if someone gains physical access to the SD card, they cannot read your sensitive macros without the encryption key.

## Features

### 🔐 Security Features
- **AES-256-CBC Encryption**: Military-grade encryption standard
- **Secure Key Storage**: Encryption keys stored in ESP32's Non-Volatile Storage (NVS)
- **Automatic Migration**: Plain text macros automatically converted to encrypted format
- **Hardware RNG**: Uses ESP32's hardware random number generator for key generation
- **PKCS7 Padding**: Industry-standard padding for block cipher operations

### 🔄 Backward Compatibility
- Automatically detects and migrates existing plain text `macros.txt` files
- After migration, the plain text file is securely deleted
- Web UI remains unchanged - encryption/decryption happens transparently

## File Format

### Encrypted File
- **Location**: `/macros.enc` on SD card
- **Format**: Binary encrypted data (AES-256-CBC)
- **Content**: Same macro format as before, but encrypted

### Original Format (Still Supported)
```
# Regular macros
Email:user@example.com
Username:john_doe

# Sensitive macros  
SENSITIVE:Password:MySecretPassword123!
SENSITIVE:API_Key:sk-1234567890abcdef
```

## Security Best Practices

### ✅ DO's
1. **Keep firmware updated** - Security improvements in newer versions
2. **Use strong authentication** - Change default web UI credentials
3. **Physical security** - Keep device in secure location
4. **Regular backups** - Export macros periodically (they'll be decrypted in web UI)
5. **Use SENSITIVE prefix** - Mark sensitive macros appropriately

### ❌ DON'Ts
1. **Don't share devices** - Each device has unique encryption keys
2. **Don't modify macros.enc directly** - Always use web UI or device interface
3. **Don't reset NVS carelessly** - This will make encrypted data unrecoverable
4. **Don't downgrade firmware** - Older versions won't support encryption

## Key Management

### Key Generation
- Keys are automatically generated on first use
- Uses ESP32's hardware RNG for cryptographic randomness
- Stored securely in NVS (survives power cycles)

### Key Rotation
To rotate encryption keys (makes old encrypted data unreadable):
1. Export your macros through web UI first
2. Add key rotation function call in code (optional feature)
3. Re-import macros after rotation

### Key Recovery
⚠️ **WARNING**: If NVS is erased or corrupted, encrypted macros become unrecoverable!
- No backdoor or master key exists (by design)
- Always maintain backups of important macros

## Technical Details

### Encryption Algorithm
- **Cipher**: AES-256-CBC (Cipher Block Chaining)
- **Key Size**: 256 bits (32 bytes)
- **IV Size**: 128 bits (16 bytes)
- **Padding**: PKCS7

### Memory Usage
- Encryption operations use heap memory
- Large macro files are processed in memory
- PSRAM utilized for better performance

### Performance
- Initial encryption: ~100ms for typical macro file
- Decryption on load: ~50-100ms
- Web UI operations: No noticeable delay

## Migration Process

When upgrading from unencrypted version:
1. Device checks for `macros.txt` on boot
2. If found, automatically encrypts to `macros.enc`
3. Original `macros.txt` deleted after successful encryption
4. All future operations use encrypted file

## Troubleshooting

### "Failed to decrypt macros file"
- Corruption in encrypted file
- NVS key mismatch (device was reset)
- Solution: Delete macros.enc and recreate macros

### "Failed to initialize crypto system"
- NVS storage issue
- Solution: Check NVS partition, may need reflashing

### Web UI shows empty macros
- Encryption/decryption failure
- Check serial monitor for detailed error messages

## API Changes

### GET /api/macros
- Returns decrypted content
- Automatic migration if plain text file exists
- Requires authentication

### POST /api/macros
- Accepts plain text content
- Automatically encrypts before saving
- Removes old plain text files

## Future Enhancements

Potential improvements for consideration:
- Cloud backup with encrypted sync
- Multi-device key sharing (secure key exchange)
- Tamper detection for SD card
- Optional password-based encryption layer
- Encrypted configuration files

## Security Disclosure

If you discover a security vulnerability:
1. Do NOT publish it publicly
2. Contact the maintainers privately
3. Allow time for patch development
4. Coordinated disclosure after fix

## License

The encryption implementation uses:
- mbedTLS library (Apache 2.0 License)
- ESP-IDF crypto components (Apache 2.0 License)

---

**Remember**: Encryption is just one layer of security. Physical device security, strong passwords, and regular updates are equally important!
