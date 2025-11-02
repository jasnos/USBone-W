#ifndef CRYPTO_MANAGER_H
#define CRYPTO_MANAGER_H

#include <Arduino.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <Preferences.h>
#include <vector>

class CryptoManager {
public:
    // Singleton pattern for secure key management
    static CryptoManager& getInstance();
    
    // Initialize crypto system and generate/load keys
    bool initialize();
    
    // Encrypt/decrypt data
    bool encryptData(const uint8_t* input, size_t inputLen, std::vector<uint8_t>& output);
    bool decryptData(const uint8_t* input, size_t inputLen, std::vector<uint8_t>& output);
    
    // File operations
    bool encryptFile(const String& inputPath, const String& outputPath);
    bool decryptFile(const String& inputPath, const String& outputPath);
    
    // In-memory operations for web UI
    String encryptString(const String& plainText);
    String decryptString(const String& cipherText);
    
    // Key management
    bool rotateKey();  // Generate new key (will make old data unreadable)
    bool hasValidKey();
    
private:
    CryptoManager() = default;
    ~CryptoManager() = default;
    CryptoManager(const CryptoManager&) = delete;
    CryptoManager& operator=(const CryptoManager&) = delete;
    
    // AES-256 requires 32-byte key and 16-byte IV
    static const size_t KEY_SIZE = 32;
    static const size_t IV_SIZE = 16;
    static const size_t BLOCK_SIZE = 16;
    
    uint8_t encryptionKey[KEY_SIZE];
    uint8_t iv[IV_SIZE];
    bool initialized = false;
    
    // Generate or load encryption key from NVS
    bool loadOrGenerateKey();
    bool saveKeyToNVS();
    bool loadKeyFromNVS();
    
    // Helper functions
    void generateRandomBytes(uint8_t* buffer, size_t length);
    std::vector<uint8_t> addPKCS7Padding(const std::vector<uint8_t>& data);
    std::vector<uint8_t> removePKCS7Padding(const std::vector<uint8_t>& data);
};

#endif // CRYPTO_MANAGER_H
