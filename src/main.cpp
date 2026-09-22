/*
 * Motor AED XIAO - IMU streaming over BLE
 * Firmware v3.0
 *
 * BLEUart (Nordic UART Service), 批量传输, 带SEQ序号
 * 协议: [SEQ:1] + [X_L,X_H,Y_L,Y_H,Z_L,Z_H] * N
 */

#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include <Adafruit_TinyUSB.h>

#define FW_VERSION "v3.0"

// ---- Pin / address ----
#define PIN_IMU_PWR   PIN_LSM6DS3TR_C_POWER
#define IMU_ADDR      0x6A

// ---- LSM6DS3TR-C registers ----
#define REG_WHO_AM_I      0x0F
#define REG_CTRL1_XL      0x10
#define REG_CTRL3_C       0x12
#define REG_OUTX_L_XL     0x28
#define REG_OUTY_L_XL     0x2A
#define REG_OUTZ_L_XL     0x2C

// ---- BLE ----
#define DEVICE_NAME "MotorAED-XIAO"

BLEUart bleuart;

// 每包采样点数：1字节SEQ + N*6字节数据
// 默认MTU=23字节 → 有效载荷≈20字节 → 可放3个样本(19字节)
// MTU协商后更大，可放更多样本
#define SAMPLES_PER_PACKET  6   // 需要MTU≥40字节; 如不工作改回3
#define SAMPLE_RATE_HZ      833 // 采样率：1660对BLE偏高易丢包，降为833(奈奎斯特416Hz，覆盖<400Hz分析)
static uint8_t txBuf[1 + SAMPLES_PER_PACKET * 6];
static uint8_t seq = 0;

// ---- I2C primitives ----

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

// ---- IMU init ----
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
  Wire1.setClock(400000);  // 400kHz I2C, 更快读取
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
  i2cWrite(REG_CTRL1_XL, 0x70);             // 833Hz, ±2g
  delay(20);

  Serial.println("[IMU] configured OK (833Hz, ±2g)");
  return true;
}

// ---- BLE连接回调 ----
void connect_callback(uint16_t conn_handle) {
  Serial.printf("[BLE] Connected (MTU=%d)\r\n",
                Bluefruit.Connection(conn_handle)->getMtu());
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  Serial.println("[BLE] Disconnected");
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
  // 提高BLE带宽：默认 SoftDevice 是 MTU=23 / HVN队列=1 / 事件长度=3，
  // 37字节包会被拆成2个通知且队列瞬间占满 → write()返回0、持续丢包。
  // BANDWIDTH_MAX → MTU=247 / HVN队列=3 / 事件长度=100(启用DLE)，必须在 begin() 前设置
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  if (!Bluefruit.begin()) {
    Serial.println("[BLE] begin FAILED!");
    while (1) { delay(100); }
  }
  Bluefruit.setName(DEVICE_NAME);
  Bluefruit.Periph.setConnInterval(6, 10); // 更快连接间隔 7.5ms~12.5ms
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

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

// ---- Main loop: 批量读取 + BLE发送 ----
// 目标采样率 833Hz，每包 SAMPLES_PER_PACKET 个样本 → 每包周期 ≈ 7203us
void loop() {
  if (!bleuart.notifyEnabled()) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    return;
  }

  uint32_t tStart = micros();

  // 填充发送缓冲区：第0字节是SEQ（发送成功后才递增，见下方write检查）
  txBuf[0] = seq;

  // 批量读取SAMPLES_PER_PACKET个样本
  bool readOk = true;
  for (uint8_t s = 0; s < SAMPLES_PER_PACKET; s++) {
    uint8_t* p = &txBuf[1 + s * 6];
    // 连续读6字节 (X_L到Z_H)
    if (!i2cReadBuf(REG_OUTX_L_XL, p, 6)) {
      readOk = false;
      break;
    }
  }

  if (!readOk) {
    Serial.println("[IMU] read failed");
    Serial.flush();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(2);
    return;
  }

  // 发送整包（MTU协商≥40时一个通知装下，不会分包）
  // 检查返回值：BLE TX FIFO满时 write() 短写会丢包并污染流，
  // 此时丢弃本包且不递增seq，避免接收端把溢出误判成"丢包"
  size_t written = bleuart.write(txBuf, sizeof(txBuf));
  if (written == sizeof(txBuf)) {
    seq++;
  } else {
    static uint32_t dropCount = 0;
    if ((++dropCount % 100) == 1) {
      Serial.printf("[BLE] short write %u/%u (drops=%lu)\r\n",
                    (unsigned)written, (unsigned)sizeof(txBuf), (unsigned long)dropCount);
    }
  }
  digitalWrite(LED_BUILTIN, LOW);

  // 精确节奏：每包固定 = SAMPLES_PER_PACKET / 1660 秒，
  // 避免突发超发导致BLE拥塞丢包；读取+发送已超时就跳过延时
  static const uint32_t targetPeriodUs = (uint32_t)(1000000.0f * SAMPLES_PER_PACKET / SAMPLE_RATE_HZ); // ≈7203us
  uint32_t elapsed = micros() - tStart;
  if (elapsed < targetPeriodUs) {
    delayMicroseconds(targetPeriodUs - elapsed);
  }

  // 串口调试：每200包打印一次（去掉每循环的阻塞式打印，避免拖慢节奏造成突发）
  static uint32_t loopCount = 0;
  if (++loopCount % 200 == 0) {
    int16_t x = (int16_t)(txBuf[2] << 8 | txBuf[1]);
    int16_t y = (int16_t)(txBuf[4] << 8 | txBuf[3]);
    int16_t z = (int16_t)(txBuf[6] << 8 | txBuf[5]);
    Serial.printf("[IMU] X=%d Y=%d Z=%d (seq=%d)\r\n", x, y, z, txBuf[0]);
  }
}
