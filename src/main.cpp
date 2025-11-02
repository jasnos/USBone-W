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
#include "crypto_manager.h"
#include <vector>
#include <algorithm>

// USB configuration
#if ARDUINO_USB_MODE
#warning "USB configured in native mode"
#endif

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
bool saveMacrosToSD(const String& content);
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

// SD Card state
bool sdCardAvailable = false;

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

// HTML Page (stored in PROGMEM to save RAM) - Modern Cyber-Tech Dark Theme
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>USBone Control Panel</title>
    <link href="https://fonts.googleapis.com/css2?family=Orbitron:wght@400;700;900&family=Rajdhani:wght@300;400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-dark: #0a0e27;
            --bg-darker: #060916;
            --bg-card: #141b34;
            --accent-primary: #00d4ff;
            --accent-secondary: #7b2cbf;
            --accent-success: #00ff88;
            --accent-danger: #ff0055;
            --accent-warning: #ffaa00;
            --text-primary: #ffffff;
            --text-secondary: #a0aec0;
            --border-color: rgba(0, 212, 255, 0.2);
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Rajdhani', sans-serif;
            background: linear-gradient(135deg, var(--bg-darker) 0%, var(--bg-dark) 100%);
            color: var(--text-primary);
            min-height: 100vh;
            overflow-x: hidden;
        }
        
        body::before {
            content: '';
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: 
                radial-gradient(circle at 20% 50%, rgba(123, 44, 191, 0.1) 0%, transparent 50%),
                radial-gradient(circle at 80% 80%, rgba(0, 212, 255, 0.1) 0%, transparent 50%);
            pointer-events: none;
            z-index: 0;
        }
        
        nav {
            position: sticky;
            top: 0;
            background: rgba(10, 14, 39, 0.95);
            backdrop-filter: blur(20px);
            border-bottom: 2px solid var(--border-color);
            padding: 0;
            z-index: 1000;
            box-shadow: 0 10px 40px rgba(0, 212, 255, 0.1);
        }
        
        .nav-container {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0 30px;
            height: 80px;
        }
        
        .logo {
            font-family: 'Orbitron', sans-serif;
            font-size: 2.2em;
            font-weight: 900;
            background: linear-gradient(135deg, var(--accent-primary) 0%, var(--accent-secondary) 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
            letter-spacing: 3px;
            text-shadow: 0 0 30px rgba(0, 212, 255, 0.5);
            cursor: pointer;
            transition: all 0.3s;
        }
        
        .logo:hover {
            transform: scale(1.05);
            filter: brightness(1.2);
        }
        
        .nav-menu {
            display: flex;
            gap: 5px;
            list-style: none;
        }
        
        .nav-menu li {
            position: relative;
        }
        
        .nav-menu a {
            display: block;
            padding: 12px 25px;
            color: var(--text-secondary);
            text-decoration: none;
            font-weight: 600;
            font-size: 1.1em;
            border-radius: 10px;
            transition: all 0.3s;
            position: relative;
            overflow: hidden;
            cursor: pointer;
        }
        
        .nav-menu a::before {
            content: '';
            position: absolute;
            top: 0;
            left: -100%;
            width: 100%;
            height: 100%;
            background: linear-gradient(90deg, transparent, rgba(0, 212, 255, 0.2), transparent);
            transition: left 0.5s;
            pointer-events: none;
            z-index: -1;
        }
        
        .nav-menu a:hover::before {
            left: 100%;
        }
        
        .nav-menu a:hover,
        .nav-menu a.active {
            color: var(--accent-primary);
            background: rgba(0, 212, 255, 0.1);
            box-shadow: 0 0 20px rgba(0, 212, 255, 0.2);
        }
        
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 40px 30px;
            position: relative;
            z-index: 1;
        }
        
        .tab-content {
            display: none;
            animation: fadeIn 0.5s;
        }
        
        .tab-content.active {
            display: block;
        }
        
        @keyframes fadeIn {
            from {
                opacity: 0;
                transform: translateY(20px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }
        
        .card {
            background: var(--bg-card);
            border: 2px solid var(--border-color);
            border-radius: 20px;
            padding: 35px;
            margin-bottom: 30px;
            box-shadow: 
                0 10px 40px rgba(0, 0, 0, 0.4),
                inset 0 1px 0 rgba(255, 255, 255, 0.05);
            position: relative;
            overflow: hidden;
            transition: all 0.3s;
        }
        
        .card::before {
            content: '';
            position: absolute;
            top: -50%;
            right: -50%;
            width: 200%;
            height: 200%;
            background: radial-gradient(circle, rgba(0, 212, 255, 0.05) 0%, transparent 70%);
            opacity: 0;
            transition: opacity 0.5s;
            pointer-events: none;
            z-index: 1;
        }
        
        .card:hover::before {
            opacity: 1;
        }
        
        .card:hover {
            border-color: var(--accent-primary);
            box-shadow: 
                0 15px 50px rgba(0, 212, 255, 0.2),
                inset 0 1px 0 rgba(255, 255, 255, 0.1);
            transform: translateY(-5px);
        }
        
        .card-title {
            font-family: 'Orbitron', sans-serif;
            font-size: 1.8em;
            font-weight: 700;
            margin-bottom: 25px;
            display: flex;
            align-items: center;
            gap: 15px;
            color: var(--accent-primary);
            text-transform: uppercase;
            letter-spacing: 2px;
            position: relative;
            z-index: 2;
        }
        
        .card-title::before {
            content: '';
            width: 5px;
            height: 30px;
            background: linear-gradient(180deg, var(--accent-primary) 0%, var(--accent-secondary) 100%);
            border-radius: 3px;
        }
        
        /* Ensure all card content is above decorative elements */
        .card > * {
            position: relative;
            z-index: 2;
        }
        
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        
        .info-item {
            background: rgba(0, 212, 255, 0.05);
            border: 1px solid rgba(0, 212, 255, 0.2);
            border-radius: 15px;
            padding: 25px;
            text-align: center;
            transition: all 0.3s;
            position: relative;
            overflow: hidden;
        }
        
        .info-item::before {
            content: '';
            position: absolute;
            top: 0;
            left: -100%;
            width: 100%;
            height: 100%;
            background: linear-gradient(90deg, transparent, rgba(0, 212, 255, 0.1), transparent);
            transition: left 0.8s;
            pointer-events: none;
        }
        
        .info-item:hover::before {
            left: 100%;
        }
        
        .info-item:hover {
            transform: scale(1.05);
            background: rgba(0, 212, 255, 0.1);
            box-shadow: 0 0 30px rgba(0, 212, 255, 0.3);
        }
        
        .info-label {
            font-size: 0.95em;
            color: var(--text-secondary);
            margin-bottom: 10px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .info-value {
            font-size: 1.6em;
            font-weight: 700;
            color: var(--accent-primary);
            font-family: 'Orbitron', sans-serif;
        }
        
        textarea {
            width: 100%;
            min-height: 350px;
            background: rgba(0, 0, 0, 0.4);
            border: 2px solid var(--border-color);
            border-radius: 15px;
            color: var(--text-primary);
            font-family: 'Courier New', monospace;
            font-size: 15px;
            padding: 20px;
            resize: vertical;
            transition: all 0.3s;
            position: relative;
            z-index: 10;
        }
        
        textarea:focus {
            outline: none;
            border-color: var(--accent-primary);
            background: rgba(0, 212, 255, 0.05);
            box-shadow: 
                0 0 30px rgba(0, 212, 255, 0.2),
                inset 0 0 20px rgba(0, 212, 255, 0.05);
            z-index: 11;
        }
        
        textarea::placeholder {
            color: var(--text-secondary);
            opacity: 0.5;
        }
        
        .button-group {
            display: flex;
            gap: 15px;
            margin-top: 20px;
            flex-wrap: wrap;
        }
        
        button {
            padding: 15px 35px;
            border: none;
            border-radius: 12px;
            font-size: 1.1em;
            font-weight: 700;
            font-family: 'Rajdhani', sans-serif;
            cursor: pointer;
            transition: all 0.3s;
            position: relative;
            overflow: hidden;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        button::before {
            content: '';
            position: absolute;
            top: 50%;
            left: 50%;
            width: 0;
            height: 0;
            border-radius: 50%;
            background: rgba(255, 255, 255, 0.3);
            transform: translate(-50%, -50%);
            transition: width 0.6s, height 0.6s;
            pointer-events: none;
            z-index: 0;
        }
        
        button:hover::before {
            width: 300px;
            height: 300px;
        }
        
        button span {
            position: relative;
            z-index: 1;
        }
        
        .btn-primary {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            box-shadow: 0 5px 20px rgba(102, 126, 234, 0.4);
        }
        
        .btn-success {
            background: linear-gradient(135deg, #00ff88 0%, #00cc66 100%);
            color: #0a0e27;
            box-shadow: 0 5px 20px rgba(0, 255, 136, 0.4);
        }
        
        .btn-info {
            background: linear-gradient(135deg, #00d4ff 0%, #0099cc 100%);
            color: #0a0e27;
            box-shadow: 0 5px 20px rgba(0, 212, 255, 0.4);
        }
        
        .btn-warning {
            background: linear-gradient(135deg, #ffaa00 0%, #ff6600 100%);
            color: white;
            box-shadow: 0 5px 20px rgba(255, 170, 0, 0.4);
        }
        
        button:hover {
            transform: translateY(-3px);
            box-shadow: 0 8px 30px rgba(0, 212, 255, 0.5);
        }
        
        button:active {
            transform: translateY(0);
        }
        
        .status {
            padding: 15px 20px;
            border-radius: 12px;
            margin-top: 20px;
            display: none;
            font-weight: 600;
            animation: slideIn 0.3s;
        }
        
        @keyframes slideIn {
            from {
                opacity: 0;
                transform: translateX(-20px);
            }
            to {
                opacity: 1;
                transform: translateX(0);
            }
        }
        
        .status.success {
            background: rgba(0, 255, 136, 0.15);
            border: 2px solid var(--accent-success);
            color: var(--accent-success);
        }
        
        .status.error {
            background: rgba(255, 0, 85, 0.15);
            border: 2px solid var(--accent-danger);
            color: var(--accent-danger);
        }
        
        .status.info {
            background: rgba(0, 212, 255, 0.15);
            border: 2px solid var(--accent-primary);
            color: var(--accent-primary);
        }
        
        .loading {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 3px solid rgba(0, 212, 255, 0.3);
            border-top-color: var(--accent-primary);
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
            margin-left: 10px;
        }
        
        @keyframes spin {
            to { transform: rotate(360deg); }
        }
        
        @media (max-width: 768px) {
            .nav-container {
                padding: 0 20px;
            }
            
            .logo {
                font-size: 1.8em;
            }
            
            .nav-menu a {
                padding: 10px 15px;
                font-size: 1em;
            }
            
            .container {
                padding: 20px 15px;
            }
            
            .card {
                padding: 25px;
            }
            
            .button-group {
                flex-direction: column;
            }
            
            button {
                width: 100%;
            }
        }
    </style>
</head>
<body>
    <nav>
        <div class="nav-container">
            <div class="logo">USBone</div>
            <ul class="nav-menu">
                <li><a class="active" onclick="showTab('info')">Device</a></li>
                <li><a onclick="showTab('editor')">Editor</a></li>
                <li><a onclick="showTab('injector')">Injector</a></li>
            </ul>
        </div>
    </nav>

    <div class="container">
        <!-- Device Information Tab -->
        <div id="info" class="tab-content active">
            <div class="card">
                <h2 class="card-title">Device Status</h2>
                <div class="info-grid">
                    <div class="info-item">
                        <div class="info-label">WiFi Mode</div>
                        <div class="info-value">AP Mode</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">IP Address</div>
                        <div class="info-value">192.168.4.1</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">Status</div>
                        <div class="info-value">🟢 Online</div>
                    </div>
                    <div class="info-item">
                        <div class="info-label">SD Card</div>
                        <div class="info-value" id="sdStatus">Checking...</div>
                    </div>
                </div>
                <p style="color: var(--text-secondary); margin-top: 20px;">
                    Hold BOOT button for 3+ seconds to toggle WiFi mode. Device auto-locks after 30 seconds of inactivity.
                </p>
            </div>
        </div>
        
        <!-- Macro Editor Tab -->
        <div id="editor" class="tab-content">
            <div class="card">
                <h2 class="card-title">Macro Editor</h2>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Edit macros stored on SD card. Format: <code style="background: rgba(0,0,0,0.4); padding: 3px 10px; border-radius: 5px; color: var(--accent-success);">NAME:CONTENT</code> or <code style="background: rgba(0,0,0,0.4); padding: 3px 10px; border-radius: 5px; color: var(--accent-danger);">SENSITIVE:NAME:CONTENT</code>
                </p>
                <textarea id="macroEditor" placeholder="Loading macros from SD card..."></textarea>
                <div class="button-group">
                    <button class="btn-success" onclick="saveMacros()">
                        <span>💾 Save to SD</span>
                    </button>
                    <button class="btn-info" onclick="loadMacros()">
                        <span>🔄 Reload</span>
                    </button>
                    <button class="btn-warning" onclick="clearEditor()">
                        <span>🗑️ Clear</span>
                    </button>
                </div>
                <div id="editorStatus" class="status"></div>
            </div>
        </div>
        
        <!-- Live Injector Tab -->
        <div id="injector" class="tab-content">
            <div class="card">
                <h2 class="card-title">Live Text Injector</h2>
                <p style="color: var(--text-secondary); margin-bottom: 20px;">
                    Type or paste text to send directly to the host computer via USB HID
                </p>
                <textarea id="liveText" placeholder="Enter text to inject...

Supports:
• Multiple paragraphs
• Special characters  
• Tab and Enter keys
• Long texts (up to 10KB)"></textarea>
                <div class="button-group">
                    <button class="btn-primary" onclick="sendText()">
                        <span>🚀 Send to Host</span>
                    </button>
                    <button class="btn-warning" onclick="clearLive()">
                        <span>🗑️ Clear</span>
                    </button>
                </div>
                <div id="liveStatus" class="status"></div>
            </div>
        </div>
    </div>

    <script>
        function showTab(tabName) {
            // Hide all tabs
            document.querySelectorAll('.tab-content').forEach(tab => {
                tab.classList.remove('active');
            });
            
            // Remove active class from all nav links
            document.querySelectorAll('.nav-menu a').forEach(link => {
                link.classList.remove('active');
            });
            
            // Show selected tab
            document.getElementById(tabName).classList.add('active');
            
            // Highlight active nav link
            event.target.classList.add('active');
            
            // Load content if needed
            if (tabName === 'editor' && !document.getElementById('macroEditor').value) {
                loadMacros();
            }
            if (tabName === 'info') {
                checkSDStatus();
            }
        }
        
        function checkSDStatus() {
            fetch('/test').then(response => response.text()).then(data => {
                if (data.includes('SD Card Available: Yes')) {
                    document.getElementById('sdStatus').textContent = '✅ Ready';
                    document.getElementById('sdStatus').style.color = 'var(--accent-success)';
                } else {
                    document.getElementById('sdStatus').textContent = '❌ Error';
                    document.getElementById('sdStatus').style.color = 'var(--accent-danger)';
                }
            }).catch(() => {
                document.getElementById('sdStatus').textContent = '⚠️ Unknown';
            });
        }
        
        window.onload = function() {
            console.log('Page loaded, attempting to load macros...');
            loadMacros();
            checkSDStatus();
        };

        function showStatus(elementId, message, type) {
            const status = document.getElementById(elementId);
            status.textContent = message;
            status.className = 'status ' + type;
            status.style.display = 'block';
            setTimeout(() => { status.style.display = 'none'; }, 5000);
        }

        async function loadMacros() {
            try {
                const response = await fetch('/api/macros', {
                    credentials: 'same-origin'
                });
                if (response.ok) {
                    const text = await response.text();
                    document.getElementById('macroEditor').value = text;
                    showStatus('editorStatus', '✅ Macros loaded successfully', 'success');
                } else if (response.status === 401) {
                    showStatus('editorStatus', '⚠️ Authentication required - please reload the page', 'error');
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
                    body: content,
                    credentials: 'same-origin'
                });
                if (response.ok) {
                    showStatus('editorStatus', '✅ Macros saved successfully!', 'success');
                } else if (response.status === 401) {
                    showStatus('editorStatus', '⚠️ Authentication required - please reload the page', 'error');
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
  
  // Test endpoint (no auth) to verify server is running
  server->on("/test", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("GET /test request received");
    String response = "Server is running!\n";
    response += "SD Card Available: " + String(sdCardAvailable ? "Yes" : "No") + "\n";
    response += "Number of macros loaded: " + String(macros.size()) + "\n";
    if (macros.size() > 0) {
      response += "First macro: " + macroNames[0] + "\n";
    }
    request->send(200, "text/plain", response);
  });
  
  // Handle favicon to avoid 500 errors
  server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "");
  });
  
  // Enable authentication for all routes
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(AUTH_USER, AUTH_PASS)) {
      return request->requestAuthentication();
    }
    request->send(200, "text/html", index_html);
  });
  
  // API endpoint to get macros (decrypted)
  server->on("/api/macros", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.println("GET /api/macros request received");
    
    // Check if request is from the same origin (already authenticated main page)
    // For now, we'll rely on the fact that the user authenticated to access the main page
    // In production, you might want to implement proper session management
    
    Serial.println("Processing macro request...");
    
    // Check if SD card was initialized successfully
    if (!sdCardAvailable) {
      Serial.println("SD card not available (initialization failed)");
      // Instead of error, return empty template to allow web UI to work
      request->send(200, "text/plain", "# SD Card Error\n# Please check SD card and restart device\n");
      return;
    }
    
    // Check if encrypted file exists
    if (SD_MMC.exists("/macros.enc")) {
      Serial.println("Found encrypted macros file");
      CryptoManager& crypto = CryptoManager::getInstance();
      if (!crypto.initialize()) {
        request->send(500, "text/plain", "Failed to initialize crypto");
        return;
      }
      
      // Read and decrypt
      File encFile = SD_MMC.open("/macros.enc", FILE_READ);
      if (!encFile) {
        request->send(404, "text/plain", "macros.enc not found");
        return;
      }
      
      size_t fileSize = encFile.size();
      std::vector<uint8_t> encData(fileSize);
      encFile.read(encData.data(), fileSize);
      encFile.close();
      
      std::vector<uint8_t> decrypted;
      if (!crypto.decryptData(encData.data(), fileSize, decrypted)) {
        request->send(500, "text/plain", "Failed to decrypt macros");
        return;
      }
      
      String content((char*)decrypted.data(), decrypted.size());
      Serial.println("Sending decrypted content, length: " + String(content.length()));
      request->send(200, "text/plain", content);
    } else if (SD_MMC.exists("/macros.txt")) {
      Serial.println("Found plain text macros file");
      // Fallback to plain text (migrate on next save)
      File file = SD_MMC.open("/macros.txt", FILE_READ);
      if (!file) {
        Serial.println("Failed to open plain text file");
        request->send(404, "text/plain", "macros.txt not found");
        return;
      }
      String content = file.readString();
      file.close();
      Serial.println("Sending plain text content, length: " + String(content.length()));
      request->send(200, "text/plain", content);
    } else {
      Serial.println("No macros file found on SD card");
      // Return empty content instead of 404 to allow creating new macros
      request->send(200, "text/plain", "# No macros found\n# Create your first macro below\n");
    }
  });
  
  // API endpoint to save macros (encrypted)
  server->on("/api/macros", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      // Response will be sent from body handler
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String macroBuffer;  // Static buffer for accumulating data
      
      Serial.println("POST /api/macros request received");
      
      // First chunk - reset buffer
      if (index == 0) {
        macroBuffer = "";
        macroBuffer.reserve(total);  // Pre-allocate memory
      }
      
      // Accumulate data
      macroBuffer.concat((const char*)data, len);
      
      // Last chunk - process and save
      if (index + len == total) {
        Serial.println("Saving macros, content length: " + String(macroBuffer.length()));
        
        if (macroBuffer.length() > 0 && saveMacrosToSD(macroBuffer)) {
          // Remove old plain text file if it exists
          if (SD_MMC.exists("/macros.txt")) {
            SD_MMC.remove("/macros.txt");
            Serial.println("Removed old plain text macros file");
          }
          
          loadMacrosFromSD();
          request->send(200, "text/plain", "Saved and encrypted successfully");
          Serial.println("Macros saved and encrypted from web UI");
        } else {
          request->send(500, "text/plain", "Failed to encrypt and save macros");
          Serial.println("Failed to save macros - buffer empty or encryption failed");
        }
        
        macroBuffer = "";  // Clear buffer
      }
    }
  );
  
  // API endpoint to inject text - simplified approach
  server->on("/api/inject", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      // Response handled in body handler
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // Use request object to store state
      struct InjectState {
        String* buffer;
        bool processed;
      };
      
      InjectState* state = (InjectState*)request->_tempObject;
      
      // First chunk - initialize state
      if (index == 0) {
        Serial.println("\n=== NEW INJECTION REQUEST ===");
        Serial.println("Total size: " + String(total) + " bytes");
        
        // Check USB HID
        if (!usbHidEnabled) {
          Serial.println("USB HID not enabled");
          request->send(400, "text/plain", "USB HID not enabled");
          return;
        }
        
        // Check size
        if (total > 10240) {
          Serial.println("Text too large");
          request->send(413, "text/plain", "Text too large (max 10KB)");
          return;
        }
        
        // Create new state for this request
        state = new InjectState();
        state->buffer = new String();
        state->buffer->reserve(total);
        state->processed = false;
        request->_tempObject = state;
      }
      
      // Check if we have state
      if (!state) {
        Serial.println("ERROR: No state object");
        return;
      }
      
      // If already processed, ignore
      if (state->processed) {
        Serial.println("Already processed, ignoring");
        return;
      }
      
      // Accumulate data
      state->buffer->concat((const char*)data, len);
      Serial.println("Accumulated: " + String(state->buffer->length()) + "/" + String(total));
      
      // Check if complete
      if (index + len >= total) {
        // Mark as processed FIRST
        state->processed = true;
        
        Serial.println("=== ALL DATA RECEIVED ===");
        
        // Check for empty
        if (state->buffer->length() == 0) {
          Serial.println("Empty text");
          request->send(400, "text/plain", "No text to inject");
        } else {
          // Inject the text ONCE
          Serial.println("Injecting " + String(state->buffer->length()) + " characters...");
          
          for (int i = 0; i < state->buffer->length(); i++) {
            char c = state->buffer->charAt(i);
            
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
            
            delay(20);
            
            // Progress indicator
            if (i % 100 == 99) {
              Serial.print(".");
              yield();
            }
          }
          
          Serial.println("\n=== INJECTION COMPLETE ===");
          request->send(200, "text/plain", "Injected successfully");
        }
        
        // Clean up
        delete state->buffer;
        delete state;
        request->_tempObject = nullptr;
      }
    }
  );
  
  // Catch-all handler for debugging
  server->onNotFound([](AsyncWebServerRequest *request) {
    Serial.println("404 Not Found: " + request->url());
    request->send(404, "text/plain", "Not Found: " + request->url());
  });
  
  server->begin();
  Serial.println("Web server started on port 80");
  Serial.println("Available endpoints:");
  Serial.println("  /test - Server test (no auth)");
  Serial.println("  / - Main page (auth required)");
  Serial.println("  /api/macros - GET/POST macros");
  Serial.println("  /api/inject - POST text injection");
  
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
    // Initialize crypto system early
    CryptoManager& crypto = CryptoManager::getInstance();
    if (!crypto.initialize()) {
      Serial.println("Warning: Crypto system initialization failed");
      Serial.println("Macros will not be encrypted");
    } else {
      Serial.println("Crypto system initialized");
    }
    
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
  Serial.println("========================================");
  Serial.println("Initializing SD card...");
  Serial.println("========================================");
  
  // Configure pins for SD card
  Serial.println("Configuring SD pins:");
  Serial.printf("  CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d\n", 
                SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  
  // Set pins for 1-bit mode (most compatible)
  if(!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)){
    Serial.println("ERROR: SD_MMC.setPins() failed!");
    sdCardAvailable = false;
    return false;
  }
  
  Serial.println("SD pins configured for 1-bit mode");
  
  // Try to begin with 1-bit mode
  Serial.println("Attempting SD_MMC.begin()...");
  if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
    Serial.println("ERROR: SD_MMC.begin() failed!");
    Serial.println("Possible causes:");
    Serial.println("  - No SD card inserted");
    Serial.println("  - SD card not formatted as FAT32");
    Serial.println("  - Hardware connection issue");
    Serial.println("  - Incompatible SD card");
    sdCardAvailable = false;
    return false;
  }
  
  Serial.println("SD_MMC.begin() succeeded");
  
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    SD_MMC.end();
    sdCardAvailable = false;
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
  
  // Test file access
  File root = SD_MMC.open("/");
  if(!root){
    Serial.println("Failed to open root directory");
    SD_MMC.end();
    sdCardAvailable = false;
    return false;
  }
  
  if(!root.isDirectory()){
    Serial.println("Root is not a directory");
    root.close();
    SD_MMC.end();
    sdCardAvailable = false;
    return false;
  }
  
  // List files in root directory for debugging
  Serial.println("Files in root directory:");
  root = SD_MMC.open("/");
  File fileEntry = root.openNextFile();
  while(fileEntry){
    if(fileEntry.isDirectory()){
      Serial.print("  DIR : ");
    } else {
      Serial.print("  FILE: ");
      Serial.print(fileEntry.size());
      Serial.print(" bytes - ");
    }
    Serial.println(fileEntry.name());
    fileEntry.close();
    fileEntry = root.openNextFile();
  }
  
  root.close();
  Serial.println("SD card initialization complete");
  sdCardAvailable = true;
  return true;
}

// Helper function to save macros content (encrypted)
bool saveMacrosToSD(const String& content) {
  CryptoManager& crypto = CryptoManager::getInstance();
  if (!crypto.initialize()) {
    Serial.println("Failed to initialize crypto system");
    return false;
  }
  
  // Encrypt the content
  std::vector<uint8_t> encrypted;
  if (!crypto.encryptData((const uint8_t*)content.c_str(), content.length(), encrypted)) {
    Serial.println("Failed to encrypt macros");
    return false;
  }
  
  // Write encrypted data to file
  fs::File file = SD_MMC.open("/macros.enc", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create encrypted macros file");
    return false;
  }
  
  size_t written = file.write(encrypted.data(), encrypted.size());
  file.close();
  
  return (written == encrypted.size());
}

void loadMacrosFromSD() {
  macros.clear();
  macroNames.clear();
  macroSensitive.clear();
  
  Serial.println("Loading macros from SD...");
  
  if (!sdCardAvailable) {
    Serial.println("SD card not available, cannot load macros");
    return;
  }
  
  // Check what files exist
  bool hasEncrypted = SD_MMC.exists("/macros.enc");
  bool hasPlainText = SD_MMC.exists("/macros.txt");
  
  Serial.println("Has encrypted file: " + String(hasEncrypted));
  Serial.println("Has plain text file: " + String(hasPlainText));
  
  if (!hasEncrypted && !hasPlainText) {
    Serial.println("No macros file found");
    return;
  }
  
  String fileContent;
  
  if (hasEncrypted) {
    Serial.println("Loading encrypted macros...");
    // Decrypt the file to memory
    CryptoManager& crypto = CryptoManager::getInstance();
    if (!crypto.initialize()) {
      Serial.println("Failed to initialize crypto system");
      return;
    }
    
    // Read encrypted file
    fs::File encFile = SD_MMC.open("/macros.enc", FILE_READ);
    if (!encFile) {
      Serial.println("Failed to open encrypted file");
      return;
    }
    
    size_t fileSize = encFile.size();
    Serial.println("Encrypted file size: " + String(fileSize));
    
    std::vector<uint8_t> encData(fileSize);
    size_t bytesRead = encFile.read(encData.data(), fileSize);
    encFile.close();
    
    if (bytesRead != fileSize) {
      Serial.println("Failed to read entire encrypted file");
      return;
    }
    
    // Decrypt data
    std::vector<uint8_t> decrypted;
    if (!crypto.decryptData(encData.data(), fileSize, decrypted)) {
      Serial.println("Failed to decrypt macros file");
      return;
    }
    
    Serial.println("Decrypted size: " + String(decrypted.size()));
    
    // Convert to String
    fileContent = String((char*)decrypted.data(), decrypted.size());
  } else if (hasPlainText) {
    Serial.println("Loading plain text macros for migration...");
    // Read plain text file (for backward compatibility)
    fs::File file = SD_MMC.open("/macros.txt", FILE_READ);
    if (!file) {
      Serial.println("Failed to open plain text file");
      return;
    }
    
    fileContent = file.readString();
    file.close();
    
    Serial.println("Plain text content length: " + String(fileContent.length()));
    
    // Migrate to encrypted format
    Serial.println("Migrating to encrypted format...");
    if (saveMacrosToSD(fileContent)) {
      Serial.println("Migration successful, removing plain text file...");
      // Delete the plain text file after successful migration
      if (SD_MMC.remove("/macros.txt")) {
        Serial.println("Plain text file removed successfully");
      } else {
        Serial.println("Failed to remove plain text file");
      }
    } else {
      Serial.println("Migration failed, keeping plain text file");
    }
  }
  
  // Parse the content line by line
  Serial.println("Parsing file content, total length: " + String(fileContent.length()));
  
  int startIdx = 0;
  int endIdx = fileContent.indexOf('\n');
  int lineCount = 0;
  
  while (endIdx != -1 || startIdx < fileContent.length()) {
    String line;
    if (endIdx != -1) {
      line = fileContent.substring(startIdx, endIdx);
      startIdx = endIdx + 1;
      endIdx = fileContent.indexOf('\n', startIdx);
    } else {
      line = fileContent.substring(startIdx);
      startIdx = fileContent.length();
    }
    
    line.trim();
    lineCount++;
    
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
  
  int sensitiveCount = 0;
  for (bool s : macroSensitive) {
    if (s) sensitiveCount++;
  }
  
  Serial.println("Processed " + String(lineCount) + " lines");
  Serial.println("Loaded " + String(macros.size()) + " macros (" + 
                 String(sensitiveCount) + " sensitive)");
  
  // Debug: print first macro if available
  if (macros.size() > 0) {
    Serial.println("First macro name: " + macroNames[0]);
    int previewLen = macros[0].length() > 20 ? 20 : macros[0].length();
    Serial.println("First macro preview: " + macros[0].substring(0, previewLen) + "...");
  }
}

void createExampleMacros() {
  String content = "";
  content += "# USBone Macro File\n";
  content += "# Format: NAME:CONTENT\n";
  content += "# For sensitive macros: SENSITIVE:NAME:CONTENT\n";
  content += "#\n";
  content += "# Special sequences:\n";
  content += "#   \\n = Enter key\n";
  content += "#   \\t = Tab key\n";
  content += "#\n";
  content += "\n";
  content += "# Regular macros\n";
  content += "Email:user@example.com\n";
  content += "Username:john_doe\n";
  content += "\n";
  content += "# Sensitive macros\n";
  content += "SENSITIVE:Password:MySecretPassword123!\n";
  content += "SENSITIVE:API_Key:sk-1234567890abcdef\n";
  content += "SENSITIVE:BankLogin:admin\\tSecurePass456\\n\n";
  
  // Save encrypted
  if (saveMacrosToSD(content)) {
    Serial.println("Example macros created and encrypted");
  } else {
    Serial.println("Failed to create example macros");
  }
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
