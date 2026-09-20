# motor-aed-xiao

> 航模电机震动识别 - Seeed XIAO nRF52840 Sense 固件
>
> 本项目是 [motor-aed-app](https://github.com/xxx/motor-aed-app) 的开源主板端固件，手机小程序端闭源放在 motor-aed-app 仓库。

## 功能

- 上电后**持续连续采集**板载 LSM6DS3TR-C 三轴加速度数据
- 通过 **Nordic UART Service (BLEUart)** 持续 Notify 发送给手机
- **手机端点击采集，手机自行记录固定长度**，不需要给主板发命令
- 支持串口调试输出

## 硬件

- 主控: Seeed XIAO nRF52840 Sense
- IMU: LSM6DS3TR-C (I2C 地址 0x6A)
- BLE: nRF52840 原生 SoftDevice

## 数据传输协议

### BLE GATT 定义

使用标准 Nordic UART Service (NUS):

|  | UUID | 属性 | 方向 |
|------|------|------|------|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | 服务 | 双向 |
| TX | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify | XIAO → 手机 |
| RX | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write | 手机 → XIAO |

- 广播设备名: `MotorAED-XIAO`

### 数据流格式

通过 TX Notify 发送**原始二进制流**，无固定包边界（流式传输）：

- 每 6 字节代表一个采样点
- 采样点内顺序: X (2B 小端) → Y (2B 小端) → Z (2B 小端)
- 每个轴是 16-bit 补码，对应 LSM6DS3TR-C 原始输出

```
| X_L | X_H | Y_L | Y_H | Z_L | Z_H | X_L | X_H | ...
```

### IMU 配置

- 加速度 ODR: 1.66 kHz
- 加速度量程: ±2 g
- 转换为物理量: m/s² = count / 16384 * 9.80665

### 发送策略

- 每读取一次 IMU 发送 6 字节
- 每发送一包后加 10 ms delay 限流，避免 BLE 拥塞

## 编译

本项目使用 PlatformIO 开发，打开目录就能编译上传:

```bash
pio run --target upload
```

## 配置

配置都在 `platformio.ini`，默认已经配置好 Seeed XIAO nRF52840 Sense，直接使用。

## 协议

MIT License
