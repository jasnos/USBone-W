#include "../include/crypto_manager.h"
#include <SD_MMC.h>
#include <esp_random.h>

CryptoManager& CryptoManager::getInstance() {
    static CryptoManager instance;
    return instance;
}

bool CryptoManager::initialize() {
    if (initialized) {
        return true;
    }
    
    // Load or generate encryption key
    if (!loadOrGenerateKey()) {
        Serial.println("Failed to initialize encryption key");
        return false;
    }
    
    // Generate IV (can be static or dynamic per file)
    // For simplicity, using a static IV stored with the key
    // In production, consider using a unique IV per encryption
    generateRandomBytes(iv, IV_SIZE);
    
    initialized = true;
    Serial.println("Crypto system initialized successfully");
    return true;
}

bool CryptoManager::loadOrGenerateKey() {
    // Try to load existing key from NVS
    if (loadKeyFromNVS()) {
        Serial.println("Encryption key loaded from NVS");
        return true;
    }
    
    // Generate new key if none exists
    Serial.println("Generating new encryption key...");
    generateRandomBytes(encryptionKey, KEY_SIZE);
    
    // Save to NVS for persistence
    if (!saveKeyToNVS()) {
        Serial.println("Warning: Could not save key to NVS");
        // Continue anyway - key will be regenerated on next boot
    }
    
    return true;
}

bool CryptoManager::saveKeyToNVS() {
    Preferences prefs;
    if (!prefs.begin("crypto", false)) {
        return false;
    }
    
    // Save encryption key
    size_t written = prefs.putBytes("aes_key", encryptionKey, KEY_SIZE);
    if (written != KEY_SIZE) {
        prefs.end();
        return false;
    }
    
    // Save IV
    written = prefs.putBytes("aes_iv", iv, IV_SIZE);
    if (written != IV_SIZE) {
        prefs.end();
        return false;
    }
    
    // Save a magic number to verify key validity
    prefs.putUInt("magic", 0xDEADBEEF);
    
    prefs.end();
    return true;
}

bool CryptoManager::loadKeyFromNVS() {
    Preferences prefs;
    if (!prefs.begin("crypto", true)) {  // Read-only mode
        return false;
    }
    
    // Check magic number
    if (prefs.getUInt("magic", 0) != 0xDEADBEEF) {
        prefs.end();
        return false;
    }
    
    // Load encryption key
    size_t keyLen = prefs.getBytes("aes_key", encryptionKey, KEY_SIZE);
    if (keyLen != KEY_SIZE) {
        prefs.end();
        return false;
    }
    
    // Load IV
    size_t ivLen = prefs.getBytes("aes_iv", iv, IV_SIZE);
    if (ivLen != IV_SIZE) {
        prefs.end();
        return false;
    }
    
    prefs.end();
    return true;
}

void CryptoManager::generateRandomBytes(uint8_t* buffer, size_t length) {
    // Use ESP32's hardware RNG
    for (size_t i = 0; i < length; i++) {
        buffer[i] = esp_random() & 0xFF;
    }
}

std::vector<uint8_t> CryptoManager::addPKCS7Padding(const std::vector<uint8_t>& data) {
    size_t padding_length = BLOCK_SIZE - (data.size() % BLOCK_SIZE);
    std::vector<uint8_t> padded = data;
    
    for (size_t i = 0; i < padding_length; i++) {
        padded.push_back(padding_length);
    }
    
    return padded;
}

std::vector<uint8_t> CryptoManager::removePKCS7Padding(const std::vector<uint8_t>& data) {
    if (data.empty() || data.size() % BLOCK_SIZE != 0) {
        return data;  // Invalid data
    }
    
    uint8_t padding_length = data.back();
    if (padding_length > BLOCK_SIZE || padding_length > data.size()) {
        return data;  // Invalid padding
    }
    
    // Verify padding
    for (size_t i = data.size() - padding_length; i < data.size(); i++) {
        if (data[i] != padding_length) {
            return data;  // Invalid padding
        }
    }
    
    std::vector<uint8_t> unpadded(data.begin(), data.end() - padding_length);
    return unpadded;
}

bool CryptoManager::encryptData(const uint8_t* input, size_t inputLen, std::vector<uint8_t>& output) {
    if (!initialized) {
        return false;
    }
    
    // Convert input to vector and add padding
    std::vector<uint8_t> inputVec(input, input + inputLen);
    std::vector<uint8_t> padded = addPKCS7Padding(inputVec);
    
    // Prepare output buffer
    output.resize(padded.size());
    
    // Setup AES context
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Set encryption key
    int ret = mbedtls_aes_setkey_enc(&aes, encryptionKey, 256);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Create a copy of IV (CBC mode modifies it)
    uint8_t ivCopy[IV_SIZE];
    memcpy(ivCopy, iv, IV_SIZE);
    
    // Encrypt data
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT,
                                 padded.size(), ivCopy,
                                 padded.data(), output.data());
    
    mbedtls_aes_free(&aes);
    return (ret == 0);
}

bool CryptoManager::decryptData(const uint8_t* input, size_t inputLen, std::vector<uint8_t>& output) {
    if (!initialized || inputLen % BLOCK_SIZE != 0) {
        return false;
    }
    
    // Prepare output buffer
    std::vector<uint8_t> decrypted(inputLen);
    
    // Setup AES context
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    // Set decryption key
    int ret = mbedtls_aes_setkey_dec(&aes, encryptionKey, 256);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }
    
    // Create a copy of IV
    uint8_t ivCopy[IV_SIZE];
    memcpy(ivCopy, iv, IV_SIZE);
    
    // Decrypt data
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT,
                                 inputLen, ivCopy,
                                 input, decrypted.data());
    
    mbedtls_aes_free(&aes);
    
    if (ret != 0) {
        return false;
    }
    
    // Remove padding
    output = removePKCS7Padding(decrypted);
    return true;
}

String CryptoManager::encryptString(const String& plainText) {
    if (!initialized || plainText.length() == 0) {
        return "";
    }
    
    std::vector<uint8_t> encrypted;
    if (!encryptData((const uint8_t*)plainText.c_str(), plainText.length(), encrypted)) {
        return "";
    }
    
    // Convert to hex string for easy storage
    String result;
    result.reserve(encrypted.size() * 2);
    
    for (uint8_t byte : encrypted) {
        char hex[3];
        sprintf(hex, "%02X", byte);
        result += hex;
    }
    
    return result;
}

String CryptoManager::decryptString(const String& cipherText) {
    if (!initialized || cipherText.length() == 0 || cipherText.length() % 2 != 0) {
        return "";
    }
    
    // Convert hex string back to bytes
    std::vector<uint8_t> encrypted;
    encrypted.reserve(cipherText.length() / 2);
    
    for (size_t i = 0; i < cipherText.length(); i += 2) {
        String byteStr = cipherText.substring(i, i + 2);
        uint8_t byte = strtol(byteStr.c_str(), nullptr, 16);
        encrypted.push_back(byte);
    }
    
    std::vector<uint8_t> decrypted;
    if (!decryptData(encrypted.data(), encrypted.size(), decrypted)) {
        return "";
    }
    
    // Convert decrypted bytes to string
    return String((char*)decrypted.data(), decrypted.size());
}

bool CryptoManager::encryptFile(const String& inputPath, const String& outputPath) {
    if (!initialized) {
        return false;
    }
    
    File inputFile = SD_MMC.open(inputPath, FILE_READ);
    if (!inputFile) {
        Serial.println("Failed to open input file: " + inputPath);
        return false;
    }
    
    // Read entire file into memory (consider chunking for large files)
    size_t fileSize = inputFile.size();
    std::vector<uint8_t> fileData(fileSize);
    inputFile.read(fileData.data(), fileSize);
    inputFile.close();
    
    // Encrypt data
    std::vector<uint8_t> encrypted;
    if (!encryptData(fileData.data(), fileSize, encrypted)) {
        Serial.println("Encryption failed");
        return false;
    }
    
    // Write encrypted data to output file
    File outputFile = SD_MMC.open(outputPath, FILE_WRITE);
    if (!outputFile) {
        Serial.println("Failed to open output file: " + outputPath);
        return false;
    }
    
    size_t written = outputFile.write(encrypted.data(), encrypted.size());
    outputFile.close();
    
    return (written == encrypted.size());
}

bool CryptoManager::decryptFile(const String& inputPath, const String& outputPath) {
    if (!initialized) {
        return false;
    }
    
    File inputFile = SD_MMC.open(inputPath, FILE_READ);
    if (!inputFile) {
        Serial.println("Failed to open encrypted file: " + inputPath);
        return false;
    }
    
    // Read encrypted file
    size_t fileSize = inputFile.size();
    std::vector<uint8_t> encrypted(fileSize);
    inputFile.read(encrypted.data(), fileSize);
    inputFile.close();
    
    // Decrypt data
    std::vector<uint8_t> decrypted;
    if (!decryptData(encrypted.data(), fileSize, decrypted)) {
        Serial.println("Decryption failed");
        return false;
    }
    
    // Write decrypted data to output file
    File outputFile = SD_MMC.open(outputPath, FILE_WRITE);
    if (!outputFile) {
        Serial.println("Failed to open output file: " + outputPath);
        return false;
    }
    
    size_t written = outputFile.write(decrypted.data(), decrypted.size());
    outputFile.close();
    
    return (written == decrypted.size());
}

bool CryptoManager::rotateKey() {
    // Generate new key
    generateRandomBytes(encryptionKey, KEY_SIZE);
    generateRandomBytes(iv, IV_SIZE);
    
    // Save to NVS
    if (!saveKeyToNVS()) {
        return false;
    }
    
    Serial.println("Encryption key rotated successfully");
    Serial.println("WARNING: Previous encrypted files will no longer be readable!");
    return true;
}

bool CryptoManager::hasValidKey() {
    Preferences prefs;
    if (!prefs.begin("crypto", true)) {
        return false;
    }
    
    bool valid = (prefs.getUInt("magic", 0) == 0xDEADBEEF);
    prefs.end();
    
    return valid;
}
