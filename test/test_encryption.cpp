// Test program to verify encryption functionality
// This can be used to test the crypto system independently

#include <Arduino.h>
#include "../include/crypto_manager.h"
#include <SD_MMC.h>

void testEncryption() {
    Serial.println("\n=== ENCRYPTION TEST ===");
    
    // Initialize crypto
    CryptoManager& crypto = CryptoManager::getInstance();
    if (!crypto.initialize()) {
        Serial.println("FAIL: Could not initialize crypto");
        return;
    }
    Serial.println("PASS: Crypto initialized");
    
    // Test string encryption/decryption
    String testData = "Hello, this is a test macro!\nWith multiple lines\nAnd special chars: @#$%";
    Serial.println("Original: " + testData);
    
    // Encrypt
    std::vector<uint8_t> encrypted;
    if (!crypto.encryptData((const uint8_t*)testData.c_str(), testData.length(), encrypted)) {
        Serial.println("FAIL: Encryption failed");
        return;
    }
    Serial.println("PASS: Encrypted " + String(encrypted.size()) + " bytes");
    
    // Decrypt
    std::vector<uint8_t> decrypted;
    if (!crypto.decryptData(encrypted.data(), encrypted.size(), decrypted)) {
        Serial.println("FAIL: Decryption failed");
        return;
    }
    
    String decryptedStr((char*)decrypted.data(), decrypted.size());
    Serial.println("Decrypted: " + decryptedStr);
    
    if (testData == decryptedStr) {
        Serial.println("PASS: Encryption/Decryption successful!");
    } else {
        Serial.println("FAIL: Decrypted data doesn't match original");
    }
}

// Add this function call in your setup() for testing:
// testEncryption();
