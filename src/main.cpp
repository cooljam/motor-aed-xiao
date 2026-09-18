/*
 * 航模电机震动识别 - XIAO nRF52840 Sense 固件
 *
 * 功能:
 * - 上电后**持续连续采集**LIS3DH三轴加速度数据
 * - 持续通过BLE发送给手机小程序
 * - 手机点击采集，手机自行记录固定长度，不需要主板端控制
 * - 主板始终连续发送，不需要启停
 *
 * Author: Motor AED Project
 * License: MIT
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include <BLEPeripheral.h>

// ============ 调试配置 ============
// #define DEBUG_OUTPUT_RAW  1  // 打开: 串口输出原始数据方便调试，关闭: 只输出调试信息不输出数据

// ============ 配置参数 ============
#define SAMPLE_RATE     LIS3DH_DATARATE_1000_HZ  // 1000Hz采样
#define ACCEL_RANGE     LIS3DH_RANGE_8_G         // 量程 ±8g，灵敏度足够适合航模震动
#define BLE_PACKET_SIZE 256                     // BLE数据包大小（包含SEQ头部）

// ============ BLE UUID定义 ============
#define SERVICE_UUID           "0000FFB0-0000-1000-8000-00805F9B34FB"
#define DATA_CHARACTERISTIC_UUID "0000FFB1-0000-1000-8000-00805F9B34FB"

// ============ 全局对象 ============
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
BLEPeripheral ble;
BLECharacteristic dataCharacteristic(DATA_CHARACTERISTIC_UUID, BLENotify, BLE_PACKET_SIZE);

// ============ 全局变量 ============
// 缓冲区: 字节0预留SEQ，数据从字节1开始放
uint8_t bleBuffer[BLE_PACKET_SIZE];
uint16_t bufferBytes = 1;  // 第一个字节留给SEQ，从1开始放数据
uint8_t seq = 0;

// LED闪烁
unsigned long lastBlink = 0;
bool ledState = false;

// ============ 函数声明 ============
void bufferPush(int16_t value);
void bufferClear();
void bleNotifyData();
void blinkLED();

// ============ 初始化 ============
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== Motor AED XIAO Firmware Starting ===");

  // 1. 初始化LIS3DH加速度计
  if (!lis.begin(0x18)) {
    Serial.println("Could not find LIS3DH! Check wiring.");
    while (1) delay(1000);
  }
  Serial.println("Found LIS3DH");

  lis.setDataRate(SAMPLE_RATE);
  lis.setRange(ACCEL_RANGE);
  lis.enableFIFO(true);
  lis.setFIFOMode(LIS3DH_FIFOMODE_STREAM);
  Serial.printf("Sample rate: 1000Hz, Range: ±%dg, FIFO: Stream mode\n", ACCEL_RANGE);

  // 2. 初始化BLE
  ble.setAdvertisementData();
  ble.setLocalName("Motor-AED");
  ble.addService(SERVICE_UUID);
  ble.characteristics().add(dataCharacteristic);
  ble.begin();
  ble.startAdvertising();
  Serial.println("BLE started, advertising as 'Motor-AED'");

  // 3. 初始化LED
  pinMode(LED_BUILTIN, OUTPUT);

  bufferClear();
  Serial.println("=== Initialization complete ===");
}

// ============ 主循环 ============
void loop() {
  // 1. 处理BLE事件
  ble.poll();

  // 2. 检查LIS3DH FIFO是否有数据
  uint16_t available = lis.getFIFO() & 0x1F;

  if (available > 0) {
    for (int i = 0; i < available; i++) {
      lis.read();
      int16_t x = lis.x;
      int16_t y = lis.y;
      int16_t z = lis.z;

      // 始终打包发送
      bufferPush(x);
      bufferPush(y);
      bufferPush(z);

      // 如果缓冲区满了，发送一包
      if (bufferBytes >= BLE_PACKET_SIZE) {
        bleNotifyData();
        bufferClear();
      }
    }
  }

  // 3. 如果缓冲区还有数据但没满，超时也发送（低延迟）
  if (bufferBytes > 1) {
    static unsigned long lastSend = 0;
    unsigned long now = millis();
    if (now - lastSend > 10) {  // 超过10ms就发送，保证低延迟
      bleNotifyData();
      bufferClear();
      lastSend = now;
    }
  }

  // 4. LED闪烁表示运行
  blinkLED();
}

// ============ 函数实现 ============

// 缓冲区一开始就预留一个字节给SEQ，数据从index=1开始
void bufferClear() {
  bufferBytes = 1;  // 字节0留给SEQ
}

// 将16位值压入缓冲区（小端字节序）
void bufferPush(int16_t value) {
  if (bufferBytes + 2 <= BLE_PACKET_SIZE) {
    bleBuffer[bufferBytes] = value & 0xFF;
    bleBuffer[bufferBytes + 1] = (value >> 8) & 0xFF;
    bufferBytes += 2;
  }
}

// 通过BLE发送数据包
void bleNotifyData() {
  // 第一个字节放SEQ序号
  bleBuffer[0] = seq++;
  if (seq > 255) seq = 0;

  dataCharacteristic.setValue(bleBuffer, bufferBytes);

#ifdef DEBUG_OUTPUT_RAW
  // 串口输出原始数据方便调试
  for (uint16_t i = 0; i < bufferBytes; i++) {
    Serial.print(bleBuffer[i], HEX);
    if (i < bufferBytes - 1) Serial.print(',');
  }
  Serial.println();
#else
  if (Serial.availableForWrite() > 0) {
    Serial.printf("Sent packet seq=%d, len=%d\r\n", (int)bleBuffer[0], (int)bufferBytes);
  }
#endif
}

// LED闪烁，表示程序正在运行
void blinkLED() {
  unsigned long now = millis();
  if (now - lastBlink >= 500) {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}
