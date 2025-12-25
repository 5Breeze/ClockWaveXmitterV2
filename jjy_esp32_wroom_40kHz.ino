#include <Arduino.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <time.h>
#include <esp_timer.h>
#include <SPIFFS.h>  // 引入SPIFFS文件系统库

#define LEDR 5
#define LEDG 6
#define LEDB 7

// 函数前置声明（解决编译错误）
void JJY_encode(struct tm *timeInfo);
void WWVB_encode(struct tm *timeInfo);
void BPC_encode(struct tm *timeInfo);

// 定时器句柄
esp_timer_handle_t tx_timer;

// 全局变量（volatile仅用于中断共享）
volatile uint16_t ms_in_sec = 0;
volatile int last_sec = -1;
volatile int duty_now = 0;

// ==========================================
// 硬件定义
// ==========================================
const int ledChannel = 0;          // LEDC通道0
const int ledPin = 0;              // GPIO2（板载LED，也可换GPIO18/19）
const uint32_t timer_period_us = 1000; // 定时器周期1ms（微秒）

// ==========================================
// 全局变量与配置
// ==========================================
enum Protocol {
  PROTO_JJY40 = 0,
  PROTO_JJY60 = 1,
  PROTO_WWVB  = 2,
  PROTO_BPC   = 3
};

int8_t sg[60]; // 信号数据数组 (最大60秒)

// 配置参数
String wifi_ssid = "";
String wifi_password = "";
float timezone_offset = 8.0f;
int selected_protocol = PROTO_JJY40;
bool config_mode = false;
String current_lang = "en";

// Web Server & WiFi
String ap_ssid_base = "ClockWave-Config";  // 基础AP名称
String ap_ssid;                            // 动态生成带MAC的AP名称
const char* ap_password = "12345678";
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

// 密钥验证相关
String device_mac;         // 设备MAC地址（原始）
String auth_key;           // 生成的认证密钥（MAC反序+异或混淆）
bool auth_verified = false;// 密钥验证状态
String input_key = "";     // 用户输入的密钥
const uint8_t XOR_KEY[] = {0x5A, 0x6B, 0x7C, 0x8D, 0x9E, 0xAF}; // 固定异或混淆密钥（6字节，与MAC长度匹配）

// 时间与同步控制变量
unsigned long last_ntp_sync = 0;
const unsigned long ntp_sync_interval = 3600000; // 1小时同步一次
bool ntp_time_valid = false;

// 信号发射控制（移除重复逻辑，仅保留一套）
int current_sec_index = -1;
unsigned long sec_start_millis = 0;
int current_duty = 0;

// ==========================================
// 语言字符串结构
// ==========================================
struct LanguageStrings {
  const char* title;
  const char* header;
  const char* ssid_label;
  const char* password_label;
  const char* timezone_label;
  const char* proto_label;
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
  const char* ap_mode_ntp_error;
  const char* ap_mode_hint;
  // 新增密钥验证相关
  const char* auth_title;
  const char* device_mac_label;
  const char* auth_key_label;
  const char* auth_key_placeholder;
  const char* verify_btn;
  const char* auth_success;
  const char* auth_failed;
};

LanguageStrings lang_en = {
  "ClockWave Configuration", "ClockWave WiFi & Protocol", "WiFi SSID:", "WiFi Password:", 
  "Timezone Offset (hours):", "Time Signal Protocol:", 
  "Save Configuration", "Sync NTP Time Now", "Restart Device",
  "Configuration saved! Restarting...", "Cannot sync NTP - WiFi not connected!",
  "NTP synchronized successfully!", "NTP sync failed!", "Restarting device...",
  "English", "中文",
  "Cannot sync NTP in AP mode!", "Device in AP mode - Configure WiFi first.",
  // 新增密钥验证
  "Device Authentication",
  "Device MAC Address",
  "Authentication Key",
  "oshwhub by 五月景风",
  "Verify Key",
  "Authentication successful!",
  "Authentication failed! Wrong key."
};

LanguageStrings lang_cn = {
  "ClockWave 配置页面", "ClockWave 参数配置", "WiFi名称:", "WiFi密码:", 
  "时区偏移 (小时):", "授时协议选择:",
  "保存配置", "立即同步NTP时间", "重启设备",
  "配置保存成功！设备将重启...", "无法同步NTP - WiFi未连接！",
  "NTP时间同步成功！", "NTP同步失败！", "正在重启设备...",
  "English", "中文",
  "AP模式下无法同步NTP！", "设备处于AP模式 - 请先配置WiFi。",
  // 新增密钥验证
  "设备认证",
  "设备MAC地址",
  "认证密钥",
  "oshwhub联系五月景风",
  "验证密钥",
  "认证成功！",
  "认证失败！密钥错误。"
};

// ==========================================
// HTML 模板
// ==========================================
const char* html_template = R"HTML(
<!DOCTYPE html>
<html lang="%LANG%">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>%TITLE%</title>
    <style>
        :root { --primary: #2563eb; --gray-100: #f3f4f6; --radius: 0.5rem; }
        body { background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%); min-height: 100vh; padding: 20px; display: flex; flex-direction: column; align-items: center; justify-content: center; font-family: sans-serif; margin: 0; }
        .container { background: white; border-radius: 0.75rem; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); padding: 2rem; width: 100%; max-width: 450px; }
        h2 { text-align: center; color: #1f2937; margin-bottom: 1.5rem; }
        .form-group { margin-bottom: 1rem; }
        label { display: block; margin-bottom: 0.5rem; color: #374151; font-weight: 500; }
        input, select { width: 100%; padding: 0.75rem; border: 1px solid #d1d5db; border-radius: var(--radius); font-size: 1rem; box-sizing: border-box; }
        button { width: 100%; padding: 0.75rem; border: none; border-radius: var(--radius); font-size: 1rem; cursor: pointer; margin-top: 0.5rem; color: white; background-color: var(--primary); }
        .status { padding: 1rem; border-radius: var(--radius); margin-bottom: 1.5rem; text-align: center; }
        .success { background-color: #dcfce7; color: #059669; }
        .error { background-color: #fee2e2; color: #dc2626; }
        .info { background-color: #dbeafe; color: #2563eb; }
        .auth-section { border: 1px solid #e5e7eb; padding: 1rem; border-radius: 0.75rem; margin-bottom: 1.5rem; }
        .lang-switch { position: absolute; top: 20px; right: 20px; }
        .lang-btn { background: white; color: #333; padding: 5px 10px; border: 1px solid #ccc; width: auto; }
        .lang-btn.active { background: var(--primary); color: white; }
        .disabled-section { opacity: 0.5; pointer-events: none; }
    </style>
</head>
<body>
    <div class="lang-switch">
        <button class="lang-btn %EN_ACTIVE%" onclick="location.href='/?lang=en'">EN</button>
        <button class="lang-btn %CN_ACTIVE%" onclick="location.href='/?lang=cn'">CN</button>
    </div>
    <div class="container">
        <h2>%HEADER%</h2>
        %STATUS%
        %AP_MODE_HINT%
        
        <!-- 密钥验证区域 -->
        <div class="auth-section">
            <h3 style="text-align:center; margin-top:0;">%AUTH_TITLE%</h3>
            <form method="POST" action="/verify_key?lang=%LANG%">
                <div class="form-group">
                    <label>%DEVICE_MAC_LABEL%:</label>
                    <input type="text" value="%DEVICE_MAC%" readonly style="background:#f9fafb;">
                </div>
                <div class="form-group">
                    <label>%AUTH_KEY_LABEL%:</label>
                    <input type="password" name="auth_key" placeholder="%AUTH_KEY_PLACEHOLDER%" required>
                </div>
                <button type="submit">%VERIFY_BTN%</button>
            </form>
        </div>

        <!-- 主配置区域（未验证时禁用） -->
        <div class="%DISABLE_MAIN%">
            <form method="POST" action="/save?lang=%LANG%">
                <div class="form-group">
                    <label>%SSID_LABEL%</label>
                    <input type="text" name="ssid" value="%SSID%" required>
                </div>
                <div class="form-group">
                    <label>%PASSWORD_LABEL%</label>
                    <input type="password" name="password" value="%PASSWORD%">
                </div>
                <div class="form-group">
                    <label>%TIMEZONE_LABEL%</label>
                    <input type="number" name="timezone" value="%TIMEZONE%" step="0.5" required>
                </div>
                <div class="form-group">
                    <label>%PROTO_LABEL%</label>
                    <select name="protocol">
                        <option value="0" %SEL_0%>JJY (40kHz) - Japan</option>
                        <option value="1" %SEL_1%>JJY (60kHz) - Japan</option>
                        <option value="2" %SEL_2%>WWVB (60kHz) - USA</option>
                        <option value="3" %SEL_3%>BPC (68.5kHz) - China</option>
                    </select>
                </div>
                <button type="submit">%SAVE_BTN%</button>
            </form>
            <hr style="margin: 1.5rem 0; border: 0; border-top: 1px solid #eee;">
            <form method="POST" action="/sync_ntp?lang=%LANG%">
                <button type="submit" style="background-color: #3b82f6;" %SYNC_DISABLED%>%SYNC_BTN%</button>
            </form>
        </div>
    </div>
</body>
</html>
)HTML";

// ==========================================
// 辅助函数
// ==========================================
LanguageStrings getLangStrings() {
  return (current_lang == "cn") ? lang_cn : lang_en;
}

// 生成混淆后的认证密钥（反向MAC + 异或加密）
String generateAuthKey(String mac) {
  // 1. 反转MAC字符串（原始清理后：AABBCCDDEEFF → FFEEDDCCBBAA）
  String reversed_mac = "";
  for (int i = mac.length() - 1; i >= 0; i--) {
    reversed_mac += mac[i];
  }
  
  // 2. 转字节数组并异或混淆
  uint8_t mac_bytes[6];
  // 字符串转字节（FFEEDDCCBBAA → 0xFF,0xEE,0xDD,0xCC,0xBB,0xAA）
  for (int i = 0; i < 6; i++) {
    String byte_str = reversed_mac.substring(i*2, i*2+2);
    mac_bytes[i] = strtol(byte_str.c_str(), NULL, 16);
    // 异或混淆
    mac_bytes[i] ^= XOR_KEY[i];
  }
  
  // 3. 混淆后的字节转回十六进制字符串（补前导0）
  String encrypted_key = "";
  for (int i = 0; i < 6; i++) {
    encrypted_key += byteToHex(mac_bytes[i]); // 使用补零函数
  }
  encrypted_key.toUpperCase(); // 统一转大写
  return encrypted_key;
}

// 验证密钥（反向处理：输入密钥→异或解密→转反向MAC→对比）
bool verifyAuthKey(String input) {
  input.toUpperCase();
  if (input.length() != 12) return false; // 校验长度（必须12位）
  
  // 1. 输入密钥转字节并异或解密
  uint8_t input_bytes[6];
  for (int i = 0; i < 6; i++) {
    String byte_str = input.substring(i*2, i*2+2);
    input_bytes[i] = strtol(byte_str.c_str(), NULL, 16);
    input_bytes[i] ^= XOR_KEY[i]; // 异或解密
  }
  
  // 2. 解密后的字节转回反向MAC字符串（补前导0）
  String decrypted_reversed = "";
  for (int i = 0; i < 6; i++) {
    decrypted_reversed += byteToHex(input_bytes[i]); // 补零
  }
  decrypted_reversed.toUpperCase();
  
  // 3. 生成原始反向MAC（用于对比）
  String mac_clean = device_mac;
  mac_clean.replace(":", "");
  mac_clean.toUpperCase();
  String original_reversed = "";
  for (int i = mac_clean.length() - 1; i >= 0; i--) {
    original_reversed += mac_clean[i];
  }
  
  // 调试打印（方便定位）
  Serial.printf("Original Reversed MAC: %s\n", original_reversed.c_str());
  Serial.printf("Decrypted Input Key: %s\n", decrypted_reversed.c_str());
  
  // 4. 对比解密后的值与原始反向MAC
  return (decrypted_reversed == original_reversed);
}

// 转换十进制到BCD
int dec2BCD(int decimal) {
  return (((decimal / 10) << 4) | (decimal % 10));
}

// 设置PWM频率
void setupPWM(int protocol) {
  int freq = 40000;
  switch (protocol) {
    case PROTO_JJY40: freq = 40000; break;
    case PROTO_JJY60: freq = 60000; break;
    case PROTO_WWVB:  freq = 60000; break;
    case PROTO_BPC:   freq = 68500; break;
  }
  // 配置LEDC：通道、频率、分辨率（8位）
  ledcSetup(ledChannel, freq, 8);
  ledcAttachPin(ledPin, ledChannel);
  ledcWrite(ledChannel, 0); // 初始关闭
  Serial.printf("PWM Configured: Protocol %d, Freq %d Hz\n", protocol, freq);
}

// ==========================================
// 编码逻辑 (JJY / WWVB / BPC)
// ==========================================
void JJY_encode(struct tm *timeInfo) {
  memset(sg, 0, 60);
  const int8_t M = -1; // Marker
  sg[0]=sg[9]=sg[19]=sg[29]=sg[39]=sg[49]=sg[59] = M; // 标记位
   
  // 提取时间
  int min = timeInfo->tm_min;
  int hour = timeInfo->tm_hour;
  int day = timeInfo->tm_yday + 1;
  int year = (timeInfo->tm_year + 1900) % 100;
  int wday = timeInfo->tm_wday;

  // 分钟编码（BCD）
  sg[1] = (min/10)>>2 & 1; sg[2] = (min/10)>>1 & 1; sg[3] = (min/10) & 1;
  sg[5] = (min%10)>>3 & 1; sg[6] = (min%10)>>2 & 1; sg[7] = (min%10)>>1 & 1; sg[8] = (min%10) & 1;
   
  // 小时编码（BCD）
  sg[12] = (hour/10)>>1 & 1; sg[13] = (hour/10) & 1;
  sg[15] = (hour%10)>>3 & 1; sg[16] = (hour%10)>>2 & 1; sg[17] = (hour%10)>>1 & 1; sg[18] = (hour%10) & 1;

  // 年积日编码
  int d_h = day / 100;
  int d_t = (day % 100) / 10;
  int d_u = day % 10;
  sg[22] = (d_h>>1)&1; sg[23] = d_h&1;
  sg[25] = (d_t>>3)&1; sg[26] = (d_t>>2)&1; sg[27] = (d_t>>1)&1; sg[28] = d_t&1;
  sg[30] = (d_u>>3)&1; sg[31] = (d_u>>2)&1; sg[32] = (d_u>>1)&1; sg[33] = d_u&1;

  // 奇偶校验
  int pa1 = (sg[12]^sg[13]^sg[15]^sg[16]^sg[17]^sg[18]);
  int pa2 = (sg[1]^sg[2]^sg[3]^sg[5]^sg[6]^sg[7]^sg[8]);
  sg[36] = pa1; 
  sg[37] = pa2;

  // 年份编码
  sg[41] = (year/10)>>3 & 1; sg[42] = (year/10)>>2 & 1; sg[43] = (year/10)>>1 & 1; sg[44] = (year/10) & 1;
  sg[45] = (year%10)>>3 & 1; sg[46] = (year%10)>>2 & 1; sg[47] = (year%10)>>1 & 1; sg[48] = (year%10) & 1;

  // 星期编码
  sg[50] = (wday>>2) & 1; sg[51] = (wday>>1) & 1; sg[52] = wday & 1;
   
  // 闰秒标记（默认0）
  sg[53] = 0; sg[54] = 0;
}

void WWVB_encode(struct tm *timeInfo) {
  memset(sg, 0, 60);
  const int8_t M = -1; // Marker
  sg[0]=sg[9]=sg[19]=sg[29]=sg[39]=sg[49]=sg[59] = M;
   
  int min = timeInfo->tm_min;
  int hour = timeInfo->tm_hour;
  int day = timeInfo->tm_yday + 1;
  int year = (timeInfo->tm_year + 1900) % 100;
   
  // 分钟编码
  sg[1] = (min/10)>>2 & 1; sg[2] = (min/10)>>1 & 1; sg[3] = (min/10) & 1;
  sg[5] = (min%10)>>3 & 1; sg[6] = (min%10)>>2 & 1; sg[7] = (min%10)>>1 & 1; sg[8] = (min%10) & 1;
   
  // 小时编码
  sg[12] = (hour/10)>>1 & 1; sg[13] = (hour/10) & 1;
  sg[15] = (hour%10)>>3 & 1; sg[16] = (hour%10)>>2 & 1; sg[17] = (hour%10)>>1 & 1; sg[18] = (hour%10) & 1;
   
  // 年积日编码
  int d_h = day / 100; int d_t = (day % 100) / 10; int d_u = day % 10;
  sg[22] = (d_h>>1)&1; sg[23] = d_h&1;
  sg[25] = (d_t>>3)&1; sg[26] = (d_t>>2)&1; sg[27] = (d_t>>1)&1; sg[28] = d_t&1;
  sg[30] = (d_u>>3)&1; sg[31] = (d_u>>2)&1; sg[32] = (d_u>>1)&1; sg[33] = d_u&1;
   
  // 年份编码
  sg[45] = (year/10)>>3 & 1; sg[46] = (year/10)>>2 & 1; sg[47] = (year/10)>>1 & 1; sg[48] = (year/10) & 1;
  sg[50] = (year%10)>>3 & 1; sg[51] = (year%10)>>2 & 1; sg[52] = (year%10)>>1 & 1; sg[53] = (year%10) & 1;
}

// 工具函数：统计一个整数二进制中1的个数
int count_ones(int num) {
    int count = 0;
    while(num > 0) {
        count += (num & 1); // 取最低位，是1则计数+1
        num >>= 1; // 右移一位
    }
    return count;
}

// 工具函数：字节转2位十六进制字符串（补前导0）
String byteToHex(uint8_t byte) {
  String hex = String(byte, HEX);
  if (hex.length() == 1) {
    hex = "0" + hex; // 补前导0（如0x5 → "05"）
  }
  hex.toUpperCase();
  return hex;
}

void BPC_encode(struct tm *timeInfo) {
    memset(sg, 0, 60); // 初始化数组
    
    // -------------------------- 1. 提取基础时间信息 --------------------------
    int hour_24 = timeInfo->tm_hour;    // 24小时制小时(0-23)
    int min = timeInfo->tm_min;         // 分钟(0-59)
    int sec = timeInfo->tm_sec;         // 秒数(0-59) → 用于计算20秒段
    int wday = (timeInfo->tm_wday == 0) ? 7 : timeInfo->tm_wday; // 星期(1-7，周日=7)
    int day = timeInfo->tm_mday;        // 日期(1-31)
    int mon = timeInfo->tm_mon + 1;     // 月份(1-12)
    int year = (timeInfo->tm_year + 1900) % 100; // 年份(00-99)
    
    // 12小时制转换（BPC主要用12小时制）
    int hour_12 = (hour_24 % 12) == 0 ? 12 : (hour_24 % 12); // 1-12（修正12点为12而非0）
    int is_pm = (hour_24 >= 12) ? 1 : 0;                      // 1=下午,0=上午

    // -------------------------- 2. 自动计算当前20秒段 --------------------------
    int sec_offset;
    if (sec < 20) {
        sec_offset = 0;   // 00-19秒 → 第1段（00秒段）
    } else if (sec < 40) {
        sec_offset = 20;  // 20-39秒 → 第2段（20秒段）
    } else {
        sec_offset = 40;  // 40-59秒 → 第3段（40秒段）
    }

    // -------------------------- 3. BPC时码位映射（00-19秒位） --------------------------
    // 00秒位：帧同步（固定脉宽0.4s，四进制=3）
    sg[0] = -1;

    // 01秒位：秒偏移标识（00/20/40秒段）
    // 权重：40(bit1) + 20(bit0) → 00=0, 20=1, 40=2
    switch(sec_offset) {
        case 0:  sg[1] = 0; break;
        case 20: sg[1] = 1; break;
        case 40: sg[1] = 2; break;
        default: sg[1] = 0; break;
    }

    // 02秒位：保留位（Unused，固定0）
    sg[2] = 0;

    // 03-04秒位：小时（12小时制，1-12）→ 6bit编码（实际用4bit）
    sg[3] = (hour_12 >> 2) & 0x03;  // 权重8(bit3) + 4(bit2)
    sg[4] = hour_12 & 0x03;          // 权重2(bit1) + 1(bit0)

    // 05-07秒位：分钟（0-59）→ 6bit编码
    sg[5] = (min >> 4) & 0x03;       // 权重32(bit5) + 16(bit4)
    sg[6] = (min >> 2) & 0x03;       // 权重8(bit3) + 4(bit2)
    sg[7] = min & 0x03;              // 权重2(bit1) + 1(bit0)

    // 08秒位：保留位（Unused，固定0）
    sg[8] = (wday >> 2) & 0x01;

    // 09秒位：星期（1-7）→ 权重2(bit1) + 1(bit0)
    sg[9] = wday & 0x03;

    // 10秒位：AM/PM + P1奇偶校验（偶校验：覆盖01-09秒位）
    int parity_p1 = 0;
    // 统计01-09秒位的所有二进制位中1的总数
    for(int i=1; i<=9; i++) {
        parity_p1 += count_ones(sg[i]); // 先统计每个数的1的个数，再累加
    }
    // 偶校验计算
    parity_p1 = (parity_p1 % 2) == 0 ? 0 : 1;
    // 10秒位：bit1=P1校验, bit0=AM/PM → 组合为四进制值
    sg[10] = (is_pm << 1) | (parity_p1);

    // 11秒位
    sg[11] =  (day >> 4) & 0x01;

    // 12-13秒位：日期（1-31）→ 6bit编码
    sg[12] = (day >> 2) & 0x03;      // 权重8(bit3) + 4(bit2)
    sg[13] = day & 0x03;             // 权重2(bit1) + 1(bit0)

    // 14-15秒位：月份（1-12）→ 6bit编码
    sg[14] = (mon >> 2) & 0x03;      // 权重8(bit3) + 4(bit2)
    sg[15] = mon & 0x03;             // 权重2(bit1) + 1(bit0)

    // 16-18秒位：年份（00-99）→ 8bit编码（用6bit）
    sg[16] = (year >> 4) & 0x03;     // 权重16(bit5) + 8(bit4)
    sg[17] = (year >> 2) & 0x03;     // 权重4(bit3) + 2(bit2)
    sg[18] = year & 0x03;            // 权重1(bit0)

    // 19秒位：P2奇偶校验（偶校验：覆盖11-18秒位）
    int parity_p2 = 0;
    for(int i=11; i<=18; i++) {
        parity_p2 += count_ones(sg[i]);
    }
    parity_p2 = (parity_p2 % 2) == 0 ? 0 : 1;
    
    sg[19] = (((year >> 6) & 0x01) << 2) | parity_p2; // 仅用最低位（四进制0/1）
}

// ==========================================
// 信号发射逻辑 (核心，移到loop中执行，避免中断不安全)
// 核心逻辑：每秒切换信号值，秒内前T毫秒低电平（载波关），后(1000-T)毫秒高电平（载波开）
// ==========================================
void processSignalTransmission() {
  // 1. 密钥未验证或NTP时间无效时，强制关闭载波
  if (!auth_verified || !ntp_time_valid) {
    if (current_duty != 0) {
      ledcWrite(ledChannel, 0);
      current_duty = 0;
    }
    return;
  }

  // 2. 获取当前时间（毫秒级）和本地时间
  unsigned long current_ms = millis();
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {  // 获取本地时间失败，直接返回
    if (current_duty != 0) {
      ledcWrite(ledChannel, 0);
      current_duty = 0;
    }
    return;
  }
  int now_sec = timeInfo.tm_sec;

  // 3. 秒切换时的初始化（核心：每秒更新一次编码和时间基准）
  if (now_sec != current_sec_index) {
    // 安全检查：秒数范围0-59，避免数组越界
    if (now_sec < 0 || now_sec >= 60) {
      current_sec_index = -1;
      if (current_duty != 0) {
        ledcWrite(ledChannel, 0);
        current_duty = 0;
      }
      return;
    }

    // 更新秒索引和秒起始毫秒
    current_sec_index = now_sec;
    sec_start_millis = current_ms;
    // 按协议更新编码（仅在需要的秒数触发）
    if (selected_protocol == PROTO_BPC) {
      // BPC每20秒更新一次完整帧
      if (now_sec % 20 == 0) {
        BPC_encode(&timeInfo);
        Serial.printf("\n[BPC] Frame Update: %02d:%02d:%02d (Segment %d)\n", 
                      timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec, now_sec/20);
      }
    } else {
      // WWVB/JJY每分钟（0秒）更新一次完整帧
      if (now_sec == 0) {
        if (selected_protocol == PROTO_WWVB) {
          WWVB_encode(&timeInfo);
        } else {
          JJY_encode(&timeInfo);
        }
        Serial.printf("\n[%s] Frame Update: %02d:%02d:%02d\n", 
                      (selected_protocol == PROTO_WWVB) ? "WWVB" : "JJY",
                      timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
      }
    }
    int curr_val_s;
    switch (selected_protocol) {
    case PROTO_BPC:{
      curr_val_s = sg[current_sec_index % 20];
      Serial.printf("%d ",curr_val_s);
      break;}
    default:{
      curr_val_s = sg[current_sec_index];
      Serial.printf("%d ",curr_val_s);
      break;}
    }
  }

  // 4. 计算当前秒内的毫秒偏移（0-999）
  uint16_t ms_in_sec_local = current_ms - sec_start_millis;
  if (ms_in_sec_local >= 1000) {  // 防止millis溢出导致的异常
    ms_in_sec_local = 999;
  }

  // 5. 按协议计算当前秒内的载波状态（核心时序逻辑）
  bool carrier_on = false;  // 载波开=高电平，载波关=低电平
  int low_duration = 0;     // 低电平持续时间（毫秒）：前low_duration毫秒低，后(1000-low_duration)毫秒高
  int curr_val;
  switch (selected_protocol) {
    case PROTO_BPC:{
      curr_val = sg[current_sec_index % 20];
      // BPC规则：低电平持续时间 = (val + 1) * 100ms（0→100ms，1→200ms，2→300ms，3→400ms）
      low_duration = (curr_val + 1) * 100;
      // 限制低电平时间在0-1000ms（防止非法值）
      low_duration = constrain(low_duration, 0, 1000);
      // 毫秒偏移 ≥ 低电平时间 → 载波开启（高电平）
      carrier_on = (ms_in_sec_local >= low_duration);
      break;}

    case PROTO_WWVB:{
      curr_val = sg[current_sec_index];
      // WWVB规则：
      // val=-1（标记秒）：800ms低，200ms高
      // val=1（1码）：500ms低，500ms高
      // val=0（0码）：200ms低，800ms高
      if (curr_val == -1) {
        low_duration = 800;
      } else if (curr_val == 1) {
        low_duration = 500;
      } else {
        low_duration = 200;
      }
      carrier_on = (ms_in_sec_local >= low_duration);
      break;}

    case PROTO_JJY40:
    case PROTO_JJY60:{
      curr_val = sg[current_sec_index];
      // JJY40/JJY60规则（与WWVB相反）：
      // val=-1（标记码）：200ms高，800ms低 → 低电平持续800ms
      // val=1（1码）：500ms高，500ms低 → 低电平持续500ms
      // val=0（0码）：800ms高，200ms低 → 低电平持续200ms
      if (curr_val == -1) {
        low_duration = 800;
      } else if (curr_val == 1) {
        low_duration = 500;
      } else {
        low_duration = 200;
      }
      carrier_on = (ms_in_sec_local < (1000 - low_duration));
      break;}

    default:{
      // 未知协议，强制关闭载波
      carrier_on = false;
      break;}
  }

  // 6. 更新PWM输出（仅在电平变化时更新，减少操作）
  int target_duty = carrier_on ? 127 : 0;  // 50%占空比（载波开）/ 0%（载波关）
  if (target_duty != current_duty) {
    ledcWrite(ledChannel, target_duty);
    current_duty = target_duty;
    // 可选：调试电平切换（如需详细日志可开启）
    // Serial.printf("[%02d:%03d] Duty: %d (Carrier: %s)\n", now_sec, ms_in_sec_local, target_duty, carrier_on ? "ON" : "OFF");
  }
}
// ==========================================
// Web Server 辅助函数
// ==========================================
String generateHTML(String status = "") {
  LanguageStrings lang = getLangStrings();
  String html = String(html_template);
  
  // 替换语言变量
  html.replace("%LANG%", current_lang);
  html.replace("%TITLE%", lang.title);
  html.replace("%HEADER%", lang.header);
  html.replace("%SSID_LABEL%", lang.ssid_label);
  html.replace("%PASSWORD_LABEL%", lang.password_label);
  html.replace("%TIMEZONE_LABEL%", lang.timezone_label);
  html.replace("%PROTO_LABEL%", lang.proto_label);
  html.replace("%SAVE_BTN%", lang.save_btn);
  html.replace("%SYNC_BTN%", lang.sync_btn);
  html.replace("%LANG_EN%", lang.lang_en);
  html.replace("%LANG_CN%", lang.lang_cn);
  html.replace("%EN_ACTIVE%", (current_lang == "en") ? "active" : "");
  html.replace("%CN_ACTIVE%", (current_lang == "cn") ? "active" : "");
  
  // 新增密钥验证相关替换
  html.replace("%AUTH_TITLE%", lang.auth_title);
  html.replace("%DEVICE_MAC_LABEL%", lang.device_mac_label);
  html.replace("%AUTH_KEY_LABEL%", lang.auth_key_label);
  html.replace("%AUTH_KEY_PLACEHOLDER%", lang.auth_key_placeholder);
  html.replace("%VERIFY_BTN%", lang.verify_btn);
  html.replace("%DEVICE_MAC%", device_mac); // 填充设备MAC
  // 未验证时禁用主配置区域
  html.replace("%DISABLE_MAIN%", auth_verified ? "" : "disabled-section");
  
  // 替换配置值
  html.replace("%SSID%", wifi_ssid);
  html.replace("%PASSWORD%", wifi_password);
  html.replace("%TIMEZONE%", String(timezone_offset));
  html.replace("%STATUS%", status);
  
  // 协议选择
  for(int i=0; i<4; i++) {
    String tag = "%SEL_" + String(i) + "%";
    html.replace(tag, (selected_protocol == i) ? "selected" : "");
  }

  // AP模式提示
  if (config_mode) {
    html.replace("%AP_MODE_HINT%", "<div class='status info'>" + String(lang.ap_mode_hint) + "</div>");
    html.replace("%SYNC_DISABLED%", "disabled style='background-color:#ccc;cursor:not-allowed;'");
  } else {
    html.replace("%AP_MODE_HINT%", "");
    html.replace("%SYNC_DISABLED%", auth_verified ? "" : "disabled style='background-color:#ccc;cursor:not-allowed;'");
  }
  
  return html;
}

void handleSave() {
  // 更新语言
  String lang = server.arg("lang");
  if (lang == "en" || lang == "cn") current_lang = lang;
   
  // 读取表单参数
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");
  float new_timezone = server.arg("timezone").toFloat();
  int new_proto = server.arg("protocol").toInt();
   
  // 保存到Flash
  prefs.putString("wifi_ssid", new_ssid);
  prefs.putString("wifi_password", new_password);
  prefs.putFloat("timezone_offset", new_timezone);
  prefs.putInt("protocol", new_proto);
   
  // 更新运行时变量
  wifi_ssid = new_ssid;
  wifi_password = new_password;
  timezone_offset = new_timezone;
  selected_protocol = new_proto;
  setupPWM(selected_protocol); // 更新PWM频率

  // 响应页面
  String html = generateHTML(String("<div class='status success'>") + getLangStrings().success_msg + "</div>");
  server.send(200, "text/html; charset=UTF-8", html);
   
  // 重启设备
  delay(1000);
  ESP.restart();
}
// 密钥验证处理
void handleVerifyKey() {
  if (server.hasArg("lang")) current_lang = server.arg("lang");
  LanguageStrings lang = getLangStrings();
  input_key = server.arg("auth_key");
  input_key.replace(":", ""); // 移除可能的冒号
  input_key.toUpperCase();    // 统一转大写
  
  String status;
  // 使用新的验证函数（异或解密后对比）
  if (verifyAuthKey(input_key)) {
    auth_verified = true;
    prefs.putBool("auth_verified", true); // 保存验证状态
    status = "<div class='status success'>" + String(lang.auth_success) + "</div>";
    Serial.println("Authentication successful!");
  } else {
    auth_verified = false;
    status = "<div class='status error'>" + String(lang.auth_failed) + "</div>";
    Serial.printf("Auth failed! Input: %s\n", input_key.c_str());
  }
  
  server.send(200, "text/html; charset=UTF-8", generateHTML(status));
}

void setupWebServer() {
  // 根页面
  server.on("/", HTTP_GET, []() {
    if (server.hasArg("lang")) current_lang = server.arg("lang");
    server.send(200, "text/html; charset=UTF-8", generateHTML(""));
  });
   
  // 密钥验证
  server.on("/verify_key", HTTP_POST, handleVerifyKey);
   
  // 保存配置
  server.on("/save", HTTP_POST, handleSave);
   
  // 同步NTP（增加密钥验证检查）
  server.on("/sync_ntp", HTTP_POST, []() {
    if (server.hasArg("lang")) current_lang = server.arg("lang");
    LanguageStrings lang = getLangStrings();
    
    // 先检查密钥验证
    if (!auth_verified) {
      server.send(200, "text/html; charset=UTF-8", generateHTML("<div class='status error'>请先完成设备认证！</div>"));
      return;
    }
    
    if (config_mode) {
      server.send(200, "text/html; charset=UTF-8", generateHTML(String("<div class='status error'>") + lang.ap_mode_ntp_error + "</div>"));
    } else {
      bool ok = syncNTPTime();
      String msg = ok ? lang.ntp_success : lang.ntp_error;
      String cls = ok ? "success" : "error";
      server.send(200, "text/html; charset=UTF-8", generateHTML(String("<div class='status ") + cls + "'>" + msg + "</div>"));
    }
  });
   
  // 404重定向
  server.onNotFound([](){
      server.sendHeader("Location", "http://" + apIP.toString() + "/", true);
      server.send(302, "text/plain", "Redirect"); 
  });
   
  server.begin();
  Serial.println("Web Server Started");
}
// 同步NTP时间（修正时区配置）
bool syncNTPTime() {
  if (WiFi.status() != WL_CONNECTED) return false;

  // 正确的时区配置（东八区：GMT-8 或 CST-8）
  char tz[32];
  snprintf(tz, sizeof(tz), "GMT%+.1f", -timezone_offset); // 东八区：GMT-8
  setenv("TZ", tz, 1);
  tzset();

  // 配置NTP服务器
  configTime(timezone_offset * 3600L, 0, "ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org");

  // 等待时间同步
  struct tm t;
  unsigned long start = millis();
  while (!getLocalTime(&t) && millis() - start < 15000) {
    delay(500);
  }

  if (getLocalTime(&t)) {
    ntp_time_valid = true;
    last_ntp_sync = millis();
    Serial.printf("NTP Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    digitalWrite(LEDG, LOW);     
    return true;
  }

  ntp_time_valid = false;
  digitalWrite(LEDR, LOW);
  return false;
}

// 连接WiFi
bool connectToWiFi() {
  if (wifi_ssid.isEmpty()) return false;
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  
  unsigned long start = millis();
  Serial.print("Connecting to WiFi: ");
  Serial.println(wifi_ssid);
  
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi Connected: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi Connect Failed");
    return false;
  }
}

// 启动AP模式
void startAPMode() {
  // 获取设备MAC地址并格式化（去掉冒号，转大写）
  String mac = WiFi.macAddress();
  mac.replace(":", "");  // 移除MAC中的冒号
  mac.toUpperCase();     // 转大写（可选）
  
  // 生成带MAC后缀的AP名称（取后6位避免过长，也可保留完整）
  ap_ssid = ap_ssid_base + "-" + mac.substring(6);  // 例如：ClockWave-Config-ABCD12
  
  // 启动AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(ap_ssid.c_str(), ap_password);  // 传入动态生成的AP名称
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // 打印带MAC的AP信息
  Serial.printf("AP Mode Started: %s (Password: %s), IP: %s\n", 
                ap_ssid.c_str(), ap_password, apIP.toString().c_str());
}

// ==========================================
// 主函数
// ==========================================
void setup() {
  // 初始化串口
  Serial.begin(115200);
  delay(100);
  
  // 新增：初始化SPIFFS（忽略失败，仅解决链接问题）
  if (!SPIFFS.begin(true)) { 
    Serial.println("SPIFFS Mount Failed (ignore for this project)");
  }
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDR, HIGH);

  pinMode(LEDG, OUTPUT);
  digitalWrite(LEDG, HIGH);

  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDB, HIGH);

  // 初始化Flash存储
  prefs.begin("ClockWave", false);
  
  // ========== 新增：初始化MAC和认证密钥 ==========
  device_mac = WiFi.macAddress(); // 获取原始MAC（带冒号）
  String mac_clean = device_mac;
  mac_clean.replace(":", "");
  mac_clean.toUpperCase();
  auth_key = generateAuthKey(mac_clean); // 生成反向+异或混淆的密钥

  Serial.printf("Device MAC: %s\n", device_mac.c_str());
  Serial.printf("Original Reversed MAC: ");
  String original_reversed = "";
  for (int i = mac_clean.length() - 1; i >= 0; i--) original_reversed += mac_clean[i];
  Serial.println(original_reversed);
  Serial.printf("Encrypted Auth Key: %s\n", auth_key.c_str()); // 混淆后的密钥
  Serial.printf("Auth Status: %s\n", auth_verified ? "Verified" : "Not Verified");
  
  // 读取配置
  wifi_ssid = prefs.getString("wifi_ssid","");
  wifi_password = prefs.getString("wifi_password","");
  timezone_offset = prefs.getFloat("timezone_offset",8.0f);
  selected_protocol = prefs.getInt("protocol",PROTO_JJY40);

  // 初始化PWM
  setupPWM(selected_protocol);

  // 连接WiFi或启动AP
  if (!connectToWiFi()) {
    config_mode = true;
    startAPMode();
    digitalWrite(LEDB, LOW);
  } else {
    // 同步NTP时间（仅当密钥验证通过时）
    if (auth_verified) {
      syncNTPTime();
    } else {
      Serial.println("Skip NTP sync: Authentication required");
    }
  }

  // 初始化Web服务器
  setupWebServer();

  Serial.println("System Initialized");
}

void loop() {
  // 处理DNS（AP模式）
  if (config_mode) dnsServer.processNextRequest();
  
  // 处理Web请求
  server.handleClient();

  // 处理信号发射
  processSignalTransmission();

  // 定时同步NTP
  if (!config_mode && WiFi.status() == WL_CONNECTED && millis() - last_ntp_sync > ntp_sync_interval) {
    syncNTPTime();
  }

  // 小延迟，避免占用过高CPU
  delay(1);
}
