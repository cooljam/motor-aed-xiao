/*
 * 对照实验: 启动 Wire1 前后读 SDA/SCL 电平
 *
 * 现象:
 *   v4 诊断 (用 Wire1.begin()): PWR=HIGH 时 SCL=1, SDA=0
 *   纯 GPIO 测试: PWR=HIGH 时 SCL=0, SDA=0
 *
 * 假设: Wire1.begin() 配置了引脚外设, 或者 TWIM 激活了什么
 */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TinyUSB.h>

#define PIN_SDA   PIN_WIRE1_SDA
#define PIN_SCL   PIN_WIRE1_SCL
#define PIN_PWR   PIN_LSM6DS3TR_C_POWER

void readLevels(const char *label) {
  // 先切 GPIO 输入上拉读
  // 注意: 从 Wire 外设切回 GPIO 可能需要重新配置
  Serial.printf("  %-35s: SDA=", label);
  Serial.flush();

  // 用 nrf_gpio 直接读, 保持当前配置
  // 简单起见, 用 pinMode + digitalRead
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delay(1);
  int sda = digitalRead(PIN_SDA);
  int scl = digitalRead(PIN_SCL);
  Serial.printf("%d SCL=%d\r\n", sda, scl);
  Serial.flush();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    digitalWrite(LED_BUILTIN, (millis() / 200) & 1 ? HIGH : LOW);
  }

  Serial.println("\r\n\r\n=== Wire1 vs GPIO Level Comparison ===");
  Serial.flush();

  // 供电先 HIGH
  pinMode(PIN_PWR, OUTPUT);
  digitalWrite(PIN_PWR, HIGH);
  delay(150);
  Serial.println("IMU Power = ON (HIGH)");
  Serial.flush();

  // --- 纯 GPIO ---
  readLevels("1. Pure GPIO INPUT_PULLUP (no Wire1)");

  // --- 启动 Wire1 (100kHz) ---
  Serial.println("\r\n2. Starting Wire1...");
  Serial.flush();
  Wire1.begin();
  Wire1.setClock(100000);
  delay(5);
  readLevels("3. After Wire1.begin() + setClock");

  // --- 尝试用 Wire1 读一下 WHO_AM_I (可能挂死, 小心) ---
  // 先扫描看看会不会像 v4 一样挂死
  Serial.println("\r\n4. Wire1 scanning address 0x6A...");
  Serial.flush();

  // 先用快速扫描测试, 如果挂了就知道了
  Wire1.beginTransmission(0x6A);
  uint8_t err = Wire1.endTransmission();
  Serial.printf("   0x6A endTransmission = %d (0=ACK)\r\n", err);
  Serial.flush();

  if (err == 0) {
    // 尝试读 WHO_AM_I
    Wire1.beginTransmission(0x6A);
    Wire1.write(0x0F); // WHO_AM_I
    err = Wire1.endTransmission(false);
    if (err == 0) {
      uint8_t n = Wire1.requestFrom((uint8_t)0x6A, (uint8_t)1);
      if (n >= 1) {
        uint8_t v = Wire1.read();
        Serial.printf("   WHO_AM_I = 0x%02X %s\r\n", v,
                      (v == 0x6A) ? "<<< LSM6DS3TR-C!" : "");
      } else {
        Serial.println("   requestFrom returned 0 bytes");
      }
    }
  }
  Serial.flush();

  // --- Wire1.end() 后再看 ---
  Serial.println("\r\n5. After Wire1.end()...");
  Wire1.end();
  delay(2);
  readLevels("6. After Wire1.end() (back to GPIO PU)");

  Serial.println("\r\n=== TEST COMPLETE ===");
  Serial.println("LED slow blink.");
  Serial.flush();
}

uint32_t lastBlink = 0;
bool ledState = false;

void loop() {
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
    lastBlink = millis();
  }
}
