#include <Arduino.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>

// Hardware Definitions
const int ledChannel = 0;  // LED PWM channel for ClockWaveXmitter antenna
const int ledPin = 0;      // GPIO pin for PWM output (ANT G-5)

// Global Variables
char P0, P1, P2, P3, P4, P5;
const char M = P0 = P1 = P2 = P3 = P4 = P5 = -1;  // Marker value
char PA1, PA2, SU1, LS1, LS2;
char sg[60];  // Time data array (60 seconds)

// WiFi & Web Server Configuration
const char* ap_ssid = "ClockWaveXmitter-Config";       // Access Point SSID for configuration
const char* ap_password = "12345678";     // Access Point password (min 8 chars required by ESP32)
WebServer server(80);                     // Web server on port 80
DNSServer dnsServer;                      // DNS server for captive portal
Preferences prefs;                        // Non-volatile storage for configuration

// Configuration Parameters
String wifi_ssid = "";                    // Stored WiFi SSID
String wifi_password = "";                // Stored WiFi password
int timezone_offset = 8;                  // Default timezone offset (hours) - Japan Standard Time
bool config_mode = false;                 // Flag for AP configuration mode
String current_lang = "en";               // Default language: en / cn

// NTP Time Synchronization
unsigned long last_ntp_sync = 0;          // Timestamp of last NTP sync (ms)
const unsigned long ntp_sync_interval = 24 * 60 * 60 * 1000;  // 24-hour sync interval (ms)
bool ntp_time_valid = false;              // Flag for valid NTP time

// Captive Portal Configuration
const byte DNS_PORT = 53;                 // DNS port for captive portal
IPAddress apIP(192, 168, 4, 1);           // Fixed AP IP address

// Language Strings - Bilingual Support
struct LanguageStrings {
  const char* title;
  const char* header;
  const char* ssid_label;
  const char* password_label;
  const char* timezone_label;
  const char* save_btn;
  const char* sync_btn;
  const char* restart_btn;
  const char* success_msg;
  const char* error_msg;
  const char* ntp_success;
  const char* ntp_error;
  const char* restarting;
  const char* lang_en;
  const char* lang_cn;
};

LanguageStrings lang_en = {
  "ClockWaveXmitter Configuration",
  "ClockWaveXmitter WiFi Configuration",
  "WiFi SSID:",
  "WiFi Password:",
  "Timezone Offset (hours):",
  "Save Configuration",
  "Sync NTP Time Now",
  "Restart Device",
  "Configuration saved successfully! Restart to apply changes.",
  "Cannot sync NTP - WiFi not connected!",
  "NTP time synchronized successfully!",
  "NTP sync failed!",
  "Restarting device...",
  "English",
  "中文"
};

LanguageStrings lang_cn = {
  "ClockWaveXmitter 配置页面",
  "ClockWaveXmitter WiFi 配置",
  "WiFi名称:",
  "WiFi密码:",
  "时区偏移 (小时):",
  "保存配置",
  "立即同步NTP时间",
  "重启设备",
  "配置保存成功！重启设备以应用更改。",
  "无法同步NTP - WiFi未连接！",
  "NTP时间同步成功！",
  "NTP同步失败！",
  "正在重启设备...",
  "English",
  "中文"
};

// Beautiful HTML Template with Bilingual Support
const char* html_template = R"HTML(
<!DOCTYPE html>
<html lang="%LANG%">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%TITLE%</title>
    <style>
        :root {
            --primary: #2563eb;
            --primary-dark: #1d4ed8;
            --success: #10b981;
            --success-dark: #059669;
            --warning: #f59e0b;
            --warning-dark: #d97706;
            --danger: #ef4444;
            --danger-dark: #dc2626;
            --info: #3b82f6;
            --gray-100: #f3f4f6;
            --gray-200: #e5e7eb;
            --gray-300: #d1d5db;
            --gray-700: #374151;
            --gray-800: #1f2937;
            --shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1), 0 2px 4px -1px rgba(0, 0, 0, 0.06);
            --shadow-lg: 0 10px 15px -3px rgba(0, 0, 0, 0.1), 0 4px 6px -2px rgba(0, 0, 0, 0.05);
            --radius: 0.5rem;
            --radius-lg: 0.75rem;
            --transition: all 0.2s ease-in-out;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
            min-height: 100vh;
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }
        
        .language-switcher {
            position: absolute;
            top: 20px;
            right: 20px;
            display: flex;
            gap: 10px;
            z-index: 100;
        }
        
        .lang-btn {
            padding: 6px 12px;
            border: 1px solid var(--gray-300);
            border-radius: var(--radius);
            background: white;
            cursor: pointer;
            transition: var(--transition);
            font-size: 0.875rem;
        }
        
        .lang-btn.active {
            background: var(--primary);
            color: white;
            border-color: var(--primary);
        }
        
        .lang-btn:hover:not(.active) {
            background: var(--gray-100);
        }
        
        .container {
            background: white;
            border-radius: var(--radius-lg);
            box-shadow: var(--shadow-lg);
            padding: 2rem;
            width: 100%;
            max-width: 450px;
            animation: fadeIn 0.3s ease-in-out;
        }
        
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        
        h2 {
            color: var(--gray-800);
            margin-bottom: 1.5rem;
            text-align: center;
            font-size: 1.5rem;
            font-weight: 600;
        }
        
        .status {
            padding: 1rem;
            border-radius: var(--radius);
            margin-bottom: 1.5rem;
            text-align: center;
            animation: slideIn 0.3s ease-in-out;
        }
        
        @keyframes slideIn {
            from { opacity: 0; transform: translateX(-10px); }
            to { opacity: 1; transform: translateX(0); }
        }
        
        .success {
            background-color: #dcfce7;
            color: var(--success-dark);
            border: 1px solid #bbf7d0;
        }
        
        .error {
            background-color: #fee2e2;
            color: var(--danger-dark);
            border: 1px solid #fecaca;
        }
        
        .info {
            background-color: #dbeafe;
            color: var(--info);
            border: 1px solid #bfdbfe;
        }
        
        form {
            margin-bottom: 1rem;
        }
        
        .form-group {
            margin-bottom: 1rem;
        }
        
        label {
            display: block;
            margin-bottom: 0.5rem;
            color: var(--gray-700);
            font-weight: 500;
            font-size: 0.9375rem;
        }
        
        input {
            width: 100%;
            padding: 0.75rem;
            border: 1px solid var(--gray-300);
            border-radius: var(--radius);
            font-size: 1rem;
            transition: var(--transition);
            background-color: white;
        }
        
        input:focus {
            outline: none;
            border-color: var(--primary);
            box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.1);
        }
        
        button {
            width: 100%;
            padding: 0.75rem;
            border: none;
            border-radius: var(--radius);
            font-size: 1rem;
            font-weight: 500;
            cursor: pointer;
            transition: var(--transition);
            color: white;
            margin-top: 0.5rem;
        }
        
        .btn-primary {
            background-color: var(--primary);
        }
        
        .btn-primary:hover {
            background-color: var(--primary-dark);
        }
        
        .btn-info {
            background-color: var(--info);
        }
        
        .btn-info:hover {
            background-color: var(--primary-dark);
        }
        
        .btn-warning {
            background-color: var(--warning);
        }
        
        .btn-warning:hover {
            background-color: var(--warning-dark);
        }
        
        .divider {
            height: 1px;
            background-color: var(--gray-200);
            margin: 1rem 0;
        }
        
        .footer {
            text-align: center;
            margin-top: 1.5rem;
            color: var(--gray-700);
            font-size: 0.875rem;
            opacity: 0.8;
        }
        
        @media (max-width: 480px) {
            .container {
                padding: 1.5rem;
            }
            
            body {
                padding: 10px;
            }
            
            .language-switcher {
                position: relative;
                top: 0;
                right: 0;
                margin-bottom: 1rem;
                justify-content: center;
            }
        }
    </style>
</head>
<body>
    <div class="language-switcher">
        <button class="lang-btn %EN_ACTIVE%" onclick="switchLang('en')">%LANG_EN%</button>
        <button class="lang-btn %CN_ACTIVE%" onclick="switchLang('cn')">%LANG_CN%</button>
    </div>
    
    <div class="container">
        <h2>%HEADER%</h2>
        
        %STATUS%
        
        <form method="POST" action="/save">
            <div class="form-group">
                <label for="ssid">%SSID_LABEL%</label>
                <input type="text" id="ssid" name="ssid" value="%SSID%" required>
            </div>
            
            <div class="form-group">
                <label for="password">%PASSWORD_LABEL%</label>
                <input type="password" id="password" name="password" value="%PASSWORD%">
            </div>
            
            <div class="form-group">
                <label for="timezone">%TIMEZONE_LABEL%</label>
                <input type="number" id="timezone" name="timezone" value="%TIMEZONE%" step="0.5" required>
            </div>
            
            <button type="submit" class="btn-primary">%SAVE_BTN%</button>
        </form>
        
        <div class="divider"></div>
        
        <form method="POST" action="/sync_ntp">
            <button type="submit" class="btn-info">%SYNC_BTN%</button>
        </form>
        
        <form method="POST" action="/restart">
            <button type="submit" class="btn-warning">%RESTART_BTN%</button>
        </form>
        
        <div class="footer">
            &copy; 2025 ClockWaveXmitter Configuration Portal
        </div>
    </div>

    <script>
        // Language switch functionality
        function switchLang(lang) {
            // Add language parameter to current URL
            const url = new URL(window.location.href);
            url.searchParams.set('lang', lang);
            window.location.href = url.toString();
        }
        
        // Auto-hide status messages after 5 seconds
        document.addEventListener('DOMContentLoaded', function() {
            const status = document.querySelector('.status');
            if (status) {
                setTimeout(() => {
                    status.style.opacity = '0';
                    status.style.transition = 'opacity 0.5s ease';
                    setTimeout(() => status.remove(), 500);
                }, 5000);
            }
        });
    </script>
</body>
</html>
)HTML";

/**
 * @brief Get language strings based on current language
 * @return LanguageStrings struct with appropriate language
 */
LanguageStrings getLangStrings() {
  return (current_lang == "cn") ? lang_cn : lang_en;
}

/**
 * @brief Generate HTML content with language and dynamic data
 * @param status Optional status message to display
 * @return Complete HTML string
 */
String generateHTML(String status = "") {
  LanguageStrings lang = getLangStrings();
  String html = String(html_template);
  
  // Replace language placeholders
  html.replace("%LANG%", current_lang);
  html.replace("%TITLE%", lang.title);
  html.replace("%HEADER%", lang.header);
  html.replace("%SSID_LABEL%", lang.ssid_label);
  html.replace("%PASSWORD_LABEL%", lang.password_label);
  html.replace("%TIMEZONE_LABEL%", lang.timezone_label);
  html.replace("%SAVE_BTN%", lang.save_btn);
  html.replace("%SYNC_BTN%", lang.sync_btn);
  html.replace("%RESTART_BTN%", lang.restart_btn);
  html.replace("%LANG_EN%", lang.lang_en);
  html.replace("%LANG_CN%", lang.lang_cn);
  
  // Set active language button
  html.replace("%EN_ACTIVE%", (current_lang == "en") ? "active" : "");
  html.replace("%CN_ACTIVE%", (current_lang == "cn") ? "active" : "");
  
  // Replace configuration values
  html.replace("%SSID%", wifi_ssid);
  html.replace("%PASSWORD%", wifi_password);
  html.replace("%TIMEZONE%", String(timezone_offset));
  
  // Replace status message
  html.replace("%STATUS%", status);
  
  return html;
}

/**
 * @brief Initialize system components and start WiFi/AP mode
 */
void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100);
  
  // Initialize LED PWM
  ledcSetup(ledChannel, 40000, 8);  // Configure PWM channel (40kHz, 8-bit resolution)
  ledcAttachPin(ledPin, ledChannel); // Attach PWM channel to GPIO pin

  // Initialize non-volatile storage
  prefs.begin("ClockWaveXmitter-config", false);  // Open preferences namespace (read-write)
  
  // Load saved configuration
  wifi_ssid = prefs.getString("wifi_ssid", "");
  wifi_password = prefs.getString("wifi_password", "");
  timezone_offset = prefs.getInt("timezone_offset", 8);
  
  Serial.println("\n=== ClockWaveXmitter SYSTEM STARTUP ===");
  Serial.print("Saved SSID: ");
  Serial.println(wifi_ssid);
  Serial.print("Saved Timezone Offset: ");
  Serial.println(timezone_offset);
  
  // Initialize fixed markers in time data array
  set_fix();
  
  // Attempt WiFi connection
  bool wifi_connected = connectToWiFi();
  
  // Start AP mode if WiFi connection fails
  if (!wifi_connected) {
    startAPMode();          // Start AP with captive portal
    setupCaptivePortal();   // Configure DNS & redirects
    config_mode = true;
  } else {
    // Sync NTP time immediately after successful WiFi connection
    syncNTPTime();
    Serial.println("Connected to WiFi network!");
  }
  
  // Initialize web server routes (works in both AP and STA modes)
  setupWebServer();
}

/**
 * @brief Main loop - handle web requests or time processing
 */
void loop() {
  // Handle captive portal DNS requests in AP mode
  if (config_mode) {
    dnsServer.processNextRequest();  // Process DNS queries
    server.handleClient();           // Handle web requests
    delay(10);
    return;
  }
  
  // Check for 24-hour NTP resync requirement
  if (WiFi.status() == WL_CONNECTED && (millis() - last_ntp_sync >= ntp_sync_interval || !ntp_time_valid)) {
    Serial.println("24 hours elapsed or invalid time - resyncing NTP time...");
    syncNTPTime();
  }
  
  // Normal operation mode - time processing
  struct tm timeInfo;
  char s[20];
  char t[50];
  int se, mi, sh;

  // Get current local time
  if (!getLocalTime(&timeInfo)) {
    Serial.println("Failed to retrieve time! Attempting NTP resync...");
    syncNTPTime();
    delay(1000);
    return;
  }
  
  ntp_time_valid = true;

  // Format time for serial output (YYYY/MM/DD HH:MM:SS)
  sprintf(s, "%04d/%02d/%02d %02d:%02d:%02d",
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
          timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

  // Print formatted time to serial monitor
  Serial.println(s);

  // Format date information (Year:YY, DayOfYear:DDD, Weekday:D)
  sprintf(t, "Y:%02d  D:%03d WD:%01d",
          timeInfo.tm_year + 1900 - 2000, timeInfo.tm_yday + 1, timeInfo.tm_wday);

  Serial.println(t);

  // Leap second correction
  se = timeInfo.tm_sec;
  sh = 0;
  if (timeInfo.tm_sec == 60) {  // Leap second +1
    sg[53] = LS1 = 1;
    sg[54] = LS2 = 0;
    se = timeInfo.tm_sec - 1;
    sh = 1;
  }
  
  if (timeInfo.tm_sec == 61) {  // Leap second +2
    sg[53] = LS1 = 1;
    sg[54] = LS2 = 1;
    se = timeInfo.tm_sec - 2;
    sh = 2;
  }

  // Populate time data array with current time values
  set_min(timeInfo.tm_min);
  set_hour(timeInfo.tm_hour);
  set_day(timeInfo.tm_yday + 1);              // Day of year (Jan 1 = 1)
  set_wday(timeInfo.tm_wday);                 // Day of week
  set_year(timeInfo.tm_year + 1900 - 2000);   // Last 2 digits of year

  // Process each second in the current minute
  for (int i = se; i < 60 + sh; i++) {
    sprintf(t, "%02d ", sg[i]);
    Serial.print(t);
    
    // Execute LED pattern based on time data value
    switch (sg[i]) {
      case -1:
        mark();
        break;
      case 255:
        mark();
        break;
      case 0:
        zero();
        break;
      case 1:
        one();
        break;
      default:    // Error handling for invalid values
        zero();
        sprintf(t, "%02d  %02d", i, sg[i]);
        Serial.print("***BUG data ***  ");
        Serial.println(t);
        break;
    }
  }        
  delay(5);
  
  // Check WiFi connection status - switch to AP mode if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost! Switching to AP configuration mode...");
    startAPMode();
    setupCaptivePortal();
    config_mode = true;
  }
}

/**
 * @brief Connect to WiFi network using stored credentials
 * @return true if connected successfully, false otherwise
 */
bool connectToWiFi() {
  if (wifi_ssid.isEmpty()) {
    Serial.println("WiFi SSID not configured");
    return false;
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifi_ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  // Wait for connection (10 second timeout)
  unsigned long start_time = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_time < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connection successful!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed");
    return false;
  }
}

/**
 * @brief Start Access Point mode with fixed IP for captive portal
 */
void startAPMode() {
  Serial.println("Starting AP configuration mode with captive portal...");
  
  // Configure AP with fixed IP address (required for captive portal)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));  // IP, Gateway, Subnet
  WiFi.softAP(ap_ssid, ap_password);
  
  IPAddress currentAPIP = WiFi.softAPIP();
  Serial.print("AP IP Address: ");
  Serial.println(currentAPIP);
  Serial.println("Connect to WiFi SSID: " + String(ap_ssid));
  Serial.println("WiFi Password: " + String(ap_password));
  Serial.println("Captive portal active - all requests redirect to http://" + currentAPIP.toString());
}

/**
 * @brief Setup DNS server and captive portal redirect rules
 */
void setupCaptivePortal() {
  // Configure DNS server to redirect all domains to AP IP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);  // Redirect all DNS queries to AP IP
  
  Serial.println("Captive portal DNS server started on port " + String(DNS_PORT));
  
  // Add catch-all route for captive portal detection (iOS/Android)
  server.on("/generate_204", HTTP_GET, handleCaptivePortalRedirect);  // Android captive portal detect
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortalRedirect);  // iOS captive portal detect
  server.on("/canonical.html", HTTP_GET, handleCaptivePortalRedirect);    // Windows captive portal detect
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortalRedirect);   // Windows detect
  server.on("/favicon.ico", HTTP_GET, handleCaptivePortalRedirect);       // Favicon requests
}

/**
 * @brief Handle captive portal redirect for all non-config requests
 */
void handleCaptivePortalRedirect() {
  // Get language parameter if present
  String lang = server.arg("lang");
  if (lang == "en" || lang == "cn") {
    current_lang = lang;
  }
  
  // Redirect all requests to the main configuration page
  server.sendHeader("Location", "http://" + apIP.toString() + "/?lang=" + current_lang, true);
  server.send(302, "text/plain", "Redirecting to configuration page...");
  server.client().stop();
}

/**
 * @brief Configure web server routes and handlers
 */
void setupWebServer() {
  // Root route - configuration page
  server.on("/", HTTP_GET, []() {
    // Get language parameter from URL
    String lang = server.arg("lang");
    if (lang == "en" || lang == "cn") {
      current_lang = lang;
    }
    
    // Generate HTML with current language and empty status
    String html = generateHTML("");
    server.send(200, "text/html; charset=UTF-8", html);
  });
  
  // Save configuration route
  server.on("/save", HTTP_POST, []() {
    // Get language parameter
    String lang = server.arg("lang");
    if (lang == "en" || lang == "cn") {
      current_lang = lang;
    }
    
    // Get form data
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");
    float new_timezone = server.arg("timezone").toInt();
    
    // Save to non-volatile storage
    prefs.putString("wifi_ssid", new_ssid);
    prefs.putString("wifi_password", new_password);
    prefs.putInt("timezone_offset", new_timezone);
    
    // Update global variables
    wifi_ssid = new_ssid;
    wifi_password = new_password;
    timezone_offset = new_timezone;
    
    // Get language strings for success message
    LanguageStrings lang_str = getLangStrings();
    String status = "<div class='status success'>" + String(lang_str.success_msg) + "</div>";
    
    // Prepare response with success message
    String html = generateHTML(status);
    server.send(200, "text/html; charset=UTF-8", html);
    
    Serial.println("Configuration saved:");
    Serial.print("New SSID: ");
    Serial.println(new_ssid);
    Serial.print("New Timezone Offset: ");
    Serial.println(new_timezone);
  });
  
  // Manual NTP sync route
  server.on("/sync_ntp", HTTP_POST, []() {
    // Get language parameter
    String lang = server.arg("lang");
    if (lang == "en" || lang == "cn") {
      current_lang = lang;
    }
    
    LanguageStrings lang_str = getLangStrings();
    String status = "";
    
    if (WiFi.status() == WL_CONNECTED) {
      bool sync_ok = syncNTPTime();
      if (sync_ok) {
        status = "<div class='status success'>" + String(lang_str.ntp_success) + "</div>";
      } else {
        status = "<div class='status error'>" + String(lang_str.ntp_error) + "</div>";
      }
    } else {
      status = "<div class='status error'>" + String(lang_str.error_msg) + "</div>";
    }
    
    // Prepare response page
    String html = generateHTML(status);
    server.send(200, "text/html; charset=UTF-8", html);
  });
  
  // Restart device route
  server.on("/restart", HTTP_POST, []() {
    // Get language parameter
    String lang = server.arg("lang");
    if (lang == "en" || lang == "cn") {
      current_lang = lang;
    }
    
    LanguageStrings lang_str = getLangStrings();
    
    // Send restart message
    String html = "<!DOCTYPE html><html lang='" + current_lang + "'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>" + 
                  lang_str.title + "</title><style>body{display:flex;justify-content:center;align-items:center;min-height:100vh;background:#f5f7fa;font-family:Arial,sans-serif;}" +
                  ".container{background:white;padding:2rem;border-radius:0.75rem;box-shadow:0 10px 15px -3px rgba(0,0,0,0.1);text-align:center;}</style></head>" +
                  "<body><div class='container'><h2>" + lang_str.restarting + "</h2></div></body></html>";
    
    server.send(200, "text/html; charset=UTF-8", html);
    
    // Ensure response is sent before restart
    server.handleClient();
    delay(1000);
    ESP.restart();
  });
  
  // Catch-all route for any other requests (redirect to root)
  server.onNotFound(handleCaptivePortalRedirect);
  
  // Start web server
  server.begin();
  Serial.println("Web server started with captive portal and bilingual support");
}

/**
 * @brief Synchronize time with NTP servers
 * @return true if sync successful, false otherwise
 */
bool syncNTPTime() {
  Serial.println("Synchronizing with NTP servers...");
  
  // Configure NTP time servers with timezone offset
  configTime(timezone_offset * 3600L, 0, 
             "ntp.nict.jp", 
             "time.google.com", 
             "ntp.jst.mfeed.ad.jp");
  
  // Wait for time to sync (5 second timeout)
  struct tm timeInfo;
  unsigned long start_time = millis();
  while (!getLocalTime(&timeInfo) && millis() - start_time < 5000) {
    delay(500);
    Serial.print(".");
  }
  
  if (getLocalTime(&timeInfo)) {
    last_ntp_sync = millis();
    ntp_time_valid = true;
    
    char sync_time[20];
    sprintf(sync_time, "%04d/%02d/%02d %02d:%02d:%02d",
            timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
            timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    
    Serial.println("\nNTP sync successful! Current time: " + String(sync_time));
    return true;
  } else {
    Serial.println("\nNTP sync failed!");
    ntp_time_valid = false;
    return false;
  }
}

/**
 * @brief Convert decimal number to BCD format
 * @param decimal Decimal number to convert
 * @return BCD representation
 */
int dec2BCD(int decimal) {
  int bcd = 0;
  int multiplier = 1;

  while (decimal > 0) {
    int digit = decimal % 10;
    bcd += digit * multiplier;
    multiplier *= 16;  // Shift for 4-bit BCD digits
    decimal /= 10;
  }

  return bcd;
}

/**
 * @brief Set year value in time data array (last 2 digits of year)
 * @param n Year value (0-99)
 */
void set_year(int n) {
  int m = dec2BCD(n);

  sg[48] = m % 2; m = m >> 1;
  sg[47] = m % 2; m = m >> 1;
  sg[46] = m % 2; m = m >> 1;
  sg[45] = m % 2; m = m >> 1;

  sg[44] = m % 2; m = m >> 1;
  sg[43] = m % 2; m = m >> 1;
  sg[42] = m % 2; m = m >> 1;
  sg[41] = m % 2;
}

/**
 * @brief Set day of year value in time data array
 * @param n Day of year (1-366)
 */
void set_day(int n) {
  int m = dec2BCD(n);

  sg[33] = m % 2; m = m >> 1;
  sg[32] = m % 2; m = m >> 1;
  sg[31] = m % 2; m = m >> 1;
  sg[30] = m % 2; m = m >> 1;

  sg[28] = m % 2; m = m >> 1;
  sg[27] = m % 2; m = m >> 1;
  sg[26] = m % 2; m = m >> 1;
  sg[25] = m % 2; m = m >> 1;

  sg[23] = m % 2; m = m >> 1;
  sg[22] = m % 2;
}

/**
 * @brief Set day of week value in time data array
 * @param m Day of week (0-7)
 */
void set_wday(int m) {
  sg[52] = m % 2; m = m >> 1;
  sg[51] = m % 2; m = m >> 1;
  sg[50] = m % 2;
}

/**
 * @brief Set hour value in time data array
 * @param n Hour value (0-23)
 */
void set_hour(int n) {
  int m = dec2BCD(n);

  sg[18] = m % 2; m = m >> 1;
  sg[17] = m % 2; m = m >> 1;
  sg[16] = m % 2; m = m >> 1;
  sg[15] = m % 2; m = m >> 1;
  
  sg[13] = m % 2; m = m >> 1;
  sg[12] = m % 2;
  
  // Calculate parity bit PA1
  char PA1 = sg[18] ^ sg[17] ^ sg[16] ^ sg[15] ^ sg[13] ^ sg[12];
  sg[36] = PA1;
}

/**
 * @brief Set minute value in time data array
 * @param n Minute value (0-59)
 */
void set_min(int n) {
  int m = dec2BCD(n);

  sg[8] = m % 2; m = m >> 1;
  sg[7] = m % 2; m = m >> 1;
  sg[6] = m % 2; m = m >> 1;
  sg[5] = m % 2; m = m >> 1;

  sg[3] = m % 2; m = m >> 1;
  sg[2] = m % 2; m = m >> 1;
  sg[1] = m % 2;

  // Calculate parity bit PA2
  char PA2 = sg[8] ^ sg[7] ^ sg[6] ^ sg[5] ^ sg[3] ^ sg[2] ^ sg[1];
  sg[37] = PA2;
}

/**
 * @brief Set fixed marker positions in time data array
 */
void set_fix() {
  // Set marker positions to M (-1)
  sg[0] = sg[9] = sg[19] = sg[29] = sg[39] = sg[49] = sg[59] = M;
  
  // Set fixed zero positions
  sg[4] = sg[10] = sg[11] = sg[14] = sg[20] = sg[21] = sg[24] = sg[34] = sg[35] = sg[55] = sg[56] = sg[57] = sg[58] = 0;
  sg[38] = sg[40] = 0;
  sg[53] = sg[54] = 0;
}

/**
 * @brief LED pattern for marker (0.2s on, 0.8s off)
 */
void mark() {   
  ledcWrite(ledChannel, 127);  // 50% duty cycle
  delay(200);
  ledcWrite(ledChannel, 0);    // 0% duty cycle
  delay(800);
}

/**
 * @brief LED pattern for zero (0.8s on, 0.2s off)
 */
void zero() {  
  ledcWrite(ledChannel, 127);  // 50% duty cycle
  delay(800);
  ledcWrite(ledChannel, 0);    // 0% duty cycle
  delay(200);
}

/**
 * @brief LED pattern for one (0.5s on, 0.5s off)
 */
void one() {    
  ledcWrite(ledChannel, 127);  // 50% duty cycle
  delay(500);
  ledcWrite(ledChannel, 0);    // 0% duty cycle
  delay(500);
}