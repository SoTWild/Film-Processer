# 胶片冲洗机自动化控制方案

###### 【Designed by Dioxgen】[![](https://img.shields.io/badge/My%20Website-Quillset.com-brightgreen.svg)](https://Quillset.com)![](https://img.shields.io/badge/Licence-GNU-blue)![](https://img.shields.io/badge/Version-2.0-red)![](https://img.shields.io/badge/Platform-Github-success)![](https://img.shields.io/badge/Language-C%2FC%2B%2B-blueviolet)

## 概述

这是一款基于 `Atmega328p` 设计的**胶片滚冲机**自动化控制**总成**。

<img src=".\Images\成品.jpg" alt="成品" style="zoom:33%;" />

------

## 硬件

### 主要组件：

- Arduino 开发板
- LCD1602 显示屏
- EC11 旋转编码器
- DS18B20 温度传感器
- 4KW 加热棒 + 450W 加热棒
- 直流电机 + 继电器
- 水泵
- 蜂鸣器
- G30F-2ZLD-A 12V 30A 大功率继电器
- LY2NJ（HH62P）12V 10A 大功率继电器

### 引脚定义：

| 组件       | 引脚 | 说明          |
| :--------- | :--- | :------------ |
| LCD_RS     | 12   | LCD寄存器选择 |
| LCD_EN     | 11   | LCD使能       |
| LCD_D4-D7  | 2-5  | LCD数据线     |
| LCD背光    | 9    | LCD背光控制   |
| EC11_A     | 6    | 编码器A相     |
| EC11_B     | 7    | 编码器B相     |
| EC11按钮   | 8    | 编码器按键    |
| 4KW加热棒  | 10   | 大功率加热    |
| 450W加热棒 | 13   | 小功率加热    |
| 水泵       | A1   | 水循环泵      |
| 电机方向   | A2   | 电机转向控制  |
| 电机继电器 | A3   | 电机电源控制  |
| 温度传感器 | A0   | DS18B20数据线 |
| 蜂鸣器     | A4   | 声音提示      |

### Schematic：

<img src=".\Images\SCH_Schematic1_1-P1_2025-10-21.png" alt="SCH_Schematic1_1-P1_2025-10-21" style="zoom:100%;" />

### PCB：

<img src=".\Images\PCB.png" alt="PCB" style="zoom:50%;" />

------

## 软件

### 1）功能特性：

- **预设工艺**：支持 `C41` 和 `E6` 标准冲洗工艺
- **自定义工艺**：可创建和保存最多 10 个步骤的自定义工艺
- **精确温控**：双加热棒系统实现快速升温和精确控温（`±0.2℃`）
- **自动搅拌**：电机定时正反转确保药液均匀
- **用户界面**：`LCD` 显示屏 + 旋转编码器操作
- **声音提示**：蜂鸣器提供状态提醒
- **数据持久化**：`EEPROM` 存储自定义工艺参数

### 2）温度控制逻辑：

#### 加热阶段：

- <35℃：4KW + 450W 同时加热
- 35-36℃：仅 450W 加热
- ≥36℃：进入精确控温模式

#### 精确控温：

- 目标温度 ±0.2℃ 范围内控制
- 仅使用 450W 加热棒维持温度

#### 安全特性：

- 非运行状态自动关闭所有加热
- 温度传感器错误处理

#### 搅拌控制：

- 温度就绪后自动启动电机
- 每 5 秒切换一次转向，确保药液温度均匀分布

### 3）数据存储：

自定义工艺参数自动保存到 EEPROM：

- 步骤数量
- 每个步骤的类型、温度、时长
- 断电后数据不丢失

### 4）使用说明：

#### 基本操作：

1. 上电启动：系统初始化后进入主菜单
2. 旋转选择：转动编码器选择不同选项
3. 按下确认：按下编码器确认选择

#### 选择预设工艺：

1. 在主菜单选择"C41"或"E6"
2. 按下编码器开始工艺
3. 系统自动控制温度和步骤切换

#### 使用自定义工艺：

1. 在主菜单选择"Custom"
2. 如果已保存自定义工艺，直接开始执行
3. 如无自定义工艺，提示"No custom steps"

#### 设置自定义工艺：

1. 在主菜单选择"Set Custom"
2. 按顺序设置每个步骤的参数：
   - 步骤类型：选择工艺类型
   - 温度：24.0~41.0℃（步进 0.2℃）
   - 时长：≥15 秒（步进 5 秒）
3. 最多可设置10个步骤
4. 选择"SAVE AND EXIT"保存并退出

#### 工艺执行界面：

显示屏显示：

- 上行：当前温度 / 目标温度
- 下行：步骤名称 + 剩余时间或"Wait"

状态指示：

- Wait：温度未达到要求
- 数字+秒：步骤剩余时间

#### 声音提示：

- 快速加热完成：持续 1 秒蜂鸣
- 步骤结束前 5 秒：持续 2 秒蜂鸣
- 步骤完成：3 次蜂鸣（1 秒响，1 秒间隔）

### 5）代码示例：

状态机：

```c
// 系统状态定义
enum State { HOME,
             SET_CUSTOM,
             RUNNING,
             DONE };
```
工艺结构：
```c
// 预设工艺参数
ProcessStep c41Steps[] = {
  { "Color Dev", 37.8, 195 },  // 3分15秒
  { "Bleach", 37.0, 390 },     // 6分30秒
  { "Wash", 37.0, 195 },       // 3分15秒
  { "Fix", 37.0, 390 },        // 6分30秒
  { "Wash", 37.0, 195 },       // 3分15秒
  { "Stabilize", 37.0, 90 }    // 1分30秒
};
const int c41StepCount = sizeof(c41Steps) / sizeof(ProcessStep);
```
温控逻辑：
```c
// 快速加热阶段(36度以下)
if (currentTemp < 36.0) {
  if (currentTemp < 35.0) {
    // 35度以下：双加热棒同时工作
    digitalWrite(HEATER_4KW, HIGH);
    digitalWrite(HEATER_450W, HIGH);
  } else {
    // 35-36度：关闭4KW，保留450W
    digitalWrite(HEATER_4KW, LOW);
    digitalWrite(HEATER_450W, HIGH);
  }
  // 检测快速加热完成
  if (currentTemp >= 36.0 && !fastHeatCompleted) {
    currentBuzzerEvent = FAST_HEAT_DONE;
    buzzerStartTime = millis();
    fastHeatCompleted = true;
  }
}
// 目标温度控制阶段
else {
  digitalWrite(HEATER_4KW, LOW);  // 4KW关闭
  // 精确控温(±0.2℃)
  if (currentTemp < targetTemp - 0.2) {
    digitalWrite(HEATER_450W, HIGH);
  } else if (currentTemp > targetTemp + 0.2) {
    digitalWrite(HEATER_450W, LOW);
  }
}
```

------

## 注意事项：

### 硬件：

#### 安全注意事项：

- 确保所有电气连接防水
- 加热时请勿触摸加热棒
- 定期检查温度传感器精度
- 接线与器件符合功率要求

> 以下是一些常见 AWG 规格及其对应的电流承载能力：
>
> - 10 AWG: 30A
> - 12 AWG: 20A
> - 14 AWG: 15A
> - 16 AWG: 10A
> - 18 AWG: 7A
> - 20 AWG: 5A
> - 22 AWG: 3A

#### 连线：

注意连线正负极，两个大功率继电器线圈与主板端子的连接。

焊接时 `RELAY1` 与 `RELAY2` 多余引脚剪去即可。

------

## 其他

### 许可证：

本项目基于 Arduino 开发，遵循相关开源协议。

本项目代码基于 AI 生成，已经过实际验证。
