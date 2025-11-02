// Alternative SD card initialization for testing
// You can try this simpler approach if the main one fails

#include <SD_MMC.h>

bool initializeSDSimple() {
  Serial.println("Trying simple SD initialization...");
  
  // Simple 1-bit mode initialization
  if (SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
    Serial.println("SD card mounted successfully in 1-bit mode");
    
    uint8_t cardType = SD_MMC.cardType();
    if(cardType == CARD_NONE){
      Serial.println("No SD card attached");
      return false;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
      Serial.println("MMC");
    } else if(cardType == CARD_SD){
      Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    // List root files
    File root = SD_MMC.open("/");
    if(!root || !root.isDirectory()){
      Serial.println("Failed to open root directory");
      return false;
    }
    
    Serial.println("Root directory contents:");
    File file = root.openNextFile();
    while(file){
      Serial.print("  ");
      Serial.print(file.name());
      if(file.isDirectory()){
        Serial.print("/");
      } else {
        Serial.print(" (");
        Serial.print(file.size());
        Serial.print(" bytes)");
      }
      Serial.println();
      file = root.openNextFile();
    }
    
    return true;
  }
  
  Serial.println("Failed to mount SD card");
  return false;
}

// If you want to test this, replace the initializeSD() call in setup() with:
// bool sdOK = initializeSDSimple();
