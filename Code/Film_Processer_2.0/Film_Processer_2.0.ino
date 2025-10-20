#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// 引脚定义
#define EC11_A 6
#define EC11_B 7
#define EC11_BUTTON 8
#define HEATER_4KW 10
#define HEATER_450W 13
#define WATER_PUMP A1
#define TANK_MOTOR_DIR A2  // 高电平正转，低电平反转
#define TANK_MOTOR_RELAY A3
#define LCD_RS 12
#define LCD_EN 11
#define LCD_D4 5
#define LCD_D5 4
#define LCD_D6 3
#define LCD_D7 2
#define LCD_BACKLIGHT 9
#define ONE_WIRE_BUS A0
#define BUZZER A4  // 新增蜂鸣器引脚

// 硬件初始化
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// 数据结构定义
struct ProcessStep {
  const char* name;
  float temp;    // 目标温度(℃)
  int duration;  // 时长(秒)
};

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

ProcessStep e6Steps[] = {
  { "B&W First", 38.0, 390 },  // 6分30秒
  { "Wash", 36.0, 120 },       // 2分钟
  { "Reverse", 36.0, 120 },    // 2分钟
  { "Color Dev", 38.0, 360 },  // 6分钟
  { "Adjust", 36.0, 120 },     // 2分钟
  { "Bleach", 36.0, 360 },     // 6分钟
  { "Fix", 36.0, 240 },        // 4分钟
  { "Wash", 36.0, 240 },       // 4分钟
  { "Stabilize", 36.0, 60 }    // 1分钟
};
const int e6StepCount = sizeof(e6Steps) / sizeof(ProcessStep);

// 自定义工艺参数
#define MAX_CUSTOM_STEPS 10
ProcessStep customSteps[MAX_CUSTOM_STEPS];
int customStepCount = 0;

// 步骤名称列表(新增SAVE AND EXIT选项)
const char* stepNames[] = {
  "Develop", "Reverse", "Adjust", "Bleach",
  "Fix", "Wash", "Stabilize", "Other", "SAVE AND EXIT"
};
const int stepNameCount = sizeof(stepNames) / sizeof(char*);

// 系统状态定义
enum State { HOME,
             SET_CUSTOM,
             RUNNING,
             DONE };
State currentState = HOME;

// 主页菜单
const char* homeOptions[] = { "C41", "E6", "Custom", "Set Custom" };
const int homeOptionCount = sizeof(homeOptions) / sizeof(char*);
int selectedHomeOption = 0;

// 运行时变量
ProcessStep* currentProcess = nullptr;
int currentStepIndex = 0;
int totalSteps = 0;
unsigned long stepStartTime = 0;
int remainingTime = 0;
float currentTemp = 0.0;
float targetTemp = 0.0;
bool temperatureReady = false;   // 温度是否达到可开始状态
bool fastHeatCompleted = false;  // 快速加热到36度是否完成

// 电机控制参数
const unsigned long motorInterval = 5000;  // 正反转间隔(ms)
unsigned long lastMotorDirChange = 0;
bool motorForward = true;

// 编码器变量(优化防抖)
int encoderPos = 0;
int lastEncoderPos = 0;
int lastA, lastB;
bool buttonPressed = false;
bool buttonReleased = false;
unsigned long lastButtonDebounce = 0;
const unsigned long buttonDebounceDelay = 80;  // 延长防抖时间
bool encoderTurned = false;

// 蜂鸣器变量
enum BuzzerEvent { NONE,
                   FAST_HEAT_DONE,
                   STEP_END_WARN,
                   STEP_COMPLETE };
BuzzerEvent currentBuzzerEvent = NONE;
unsigned long buzzerStartTime = 0;
int beepCount = 0;

// 自定义设置变量
int settingStepIndex = 0;  // 当前设置的步骤索引(0-9)
enum SetCustomState { SELECT_NAME,
                      SELECT_TEMP,
                      SELECT_DURATION };
SetCustomState setCustomState = SELECT_NAME;
int selectedStepName = 0;
float tempSetting = 24.0;
int durationSetting = 15;

void setup() {
  // 引脚初始化
  pinMode(EC11_A, INPUT_PULLUP);
  pinMode(EC11_B, INPUT_PULLUP);
  pinMode(EC11_BUTTON, INPUT_PULLUP);
  pinMode(HEATER_4KW, OUTPUT);
  pinMode(HEATER_450W, OUTPUT);
  pinMode(WATER_PUMP, OUTPUT);
  pinMode(TANK_MOTOR_DIR, OUTPUT);
  pinMode(TANK_MOTOR_RELAY, OUTPUT);
  pinMode(LCD_BACKLIGHT, OUTPUT);
  pinMode(BUZZER, OUTPUT);  // 蜂鸣器初始化

  // 初始状态设置(确保电机上电不转)
  digitalWrite(HEATER_4KW, LOW);
  digitalWrite(HEATER_450W, LOW);
  digitalWrite(WATER_PUMP, LOW);
  digitalWrite(TANK_MOTOR_DIR, HIGH);
  digitalWrite(TANK_MOTOR_RELAY, LOW);  // 电机继电器默认关闭
  digitalWrite(LCD_BACKLIGHT, HIGH);
  digitalWrite(BUZZER, LOW);

  // 设备初始化
  lcd.begin(16, 2);
  sensors.begin();
  // 编码器初始状态读取
  lastA = digitalRead(EC11_A);
  lastB = digitalRead(EC11_B);
  loadCustomSteps();

  lcd.clear();
  lcd.print("Film Processor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1000);
  updateHomeDisplay();
}

void loop() {
  handleEncoder();
  handleInput();

  // 温度读取(每秒一次)
  static unsigned long lastTempRead = 0;
  if (millis() - lastTempRead > 1000) {
    sensors.requestTemperatures();
    currentTemp = sensors.getTempCByIndex(0);
    if (currentTemp == -127.0) currentTemp = 0.0;  // 错误处理
    lastTempRead = millis();
  }

  controlTemperature();
  controlMotor();
  updateRunningTime();
  handleBuzzer();  // 处理蜂鸣器事件

  // 显示更新(每秒一次)
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
}

// 编码器处理(优化防抖动和计数)
void handleEncoder() {
  int a = digitalRead(EC11_A);
  int b = digitalRead(EC11_B);
  unsigned long now = millis();

  // 旋转检测(使用状态变化判断，避免多级跳动)
  if (a != lastA || b != lastB) {
    // 仅当A发生变化且B与A状态不同时计数
    if (a != lastA) {
      if (b != a) {
        encoderPos++;  // 顺时针
      } else {
        encoderPos--;  // 逆时针
      }
      encoderTurned = true;
    }
    lastA = a;
    lastB = b;
  }

  // 按键检测(检测释放时触发，增强防抖)
  int buttonState = digitalRead(EC11_BUTTON);
  if (buttonState == LOW) {
    lastButtonDebounce = now;
    buttonPressed = true;
    buttonReleased = false;
  } else {
    if (buttonPressed && (now - lastButtonDebounce > buttonDebounceDelay)) {
      buttonReleased = true;  // 按键释放时才确认有效
      buttonPressed = false;
    }
  }
}

// 输入处理
void handleInput() {
  // 处理编码器旋转(每次有效旋转只处理一次)
  if (encoderTurned && encoderPos != lastEncoderPos) {
    int diff = (encoderPos - lastEncoderPos) > 0 ? 1 : -1;  // 确保每次只跳1级
    lastEncoderPos = encoderPos;
    encoderTurned = false;

    switch (currentState) {
      case HOME:
        selectedHomeOption = (selectedHomeOption + diff + homeOptionCount) % homeOptionCount;
        updateHomeDisplay();
        break;
      case SET_CUSTOM:
        handleSetCustomRotation(diff);
        break;
      default: break;
    }
  }

  // 处理按键(仅在释放时触发)
  if (buttonReleased) {
    buttonReleased = false;
    switch (currentState) {
      case HOME: handleHomeButton(); break;
      case SET_CUSTOM: handleSetCustomButton(); break;
      case RUNNING: handleRunningButton(); break;
      case DONE:
        currentState = HOME;
        updateHomeDisplay();
        break;
    }
  }
}

// 主页按键处理
void handleHomeButton() {
  if (selectedHomeOption == 0) {
    startProcess(c41Steps, c41StepCount);
  } else if (selectedHomeOption == 1) {
    startProcess(e6Steps, e6StepCount);
  } else if (selectedHomeOption == 2) {
    if (customStepCount > 0) {
      startProcess(customSteps, customStepCount);
    } else {
      lcd.clear();
      lcd.print("No custom steps");
      delay(1000);
      updateHomeDisplay();
    }
  } else if (selectedHomeOption == 3) {
    // 进入自定义设置时清空之前的步骤
    currentState = SET_CUSTOM;
    settingStepIndex = 0;
    customStepCount = 0;
    setCustomState = SELECT_NAME;
    selectedStepName = 0;
    tempSetting = 24.0;
    durationSetting = 15;
    updateSetCustomDisplay();
  }
}

// 开始工艺处理
void startProcess(ProcessStep* steps, int count) {
  currentProcess = steps;
  totalSteps = count;
  currentStepIndex = 0;
  targetTemp = currentProcess[0].temp;
  remainingTime = currentProcess[0].duration;
  stepStartTime = 0;  // 初始化为0，等待温度就绪
  currentState = RUNNING;
  lastMotorDirChange = millis();
  motorForward = true;
  temperatureReady = false;
  fastHeatCompleted = false;
  currentBuzzerEvent = NONE;
}

// 自定义设置旋转处理
void handleSetCustomRotation(int diff) {
  switch (setCustomState) {
    case SELECT_NAME:
      selectedStepName = (selectedStepName + diff + stepNameCount) % stepNameCount;
      break;
    case SELECT_TEMP:
      tempSetting += diff * 0.2;
      tempSetting = constrain(tempSetting, 24.0, 41.0);
      break;
    case SELECT_DURATION:
      durationSetting += diff * 5;
      durationSetting = max(durationSetting, 15);
      break;
  }
  updateSetCustomDisplay();
}

// 自定义设置按键处理(优化步骤流程)
void handleSetCustomButton() {
  switch (setCustomState) {
    case SELECT_NAME:
      // 检查是否选择退出
      if (selectedStepName == stepNameCount - 1) {  // 最后一项是SAVE AND EXIT
        saveCustomSteps();
        currentState = HOME;
        updateHomeDisplay();
        return;
      }
      // 否则进入温度设置
      setCustomState = SELECT_TEMP;
      break;
    case SELECT_TEMP:
      // 进入时长设置
      setCustomState = SELECT_DURATION;
      break;
    case SELECT_DURATION:
      // 保存当前步骤
      if (settingStepIndex < MAX_CUSTOM_STEPS) {
        customSteps[settingStepIndex] = {
          stepNames[selectedStepName],
          tempSetting,
          durationSetting
        };
        settingStepIndex++;
        customStepCount = settingStepIndex;  // 更新步骤计数
      }
      // 重置参数，准备下一个步骤设置
      setCustomState = SELECT_NAME;
      selectedStepName = 0;
      tempSetting = 24.0;
      durationSetting = 15;
      // 如果已达最大步骤数，自动保存退出
      if (settingStepIndex >= MAX_CUSTOM_STEPS) {
        saveCustomSteps();
        currentState = HOME;
        updateHomeDisplay();
        return;
      }
      break;
  }
  updateSetCustomDisplay();
}

// 运行中按键处理
void handleRunningButton() {
  // 温度未就绪时按按键无反应
  if (!temperatureReady) return;

  // 步骤结束后按按键进入下一步
  if (remainingTime <= 0) {
    currentStepIndex++;
    if (currentStepIndex < totalSteps) {
      targetTemp = currentProcess[currentStepIndex].temp;
      remainingTime = currentProcess[currentStepIndex].duration;
      stepStartTime = millis();
      lastMotorDirChange = millis();
      temperatureReady = false;  // 新步骤需要重新检查温度
      currentBuzzerEvent = NONE;
    } else {
      currentState = DONE;
      // 关闭所有输出
      digitalWrite(HEATER_4KW, LOW);
      digitalWrite(HEATER_450W, LOW);
      digitalWrite(TANK_MOTOR_RELAY, LOW);
      digitalWrite(WATER_PUMP, LOW);
    }
  }
}

// 温度控制(优化逻辑)
void controlTemperature() {
  if (currentState != RUNNING) {
    digitalWrite(WATER_PUMP, LOW);
    digitalWrite(HEATER_4KW, LOW);
    digitalWrite(HEATER_450W, LOW);
    return;
  }

  digitalWrite(WATER_PUMP, HIGH);  // 运行时水泵常开

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

  // 检测温度是否达到当前步骤要求(±0.2℃)
  if (abs(currentTemp - targetTemp) <= 0.2) {
    if (!temperatureReady) {
      temperatureReady = true;
      if (stepStartTime == 0) {  // 首次就绪，初始化计时
        stepStartTime = millis();
      }
    }
  } else {
    temperatureReady = false;
  }
}

// 电机控制(优化启动逻辑)
void controlMotor() {
  if (currentState != RUNNING || !temperatureReady) {
    digitalWrite(TANK_MOTOR_RELAY, LOW);  // 温度未就绪或非运行状态，电机关闭
    return;
  }

  digitalWrite(TANK_MOTOR_RELAY, HIGH);  // 温度就绪，启动电机

  // 定时切换方向
  if (millis() - lastMotorDirChange > motorInterval) {
    motorForward = !motorForward;
    digitalWrite(TANK_MOTOR_DIR, motorForward ? HIGH : LOW);
    lastMotorDirChange = millis();
  }
}

// 更新运行时间
void updateRunningTime() {
  if (currentState != RUNNING || !temperatureReady || stepStartTime == 0) return;

  unsigned long elapsed = (millis() - stepStartTime) / 1000;
  remainingTime = currentProcess[currentStepIndex].duration - elapsed;
  if (remainingTime < 0) remainingTime = 0;

  // 步骤结束前5秒提醒
  if (remainingTime == 5 && currentBuzzerEvent == NONE) {
    currentBuzzerEvent = STEP_END_WARN;
    buzzerStartTime = millis();
  }

  // 步骤结束提醒
  if (remainingTime == 0 && currentBuzzerEvent != STEP_COMPLETE) {
    currentBuzzerEvent = STEP_COMPLETE;
    buzzerStartTime = millis();
    beepCount = 0;
  }
}

// 蜂鸣器控制
void handleBuzzer() {
  if (currentBuzzerEvent == NONE) {
    digitalWrite(BUZZER, LOW);
    return;
  }

  unsigned long now = millis();

  switch (currentBuzzerEvent) {
    case FAST_HEAT_DONE:
      // 持续1秒
      if (now - buzzerStartTime < 1000) {
        digitalWrite(BUZZER, HIGH);
      } else {
        currentBuzzerEvent = NONE;
      }
      break;

    case STEP_END_WARN:
      // 持续2秒
      if (now - buzzerStartTime < 2000) {
        digitalWrite(BUZZER, HIGH);
      } else {
        currentBuzzerEvent = NONE;
      }
      break;

    case STEP_COMPLETE:
      // 响3次(1秒响，1秒间隔)
      if (beepCount < 3) {
        unsigned long phase = (now - buzzerStartTime) % 2000;
        if (phase < 1000) {
          digitalWrite(BUZZER, HIGH);
        } else {
          digitalWrite(BUZZER, LOW);
          if (phase >= 2000) {
            beepCount++;
            buzzerStartTime = now;
          }
        }
      } else {
        currentBuzzerEvent = NONE;
      }
      break;

    default:
      currentBuzzerEvent = NONE;
      break;
  }
}

// 显示更新
void updateDisplay() {
  switch (currentState) {
    case HOME: updateHomeDisplay(); break;
    case SET_CUSTOM: updateSetCustomDisplay(); break;
    case RUNNING: updateRunningDisplay(); break;
    case DONE: updateDoneDisplay(); break;
  }
}

void updateHomeDisplay() {
  lcd.clear();
  lcd.print(">");
  lcd.print(homeOptions[selectedHomeOption]);
  lcd.setCursor(0, 1);
  lcd.print("Turn to select");
}

void updateSetCustomDisplay() {
  lcd.clear();
  lcd.print("Step ");
  lcd.print(settingStepIndex + 1);  // 显示当前步骤号(1-10)
  lcd.setCursor(0, 1);

  switch (setCustomState) {
    case SELECT_NAME:
      lcd.print("Type:");
      lcd.print(stepNames[selectedStepName]);
      break;
    case SELECT_TEMP:
      lcd.print("Temp:");
      lcd.print(tempSetting, 1);
      lcd.print("C");
      break;
    case SELECT_DURATION:
      lcd.print("Time:");
      lcd.print(durationSetting);
      lcd.print("s");
      break;
  }
}

void updateRunningDisplay() {
  lcd.clear();
  // 温度显示
  lcd.print("T:");
  lcd.print(currentTemp, 1);
  lcd.print("/");
  lcd.print(targetTemp, 1);
  lcd.print("C");

  // 步骤和时间显示
  lcd.setCursor(0, 1);
  lcd.print(currentProcess[currentStepIndex].name);
  lcd.print(" ");

  // 温度未就绪时显示"Wait"
  if (!temperatureReady) {
    lcd.print("Wait");
  } else {
    lcd.print(remainingTime);
    lcd.print("s");
  }
}

void updateDoneDisplay() {
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Complete!");
  lcd.setCursor(0, 1);
  lcd.print("Press to home");
}

// EEPROM操作
void saveCustomSteps() {
  EEPROM.put(0, customStepCount);
  int addr = sizeof(customStepCount);
  for (int i = 0; i < customStepCount; i++) {
    // 保存名称索引
    int nameIdx = 0;
    while (nameIdx < stepNameCount && strcmp(customSteps[i].name, stepNames[nameIdx]) != 0) nameIdx++;
    EEPROM.put(addr, nameIdx);
    addr += sizeof(nameIdx);

    // 保存温度(0.2步进转为整数)
    EEPROM.put(addr, (int)(customSteps[i].temp * 5));
    addr += sizeof(int);

    // 保存时长
    EEPROM.put(addr, customSteps[i].duration);
    addr += sizeof(int);
  }
}

void loadCustomSteps() {
  EEPROM.get(0, customStepCount);
  customStepCount = constrain(customStepCount, 0, MAX_CUSTOM_STEPS);

  int addr = sizeof(customStepCount);
  for (int i = 0; i < customStepCount; i++) {
    int nameIdx, tempInt, duration;
    EEPROM.get(addr, nameIdx);
    addr += sizeof(nameIdx);
    EEPROM.get(addr, tempInt);
    addr += sizeof(tempInt);
    EEPROM.get(addr, duration);
    addr += sizeof(duration);

    customSteps[i] = {
      (nameIdx < stepNameCount) ? stepNames[nameIdx] : "Other",
      constrain(tempInt / 5.0, 24.0, 41.0),
      max(duration, 15)
    };
  }
}