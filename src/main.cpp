// USBone WiFi - Complete version with web interface
// Enhanced with Adafruit GFX, WiFi AP, and Web Server
// Author: Alfonso E.M. & Wojciech Jasnos

#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <Adafruit_GFX.h>
#include "Display_ST7789.h"
#include "RGB_lamp.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SD_MMC.h>

// USB configuration
#if ARDUINO_USB_MODE
#warning "USB configured in native mode"
#endif
#include <vector>

// WiFi Configuration
#define WIFI_SSID "USBone"
#define WIFI_PASS "usbone01"
#define WIFI_HOSTNAME "usbone"
#define AUTH_USER "woj"
#define AUTH_PASS "woj"

// SD pins
#define SD_CMD    15
#define SD_CLK    14  
#define SD_D0     16
#define SD_D1     18
#define SD_D2     17
#define SD_D3     21
#define BOOT_BUTTON_PIN 0

// WiFi mode state
bool wifiMode = false;
AsyncWebServer* server = nullptr;

// GFX wrapper for Waveshare display
class WaveshareGFX : public Adafruit_GFX {
  public:
    WaveshareGFX() : Adafruit_GFX(LCD_WIDTH, LCD_HEIGHT) {}
    
    void begin() {
      LCD_Init();
      Set_Backlight(80);
    }
    
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
      if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
      LCD_addWindow(x, y, x, y, &color);
    }
    
    void fillScreen(uint16_t color) {
      fillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
    }
    
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
      if (w <= 0 || h <= 0) return;
      
      uint16_t* lineBuffer = new uint16_t[w];
      for (int16_t i = 0; i < w; i++) {
        lineBuffer[i] = color;
      }
      
      for (int16_t row = 0; row < h; row++) {
        if (y + row >= 0 && y + row < LCD_HEIGHT) {
          LCD_addWindow(x, y + row, x + w - 1, y + row, lineBuffer);
        }
      }
      delete[] lineBuffer;
    }
};

WaveshareGFX display;

// USB HID
USBHIDKeyboard keyboard;
bool usbHidEnabled = false;

// Forward declarations
void updateDisplay();
void loadMacrosFromSD();
bool initializeSD();
void createExampleMacros();
void handleSingleButton();
void injectMacro();

void sendSpecialChar(char c) {
  if (c == '@') {
    // Special handling for @ symbol (Polish keyboard: AltGr+2)
    keyboard.press(KEY_RIGHT_ALT);
    keyboard.press('2');
    delay(50);
    keyboard.releaseAll();
  } else {
    keyboard.write(c);
  }
}

bool needsSpecialHandling(char c) {
  return (c == '@');
}

// Global variables
std::vector<String> macros;
std::vector<String> macroNames;
std::vector<bool> macroSensitive;
int currentMacro = 0;

// Security variables
bool deviceLocked = true;
unsigned long lastActivity = 0;
const unsigned long autoLockTime = 30000;
int unlockPattern[] = {1, 2, 1};
int patternPos = 0;
unsigned long lastPatternPress = 0;
const unsigned long patternTimeout = 5000;

// Button variables
bool lastButtonState = HIGH;
unsigned long buttonPressTime = 0;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
const unsigned long longPressTime = 1000;
const unsigned long veryLongPressTime = 3000; // For WiFi toggle
bool buttonPressed = false;
bool longPressDetected = false;
bool veryLongPressDetected = false;

// Double-click detection variables
unsigned long lastClickTime = 0;
const unsigned long doubleClickWindow = 400; // Time window for double-click (400ms)
bool waitingForDoubleClick = false;

// Colors
#define COLOR_BG     0x0000
#define COLOR_TEXT   0xFFFF
#define COLOR_SELECT 0x07E0
#define COLOR_WARN   0xFBE0
#define COLOR_ERROR  0xF800
#define COLOR_LOCKED 0xF800
#define COLOR_WIFI   0x07FF  // Cyan for WiFi

void setLED(uint8_t r, uint8_t g, uint8_t b) {
  Set_Color(r, g, b);
}

void blinkLED(uint8_t r, uint8_t g, uint8_t b, int times = 3) {
  for (int i = 0; i < times; i++) {
    setLED(r, g, b);
    delay(200);
    setLED(0, 0, 0);
    delay(200);
  }
}

// Draw padlock icon
void drawPadlock(int16_t x, int16_t y, uint16_t color) {
  // Shackle (top arc)
  display.drawRect(x + 15, y, 30, 25, color);
  display.drawRect(x + 16, y + 1, 28, 23, color);
  display.fillRect(x + 17, y + 20, 26, 6, COLOR_BG);
  
  // Lock body
  display.fillRect(x, y + 20, 60, 50, color);
  display.fillRect(x + 2, y + 22, 56, 46, COLOR_BG);
  display.fillRect(x, y + 20, 60, 50, color);
  
  // Keyhole
  display.fillCircle(x + 30, y + 38, 6, COLOR_BG);
  display.fillRect(x + 27, y + 38, 6, 15, COLOR_BG);
  display.fillTriangle(x + 27, y + 53, x + 33, y + 53, x + 30, y + 58, COLOR_BG);
}

// Helper function to draw arc segments (simplified WiFi waves)
void drawArcSegment(int16_t cx, int16_t cy, int16_t r, int16_t thickness, uint16_t color) {
  // Draw quarter circle arcs for WiFi symbol (bottom-left quadrant)
  for (int16_t i = 0; i < thickness; i++) {
    for (int16_t angle = 225; angle <= 315; angle += 2) {
      float rad = angle * PI / 180.0;
      int16_t x1 = cx + (r + i) * cos(rad);
      int16_t y1 = cy + (r + i) * sin(rad);
      display.drawPixel(x1, y1, color);
    }
  }
}

// Draw WiFi icon
void drawWiFi(int16_t x, int16_t y, uint16_t color) {
  // WiFi waves - center dot
  display.fillCircle(x + 25, y + 35, 5, color);
  
  // Draw three arcs for WiFi symbol
  drawArcSegment(x + 25, y + 35, 12, 3, color);  // Inner arc
  drawArcSegment(x + 25, y + 35, 22, 3, color);  // Middle arc
  drawArcSegment(x + 25, y + 35, 32, 3, color);  // Outer arc
}

void showUnlockedAnimation() {
  display.fillScreen(COLOR_BG);
  
  display.setTextSize(3);
  display.setCursor(20, 60);
  display.setTextColor(COLOR_SELECT);
  display.println("UNLOCKED");
  
  display.fillCircle(LCD_WIDTH/2, 140, 30, COLOR_SELECT);
  display.fillTriangle(
    LCD_WIDTH/2 - 10, 140,
    LCD_WIDTH/2 - 5, 150,
    LCD_WIDTH/2 + 15, 125,
    COLOR_BG
  );
  display.fillTriangle(
    LCD_WIDTH/2 - 5, 145,
    LCD_WIDTH/2, 150,
    LCD_WIDTH/2 + 15, 120,
    COLOR_BG
  );
  
  delay(1500);
}

// HTML Page (stored in PROGMEM to save RAM)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>USBone Control Panel</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
            color: #ffffff;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        .header {
            text-align: center;
            padding: 30px 0;
            border-bottom: 2px solid rgba(255,255,255,0.1);
            margin-bottom: 30px;
        }
        .header h1 {
            font-size: 3em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }
        .header p { font-size: 1.2em; opacity: 0.8; }
        .card {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(10px);
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 25px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.3);
            border: 1px solid rgba(255,255,255,0.2);
        }
        .card h2 {
            font-size: 1.8em;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        textarea {
            width: 100%;
            min-height: 300px;
            background: rgba(0,0,0,0.3);
            border: 2px solid rgba(255,255,255,0.2);
            border-radius: 10px;
            color: #ffffff;
            font-family: 'Courier New', monospace;
            font-size: 14px;
            padding: 15px;
            resize: vertical;
            transition: all 0.3s;
        }
        textarea:focus {
            outline: none;
            border-color: #4CAF50;
            box-shadow: 0 0 10px rgba(76,175,80,0.3);
        }
        .button-group {
            display: flex;
            gap: 10px;
            margin-top: 15px;
            flex-wrap: wrap;
        }
        button {
            padding: 12px 30px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s;
            box-shadow: 0 4px 15px rgba(0,0,0,0.2);
        }
        .btn-primary { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }
        .btn-success { background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%); color: white; }
        .btn-info { background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%); color: white; }
        .btn-warning { background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%); color: white; }
        button:hover { transform: translateY(-2px); box-shadow: 0 6px 20px rgba(0,0,0,0.3); }
        button:active { transform: translateY(0); }
        button:disabled { opacity: 0.5; cursor: not-allowed; }
        .status {
            padding: 10px 15px;
            border-radius: 8px;
            margin-top: 15px;
            display: none;
        }
        .status.success { background: rgba(76,175,80,0.2); border: 1px solid #4CAF50; color: #4CAF50; }
        .status.error { background: rgba(244,67,54,0.2); border: 1px solid #f44336; color: #f44336; }
        .status.info { background: rgba(33,150,243,0.2); border: 1px solid #2196F3; color: #2196F3; }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        .info-item {
            background: rgba(0,0,0,0.2);
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }
        .info-item .label { font-size: 0.9em; opacity: 0.7; margin-bottom: 5px; }
        .info-item .value { font-size: 1.3em; font-weight: bold; }
        @media (max-width: 768px) {
            .header h1 { font-size: 2em; }
            .button-group { flex-direction: column; }
            button { width: 100%; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔐 USBone Control Panel</h1>
            <p>Secure USB HID Device Manager</p>
        </div>

        <div class="card">
            <h2>📊 Device Information</h2>
            <div class="info-grid">
                <div class="info-item">
                    <div class="label">WiFi Mode</div>
                    <div class="value">Access Point</div>
                </div>
                <div class="info-item">
                    <div class="label">IP Address</div>
                    <div class="value">192.168.4.1</div>
                </div>
                <div class="info-item">
                    <div class="label">Hostname</div>
                    <div class="value">usbone.local</div>
                </div>
                <div class="info-item">
                    <div class="label">Status</div>
                    <div class="value">🟢 Online</div>
                </div>
            </div>
        </div>

        <div class="card">
            <h2>📝 Macro Editor</h2>
            <p style="opacity: 0.8; margin-bottom: 15px;">Edit your macros.txt file. Format: NAME:CONTENT or SENSITIVE:NAME:CONTENT</p>
            <textarea id="macroEditor" placeholder="Loading macros..."></textarea>
            <div class="button-group">
                <button class="btn-success" onclick="saveMacros()">💾 Save to SD Card</button>
                <button class="btn-info" onclick="loadMacros()">🔄 Reload from SD</button>
                <button class="btn-warning" onclick="clearEditor()">🗑️ Clear Editor</button>
            </div>
            <div id="editorStatus" class="status"></div>
        </div>

        <div class="card">
            <h2>⚡ Live Text Injector</h2>
            <p style="opacity: 0.8; margin-bottom: 15px;">Type or paste text here and send it directly to the host computer via USB HID</p>
            <textarea id="liveText" placeholder="Enter text to inject...

Supports:
• Multiple paragraphs
• Special characters
• Tab and Enter keys
• Long texts (up to 10KB)

This text will be typed on the host computer as if you typed it manually."></textarea>
            <div class="button-group">
                <button class="btn-primary" onclick="sendText()">🚀 Send to Host Computer</button>
                <button class="btn-info" onclick="clearLive()">🗑️ Clear</button>
            </div>
            <div id="liveStatus" class="status"></div>
        </div>

        <div class="card">
            <h2>❓ Quick Help</h2>
            <div style="line-height: 1.8;">
                <p><strong>Macro Format:</strong></p>
                <ul style="margin-left: 20px; margin-top: 10px;">
                    <li>Regular: <code style="background: rgba(0,0,0,0.3); padding: 2px 8px; border-radius: 4px;">Email:user@example.com</code></li>
                    <li>Sensitive: <code style="background: rgba(0,0,0,0.3); padding: 2px 8px; border-radius: 4px;">SENSITIVE:Password:Secret123</code></li>
                    <li>With Enter: <code style="background: rgba(0,0,0,0.3); padding: 2px 8px; border-radius: 4px;">Login:user\tpass\n</code></li>
                </ul>
                <p style="margin-top: 15px;"><strong>Live Injector:</strong> Type multi-line text and click Send. It will be typed on your computer!</p>
                <p style="margin-top: 15px;"><strong>Toggle WiFi:</strong> Hold BOOT button for 3+ seconds on device</p>
            </div>
        </div>
    </div>

    <script>
        window.onload = loadMacros;

        function showStatus(elementId, message, type) {
            const status = document.getElementById(elementId);
            status.textContent = message;
            status.className = 'status ' + type;
            status.style.display = 'block';
            setTimeout(() => { status.style.display = 'none'; }, 5000);
        }

        async function loadMacros() {
            try {
                const response = await fetch('/api/macros');
                if (response.ok) {
                    const text = await response.text();
                    document.getElementById('macroEditor').value = text;
                    showStatus('editorStatus', '✅ Macros loaded successfully', 'success');
                } else {
                    showStatus('editorStatus', '❌ Failed to load macros', 'error');
                }
            } catch (error) {
                showStatus('editorStatus', '❌ Error: ' + error.message, 'error');
            }
        }

        async function saveMacros() {
            const content = document.getElementById('macroEditor').value;
            try {
                const response = await fetch('/api/macros', {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain' },
                    body: content
                });
                if (response.ok) {
                    showStatus('editorStatus', '✅ Macros saved successfully!', 'success');
                } else {
                    showStatus('editorStatus', '❌ Failed to save macros', 'error');
                }
            } catch (error) {
                showStatus('editorStatus', '❌ Error: ' + error.message, 'error');
            }
        }

        async function sendText() {
            const text = document.getElementById('liveText').value;
            if (!text) {
                showStatus('liveStatus', '⚠️ Please enter some text first', 'info');
                return;
            }
            
            showStatus('liveStatus', '📤 Sending text to host...', 'info');
            
            try {
                const response = await fetch('/api/inject', {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain' },
                    body: text
                });
                if (response.ok) {
                    showStatus('liveStatus', '✅ Text sent successfully!', 'success');
                } else {
                    showStatus('liveStatus', '❌ Failed to send text', 'error');
                }
            } catch (error) {
                showStatus('liveStatus', '❌ Error: ' + error.message, 'error');
            }
        }

        function clearEditor() {
            if (confirm('Clear the macro editor? This will not delete the file.')) {
                document.getElementById('macroEditor').value = '';
            }
        }

        function clearLive() {
            document.getElementById('liveText').value = '';
        }
    </script>
</body>
</html>
)rawliteral";

void checkUnlock(bool isLongPress) {
  unsigned long now = millis();
  
  if (now - lastPatternPress > patternTimeout && patternPos > 0) {
    patternPos = 0;
    Serial.println("Pattern timeout - reset");
  }
  
  lastPatternPress = now;
  
  int press = isLongPress ? 2 : 1;
  
  if (press == unlockPattern[patternPos]) {
    patternPos++;
    Serial.print("Pattern progress: ");
    Serial.print(patternPos);
    Serial.print("/");
    Serial.println(sizeof(unlockPattern) / sizeof(unlockPattern[0]));
    blinkLED(0, 0, 255, 1);
    
    if (patternPos >= sizeof(unlockPattern) / sizeof(unlockPattern[0])) {
      deviceLocked = false;
      patternPos = 0;
      lastActivity = now;
      
      showUnlockedAnimation();
      
      blinkLED(0, 255, 0, 3);
      setLED(0, 255, 0);  // Green when unlocked
      Serial.println("Device UNLOCKED!");
      updateDisplay();
    }
  } else {
    patternPos = 0;
    Serial.println("Wrong pattern - reset");
    blinkLED(255, 0, 0, 2);
    setLED(255, 0, 0);  // Red when locked
  }
}

void initWiFi() {
  Serial.println("Starting WiFi AP...");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);
  
  if (!MDNS.begin(WIFI_HOSTNAME)) {
    Serial.println("mDNS failed!");
  } else {
    Serial.println("mDNS started: " + String(WIFI_HOSTNAME) + ".local");
  }
  
  // Create and configure web server
  server = new AsyncWebServer(80);
  
  // Enable authentication for all routes
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(AUTH_USER, AUTH_PASS)) {
      return request->requestAuthentication();
    }
    request->send(200, "text/html", index_html);
  });
  
  // API endpoint to get macros
  server->on("/api/macros", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(AUTH_USER, AUTH_PASS)) {
      return request->requestAuthentication();
    }
    
    File file = SD_MMC.open("/macros.txt", FILE_READ);
    if (!file) {
      request->send(404, "text/plain", "macros.txt not found");
      return;
    }
    
    String content = file.readString();
    file.close();
    request->send(200, "text/plain", content);
  });
  
  // API endpoint to save macros
  server->on("/api/macros", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!request->authenticate(AUTH_USER, AUTH_PASS)) {
        return request->requestAuthentication();
      }
      
      File file;
      if (index == 0) {
        // First chunk - create new file (truncate existing)
        file = SD_MMC.open("/macros.txt", FILE_WRITE);
        if (!file) {
          request->send(500, "text/plain", "Failed to create file");
          return;
        }
      } else {
        // Subsequent chunks - append to file
        file = SD_MMC.open("/macros.txt", FILE_APPEND);
        if (!file) {
          request->send(500, "text/plain", "Failed to open file for append");
          return;
        }
      }
      
      // Write data chunk
      size_t written = file.write(data, len);
      file.close();
      
      if (written != len) {
        request->send(500, "text/plain", "Failed to write all data");
        return;
      }
      
      if (index + len == total) {
        // Last chunk - reload macros and send response
        loadMacrosFromSD();
        request->send(200, "text/plain", "Saved successfully");
        Serial.println("Macros saved from web UI");
      }
    }
  );
  
  // API endpoint to inject text
  server->on("/api/inject", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!request->authenticate(AUTH_USER, AUTH_PASS)) {
        return request->requestAuthentication();
      }
      
      if (!usbHidEnabled) {
        request->send(400, "text/plain", "USB HID not enabled");
        return;
      }
      
      // Inject the text
      String text = "";
      for (size_t i = 0; i < len; i++) {
        text += (char)data[i];
      }
      
      Serial.println("Injecting text from web: " + String(len) + " bytes");
      
      for (int i = 0; i < text.length(); i++) {
        char c = text.charAt(i);
        
        if (c == '\n') {
          keyboard.press(KEY_RETURN);
          delay(50);
          keyboard.releaseAll();
        } else if (c == '\t') {
          keyboard.press(KEY_TAB);
          delay(50);
          keyboard.releaseAll();
        } else if (needsSpecialHandling(c)) {
          sendSpecialChar(c);
        } else {
          keyboard.write(c);
        }
        delay(20); // Faster typing for web injection
      }
      
      if (index + len == total) {
        request->send(200, "text/plain", "Injected");
      }
    }
  );
  
  server->begin();
  Serial.println("Web server started");
  
  wifiMode = true;
}

void stopWiFi() {
  if (server) {
    delete server;
    server = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiMode = false;
  Serial.println("WiFi stopped");
}

void toggleWiFi() {
  if (wifiMode) {
    stopWiFi();
    if (deviceLocked) {
      setLED(255, 0, 0);  // Red when locked
    } else {
      setLED(0, 255, 0);  // Green when unlocked
    }
  } else {
    initWiFi();
    setLED(128, 0, 128); // Purple for WiFi mode
  }
  updateDisplay();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== USBone WiFi Starting ===");

  setLED(0, 0, 255);
  Serial.println("RGB LED OK");
  
  display.begin();
  LCD_WriteCommand(0x36);
  LCD_WriteData(0xC0);
  
  display.fillScreen(COLOR_BG);
  display.setTextColor(COLOR_TEXT);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.println("USBone WiFi");
  display.setTextSize(1);
  display.println("\nInitializing...");
  Serial.println("LCD OK");
  
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button OK");

  bool sdOK = initializeSD();
  if (!sdOK) {
    Serial.println("SD ERROR");
    blinkLED(255, 0, 0, 2);
  } else {
    Serial.println("SD OK");
    blinkLED(0, 255, 0, 1);
  }

  if (sdOK) {
    loadMacrosFromSD();
    if (macros.size() == 0) {
      createExampleMacros();
      loadMacrosFromSD();
    }
  } else {
    macroNames.push_back("Test1");
    macros.push_back("Hello world");
    macroSensitive.push_back(false);
    macroNames.push_back("Test2");
    macros.push_back("admin\tpassword123\n");
    macroSensitive.push_back(true);
  }
  Serial.println("Macros: " + String(macros.size()));

  pinMode(0, INPUT_PULLUP);
  delay(100);
  
  bool bootPressed = (digitalRead(0) == LOW);
  
  if (bootPressed) {
    Serial.println("PROGRAMMING MODE");
    usbHidEnabled = false;
    setLED(255, 128, 0);  // Orange when in programming mode
    deviceLocked = false;
  } else {
    Serial.println("USB HID MODE");
    USB.begin();
    keyboard.begin();
    delay(2000);
    usbHidEnabled = true;
    setLED(255, 0, 0);  // Red when locked
  }
  
  updateDisplay();
  Serial.println("=== Ready ===");
  if (deviceLocked) {
    Serial.println("*** DEVICE LOCKED ***");
    Serial.println("Hold BOOT 3s for WiFi");
  }
}

void loop() {
  if (!deviceLocked && !wifiMode && usbHidEnabled && millis() - lastActivity > autoLockTime) {
    deviceLocked = true;
    setLED(255, 0, 0);  // Red when locked
    Serial.println("*** AUTO-LOCKED ***");
    updateDisplay();
  }
  
  handleSingleButton();
  delay(50);
}

bool initializeSD() {
  if(!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3)){
    return false;
  }

  if (SD_MMC.begin("/sdcard", true)) {
    if (SD_MMC.cardType() == CARD_NONE) {
      SD_MMC.end();
      return false;
    }
    return true;
  }
  return false;
}

void loadMacrosFromSD() {
  macros.clear();
  macroNames.clear();
  macroSensitive.clear();
  
  fs::File file = SD_MMC.open("/macros.txt");
  if (!file) return;

  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    
    if (line.length() > 0 && !line.startsWith("#")) {
      bool isSensitive = false;
      if (line.startsWith("SENSITIVE:")) {
        isSensitive = true;
        line = line.substring(10);
        Serial.println("Found SENSITIVE macro in line");
      }
      
      int colonPos = line.indexOf(':');
      if (colonPos > 0) {
        String name = line.substring(0, colonPos);
        String content = line.substring(colonPos + 1);
        
        content.replace("\\n", "\n");
        content.replace("\\t", "\t");
        content.replace("\\\\", "\\");
        
        macroNames.push_back(name);
        macros.push_back(content);
        macroSensitive.push_back(isSensitive);
        
        if (isSensitive) {
          Serial.println("  → Name: " + name + " (SENSITIVE)");
        } else {
          Serial.println("  → Name: " + name);
        }
      }
    }
  }
  file.close();
  
  int sensitiveCount = 0;
  for (bool s : macroSensitive) {
    if (s) sensitiveCount++;
  }
  
  Serial.println("Loaded " + String(macros.size()) + " macros (" + 
                 String(sensitiveCount) + " sensitive)");
}

void createExampleMacros() {
  fs::File file = SD_MMC.open("/macros.txt", FILE_WRITE);
  if (!file) return;

  file.println("# USBone Macro File");
  file.println("# Format: NAME:CONTENT");
  file.println("# For sensitive macros: SENSITIVE:NAME:CONTENT");
  file.println("#");
  file.println("# Special sequences:");
  file.println("#   \\n = Enter key");
  file.println("#   \\t = Tab key");
  file.println("#");
  file.println("");
  file.println("# Regular macros");
  file.println("Email:user@example.com");
  file.println("Username:john_doe");
  file.println("");
  file.println("# Sensitive macros");
  file.println("SENSITIVE:Password:MySecretPassword123!");
  file.println("SENSITIVE:API_Key:sk-1234567890abcdef");
  file.println("SENSITIVE:BankLogin:admin\\tSecurePass456\\n");
  file.close();
}

void handleSingleButton() {
  bool currentState = digitalRead(BOOT_BUTTON_PIN);
  unsigned long currentTime = millis();
  
  // Handle double-click timeout
  if (waitingForDoubleClick && (currentTime - lastClickTime > doubleClickWindow)) {
    // Single click timeout - execute single click action
    waitingForDoubleClick = false;
    
    if (!wifiMode && !deviceLocked && macros.size() > 0) {
      // Execute single click: next macro
      lastActivity = currentTime;
      currentMacro = (currentMacro + 1) % macros.size();
      blinkLED(0, 0, 255, 1);
      setLED(0, 255, 0);  // Green when unlocked
      updateDisplay();
    }
  }
  
  if (currentState != lastButtonState) {
    if (currentTime - lastDebounceTime > debounceDelay) {
      
      if (currentState == LOW) {
        buttonPressed = true;
        buttonPressTime = currentTime;
        longPressDetected = false;
        veryLongPressDetected = false;
        
      } else {
        if (buttonPressed) {
          unsigned long pressDuration = currentTime - buttonPressTime;
          
          if (pressDuration >= veryLongPressTime) {
            // VERY LONG PRESS - Toggle WiFi
            waitingForDoubleClick = false;  // Cancel any pending double-click
            toggleWiFi();
            blinkLED(128, 0, 128, 3);  // Purple blink for WiFi
          } else if (pressDuration >= longPressTime) {
            // LONG PRESS - Inject macro or unlock pattern
            waitingForDoubleClick = false;  // Cancel any pending double-click
            if (wifiMode) {
              // In WiFi mode, ignore
            } else if (deviceLocked) {
              checkUnlock(true);
            } else {
              if (macros.size() > 0) {
                lastActivity = currentTime;
                injectMacro();
              }
            }
          } else {
            // SHORT PRESS - Handle single/double click
            if (wifiMode) {
              // In WiFi mode, ignore short presses
            } else if (deviceLocked) {
              checkUnlock(false);
            } else {
              // Check for double-click
              if (waitingForDoubleClick && (currentTime - lastClickTime <= doubleClickWindow)) {
                // DOUBLE CLICK DETECTED - Previous macro
                waitingForDoubleClick = false;
                if (macros.size() > 0) {
                  lastActivity = currentTime;
                  currentMacro = (currentMacro - 1 + macros.size()) % macros.size();
                  blinkLED(0, 255, 255, 2);  // Cyan blink for backward
                  setLED(0, 255, 0);  // Green when unlocked
                  updateDisplay();
                  Serial.println("Double-click: Previous macro");
                }
              } else {
                // First click - wait for potential double-click
                waitingForDoubleClick = true;
                lastClickTime = currentTime;
              }
            }
          }
          buttonPressed = false;
        }
      }
      
      lastDebounceTime = currentTime;
      lastButtonState = currentState;
    }
  }
  
  // Visual feedback for long press detection
  if (buttonPressed && currentState == LOW) {
    unsigned long pressDuration = currentTime - buttonPressTime;
    if (pressDuration >= veryLongPressTime && !veryLongPressDetected) {
      veryLongPressDetected = true;
      waitingForDoubleClick = false;  // Cancel any pending double-click
      setLED(128, 0, 128); // Purple = WiFi toggle
    } else if (pressDuration >= longPressTime && !longPressDetected && !deviceLocked) {
      longPressDetected = true;
      waitingForDoubleClick = false;  // Cancel any pending double-click
      setLED(255, 0, 255); // Magenta = inject
    }
  }
}

void injectMacro() {
  String macro = macros[currentMacro];
  setLED(255, 0, 255);  // Magenta during injection

  if (!usbHidEnabled) {
    Serial.println("USB HID disabled");
    blinkLED(255, 255, 0, 3);
    setLED(0, 255, 0);  // Green when unlocked
    return;
  }

  Serial.println("Injecting: " + macroNames[currentMacro]);

  for (int i = 0; i < macro.length(); i++) {
    char c = macro.charAt(i);
    
    if (c == '\n') {
      keyboard.press(KEY_RETURN);
      delay(50);
      keyboard.releaseAll();
    } else if (c == '\t') {
      keyboard.press(KEY_TAB);
      delay(50);
      keyboard.releaseAll();
    } else if (needsSpecialHandling(c)) {
      sendSpecialChar(c);
    } else {
      keyboard.write(c);
    }
    delay(50);
  }

  blinkLED(0, 255, 0, 2);
  setLED(0, 255, 0);  // Green when unlocked
  Serial.println("Injection completed");
}

void updateDisplay() {
  display.fillScreen(COLOR_BG);
  
  // Title
  display.setTextSize(3);
  display.setCursor(10, 15);
  display.setTextColor(COLOR_TEXT);
  display.println("USBone");
  
  // WiFi Mode
  if (wifiMode) {
    drawWiFi(LCD_WIDTH/2 - 25, 70, COLOR_WIFI);
    
    display.setTextSize(2);
    display.setCursor(10, 140);
    display.setTextColor(COLOR_WIFI);
    display.println("WiFi Mode");
    
    display.setTextSize(1);
    display.setCursor(10, 170);
    display.setTextColor(COLOR_TEXT);
    display.println("SSID: USBone");
    display.setCursor(10, 185);
    display.println("Pass: usbone01");
    display.setCursor(10, 200);
    display.println("IP: 192.168.4.1");
    display.setCursor(10, 215);
    display.println("http://usbone.local");
    
    display.setCursor(10, 240);
    display.setTextColor(COLOR_WARN);
    display.println("Hold BOOT 3s to exit");
    return;
  }
  
  // Lock Screen
  if (deviceLocked) {
    drawPadlock(LCD_WIDTH/2 - 30, 70, COLOR_LOCKED);
    
    display.setTextSize(3);
    display.setCursor(20, 140);
    display.setTextColor(COLOR_LOCKED);
    display.println("LOCKED");
    
    display.setTextSize(2);
    display.setCursor(10, 180);
    display.setTextColor(COLOR_WARN);
    display.println("Please");
    display.setCursor(10, 205);
    display.println("unlock");
    
    display.setTextSize(1);
    display.setCursor(10, 240);
    display.setTextColor(0x7BEF);
    display.println("S-L-S | BOOT 3s=WiFi");
    return;
  }
  
  // Separator
  display.fillRect(10, 55, LCD_WIDTH - 20, 2, COLOR_SELECT);
  
  if (macros.size() > 0) {
    display.setTextSize(2);
    display.setCursor(10, 75);
    display.setTextColor(COLOR_SELECT);
    display.print("Macro ");
    display.print(currentMacro + 1);
    display.print("/");
    display.println(macros.size());
    
    display.setTextSize(3);
    display.setCursor(10, 110);
    
    if (macroSensitive[currentMacro]) {
      display.setTextColor(COLOR_WARN);
      display.print("[S] ");
    } else {
      display.setTextColor(COLOR_TEXT);
    }
    
    String displayName = macroNames[currentMacro];
    if (displayName.length() > 9) {
      displayName = displayName.substring(0, 9) + "...";
    }
    display.println(displayName);
    
    display.setTextSize(2);
    display.setCursor(10, 160);
    display.setTextColor(0x7BEF);
    
    String preview;
    if (macroSensitive[currentMacro]) {
      int contentLength = macros[currentMacro].length();
      if (contentLength > 20) contentLength = 20;
      preview = "";
      for (int i = 0; i < contentLength; i++) {
        preview += "*";
      }
    } else {
      preview = macros[currentMacro];
      preview.replace("\n", " ");
      preview.replace("\t", " ");
      if (preview.length() > 15) {
        preview = preview.substring(0, 15) + "...";
      }
    }
    display.println(preview);
  } else {
    display.setTextSize(2);
    display.setCursor(10, 75);
    display.setTextColor(COLOR_WARN);
    display.println("No macros");
  }
  
  display.setTextSize(2);
  display.setCursor(10, LCD_HEIGHT - 35);
  if (usbHidEnabled) {
    display.setTextColor(COLOR_SELECT);
    display.println("Ready");
  } else {
    display.setTextColor(COLOR_WARN);
    display.println("PROG Mode");
  }
}
