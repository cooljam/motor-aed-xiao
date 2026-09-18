# motor-aed-xiao

> 航模电机震动识别 - Seeed XIAO nRF52840 Sense 固件
>
> 本项目是 [motor-aed-app](https://github.com/xxx/motor-aed-app) 的开源主板端固件，手机小程序端闭源放在 motor-aed-app 仓库。

## 功能

- 上电后**持续连续采集**板载 LIS3DH 三轴加速度数据
- 打包后通过 BLE 持续通知发送给手机
- **手机端点击采集，手机自行记录固定长度**，不需要给主板发命令
- 支持串口调试输出

## 数据传输协议

### BLE GATT 定义

|  | UUID | 属性 | 方向 |
|------|------|------|------|
| Service | `0000FFB0-0000-1000-8000-00805F9B34FB` | 服务 | XIAO → 手机 |
| Data | `0000FFB1-0000-1000-8000-00805F9B34FB` | Notify | XIAO → 手机 |

### 数据包格式

每个 BLE 数据包格式:

| 偏移 | 长度 | 内容 |
|------|------|------|
| 0 | 1 byte | 包序号 SEQ (0-255循环)，手机用于检测丢包 |
| 1 | N × 6 bytes | 采样数据 |

**每个采样点格式**:
```
[X 2-byte 小端] [Y 2-byte 小端] [Z 2-byte 小端]
```
- 每个轴是 16-bit 补码，对应 LIS3DH 输出
- 每个采样点共 6 字节

### 数据包大小
- BLE 数据包最大 `BLE_PACKET_SIZE = 256` 字节
- 每个数据包放 `(256 - 1) / 6 = 42` 个采样点
- SEQ 序号方便手机检测丢包

## 编译

本项目使用 PlatformIO 开发，打开目录就能编译上传:

```bash
pio run --target upload
```

## 配置

配置都在 `platformio.ini`，默认已经配置好 XIAO nRF52840 Sense，直接使用。

## 协议

MIT License
