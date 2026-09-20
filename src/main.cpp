/*
 * Motor AED XIAO - IMU streaming over BLE
 * Firmware v2.9
 *
 * 改用 BLEUart (Nordic UART Service), 简化 notify, 用最稳妥的方式
 */

#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>

#define FW_VERSION "v2.10"

// ---- Pin / address ----
#define PIN_IMU_PWR   PIN_LSM6DS3TR_C_POWER  // D15 = P1.08
#define IMU_ADDR      0x6A

// ---- LSM6DS3TR-C registers ----
#define REG_WHO_AM_I      0x0F
#define REG_CTRL1_XL      0x10
#define REG_CTRL3_C       0x12
#define REG_OUTX_L_XL     0x28
#define REG_OUTX_H_XL     0x29
#define REG_OUTY_L_XL     0x2A
#define REG_OUTY_H_XL     0x2B
#define REG_OUTZ_L_XL     0x2C
#define REG_OUTZ_H_XL     0x2D

// ---- BLE ----
#define DEVICE_NAME "MotorAED-XIAO"

BLEUart bleuart; // 现成的 UART over BLE, 提供标准 Nordic UART Service

// ---- I2C primitives (v1.13 验证过的模式, 全部带 STOP) ----

static bool i2cScan(uint8_t addr) {
  Wire1.beginTransmission(addr);
  return Wire1.endTransmission() == 0;
}

static bool i2cWrite(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(IMU_ADDR);
  Wire1.write(reg);
  Wire1.write(val);
  return Wire1.endTransmission() == 0;
}

static uint8_t i2cRead(uint8_t reg) {
  Wire1.beginTransmission(IMU_ADDR);
  Wire1.write(reg);
  Wire1.endTransmission();
  Wire1.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1);
  return Wire1.read();
}

static bool i2cReadBuf(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire1.beginTransmission(IMU_ADDR);
  Wire1.write(reg);
  Wire1.endTransmission();
  uint8_t got = Wire1.requestFrom((uint8_t)IMU_ADDR, len);
  if (got < len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
  return true;
}

// ---- IMU init (v1.13 验证序列) ----
bool imuBegin(void) {
  delay(500);

  pinMode(PIN_IMU_PWR, OUTPUT);
  digitalWrite(PIN_IMU_PWR, HIGH);
  delay(200);

  pinMode(PIN_WIRE1_SDA, OUTPUT); digitalWrite(PIN_WIRE1_SDA, HIGH);
  pinMode(PIN_WIRE1_SCL, OUTPUT); digitalWrite(PIN_WIRE1_SCL, HIGH);
  delay(10);
  pinMode(PIN_WIRE1_SDA, INPUT_PULLUP);
  pinMode(PIN_WIRE1_SCL, INPUT_PULLUP);
  delay(2);

  Wire1.begin();
  Wire1.setClock(100000);
  delay(5);

  bool ack = i2cScan(IMU_ADDR);
  if (!ack) {
    Serial.println("[IMU] FAIL: no ACK at 0x6A");
    return false;
  }
  Serial.println("[IMU] ACK ok");

  uint8_t wai = i2cRead(REG_WHO_AM_I);
  Serial.printf("[IMU] WHO_AM_I = 0x%02X %s\r\n", wai,
                (wai == 0x6A) ? "<<< LSM6DS3TR-C" : "(mismatch!)");
  if (wai != 0x6A) {
    Serial.println("[IMU] FAIL: WHO_AM_I mismatch");
    return false;
  }

  i2cWrite(REG_CTRL3_C, 0x01); delay(50);   // SW_RESET
  i2cWrite(REG_CTRL3_C, 0x44);              // BDU + IF_INC
  i2cWrite(REG_CTRL1_XL, 0x80);             // 1.66kHz, ±2g
  delay(20);

  Serial.println("[IMU] configured OK");
  return true;
}

// ---- Setup ----
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
    digitalWrite(LED_BUILTIN, (millis() / 200) & 1 ? LOW : HIGH);
  }

  Serial.printf("\r\n\r\n=== Motor AED XIAO Firmware %s ===\r\n", FW_VERSION);
  Serial.flush();

  Serial.println("Initializing BLE...");
  Serial.flush();
  if (!Bluefruit.begin()) {
    Serial.println("[BLE] begin FAILED!");
    while (1) { delay(100); }
  }
  Bluefruit.setName(DEVICE_NAME);
  bleuart.begin();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.start(0);
  Serial.println("  BLE advertising started");
  Serial.flush();

  Serial.println("Initializing IMU...");
  Serial.flush();
  bool ok = imuBegin();
  if (!ok) {
    Serial.println("[IMU] INIT FAILED");
    Serial.flush();
    while (1) {
      digitalWrite(LED_BUILTIN, LOW); delay(100);
      digitalWrite(LED_BUILTIN, HIGH); delay(100);
    }
  }

  Serial.println("IMU streaming started");
  Serial.flush();
}

// ---- Main loop: 直接读实时数据 -> 通过 BLEUart 发送 ----
uint8_t txBuf[6];

void loop() {
  if (!bleuart.notifyEnabled()) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    return;
  }

  // 直接读 OUTX_L - OUTZ_H (6字节)
  if (!i2cReadBuf(REG_OUTX_L_XL, txBuf, sizeof(txBuf))) {
    Serial.println("[IMU] read failed");
    Serial.flush();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(10);
    return;
  }

  // 串口打印数据，方便调试
  int16_t x = (int16_t)(txBuf[1] << 8 | txBuf[0]);
  int16_t y = (int16_t)(txBuf[3] << 8 | txBuf[2]);
  int16_t z = (int16_t)(txBuf[5] << 8 | txBuf[4]);
  Serial.printf("[IMU] X=%d Y=%d Z=%d\r\n", x, y, z);
  Serial.flush();

  // 通过 BLEUart notify 发送 (BLEUart 继承 Stream, 内部用 notify 传输)
  bleuart.write(txBuf, sizeof(txBuf));
  digitalWrite(LED_BUILTIN, LOW);
  delay(10); // 降低发送频率，避免拥塞
}
