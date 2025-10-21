#include <LiquidCrystal.h>
#include <OneWire.h>
#include <DallasTemperature.h>
//#include <EEPROM.h>

// 引脚定义
#define EC11_A 6
#define EC11_B 7
#define EC11_BTN 8
#define HEATER_4KW 10
#define HEATER_450W 13
#define DS18B20_PIN A0
#define PUMP_PIN A1
#define MOTOR_DIR A2
#define MOTOR_POWER A3
#define LCD_BKL 9

// LCD引脚配置
/* LCD RS pin to digital pin 12
 * LCD Enable pin to digital pin 11
 * LCD D4 pin to digital pin 5
 * LCD D5 pin to digital pin 4
 * LCD D6 pin to digital pin 3
 * LCD D7 pin to digital pin 2
 * LCD R/W pin to ground
 * LCD VSS pin to ground
 * LCD VDD pin to 5V
 * LCD A pin to D9
 * LCD K pin to ground
 */
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// 温度传感器
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

// 全局变量
int encoderPos = 0;
int lastEncoded = 0;
bool lastBtnState = true;
unsigned long lastTempRead = 0;
unsigned long lastMotorSwitch = 0;
unsigned long stepStartTime = 0;
unsigned long stepRemainingTime = 0;

// 电机控制
bool motorDirection = true;                  // true=正转, false=反转
const unsigned long MOTOR_INTERVAL = 10000;  // 10秒切换方向

// 温度控制
float currentTemp = 0;
float targetTemp = 0;
bool tempControlEnabled = false;
const float TEMP_HYSTERESIS = 0.1;  // 温度控制死区

// 程序状态
enum ProgramState {
  STATE_HOME,
  STATE_SELECT_PROGRAM,
  STATE_RUNNING,
  STATE_STEP_COMPLETE,
  STATE_PROGRAM_COMPLETE
};
ProgramState currentState = STATE_HOME;

// 当前程序信息
int currentProgram = 0;
int currentStep = 0;
bool programRunning = false;

// 程序预设结构体
struct ProcessStep {
  const char* name;
  float temp;
  unsigned long time;  // 秒
};

// C41工艺步骤
ProcessStep c41Steps[] = {
  { "Color Dev", 37.8, 195 },  // 3分15秒
  { "Bleach", 37.0, 390 },     // 6分30秒
  { "Wash", 37.0, 195 },       // 3分15秒
  { "Fix", 37.0, 390 },        // 6分30秒
  { "Wash", 37.0, 195 },       // 3分15秒
  { "Stabilize", 37.0, 90 }    // 1分30秒
};

// E6工艺步骤
ProcessStep e6Steps[] = {
  { "1st Dev", 38.0, 390 },    // 6分30秒
  { "Wash", 36.0, 120 },       // 2分钟
  { "Reversal", 36.0, 120 },   // 2分钟
  { "Color Dev", 38.0, 360 },  // 6分钟
  { "Adjust", 36.0, 120 },     // 2分钟
  { "Bleach", 36.0, 360 },     // 6分钟
  { "Fix", 36.0, 240 },        // 4分钟
  { "Wash", 36.0, 240 },       // 4分钟
  { "Stabilize", 36.0, 60 }    // 1分钟
};

const int c41StepCount = sizeof(c41Steps) / sizeof(c41Steps[0]);
const int e6StepCount = sizeof(e6Steps) / sizeof(e6Steps[0]);

void setup() {
  // 初始化引脚
  pinMode(EC11_A, INPUT_PULLUP);
  pinMode(EC11_B, INPUT_PULLUP);
  pinMode(EC11_BTN, INPUT_PULLUP);
  pinMode(HEATER_4KW, OUTPUT);
  pinMode(HEATER_450W, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MOTOR_DIR, OUTPUT);
  pinMode(MOTOR_POWER, OUTPUT);
  pinMode(LCD_BKL, OUTPUT);

  // 初始化设备
  digitalWrite(HEATER_4KW, LOW);
  digitalWrite(HEATER_450W, LOW);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(MOTOR_POWER, LOW);

  // 初始化LCD
  digitalWrite(LCD_BKL, HIGH);
  lcd.begin(16, 2);

  // 初始化温度传感器
  sensors.begin();

  // 读取编码器初始状态
  lastEncoded = (digitalRead(EC11_A) << 1) | digitalRead(EC11_B);

  Serial.begin(9600);
}

void loop() {
  unsigned long currentTime = millis();

  // 读取温度（每1秒）
  if (currentTime - lastTempRead >= 1000) {
    readTemperature();
    lastTempRead = currentTime;
  }

  // 处理编码器
  readEncoder();

  // 处理按钮
  handleButton();

  // 状态机
  switch (currentState) {
    case STATE_HOME:
      displayHomeScreen();
      break;
    case STATE_SELECT_PROGRAM:
      displayProgramSelection();
      break;
    case STATE_RUNNING:
      runProcessStep(currentTime);
      displayRunningScreen();
      break;
    case STATE_STEP_COMPLETE:
      displayStepCompleteScreen();
      break;
    case STATE_PROGRAM_COMPLETE:
      displayProgramCompleteScreen();
      break;
  }

  // 温度控制
  if (tempControlEnabled) {
    controlTemperature();
  }

  // 电机控制
  if (programRunning && currentState == STATE_RUNNING) {
    controlMotor(currentTime);
  }
}

void readTemperature() {
  sensors.requestTemperatures();
  currentTemp = sensors.getTempCByIndex(0);

  // 温度传感器错误检查
  if (currentTemp == DEVICE_DISCONNECTED_C) {
    currentTemp = -127;
  }
}

void readEncoder() {
  int MSB = digitalRead(EC11_A);
  int LSB = digitalRead(EC11_B);

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
    encoderPos++;
  }
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
    encoderPos--;
  }

  lastEncoded = encoded;
}

void handleButton() {
  bool currentBtnState = digitalRead(EC11_BTN);

  if (!currentBtnState && lastBtnState) {  // 按钮按下
    delay(50);                             // 防抖
    if (!digitalRead(EC11_BTN)) {          // 确认按下
      switch (currentState) {
        case STATE_HOME:
          currentState = STATE_SELECT_PROGRAM;
          encoderPos = 0;
          lcd.clear();
          break;
        case STATE_SELECT_PROGRAM:
          startSelectedProgram();
          break;
        case STATE_RUNNING:
          // 在运行状态下，可以添加暂停功能
          // 暂时不处理按钮按下
          break;
        case STATE_STEP_COMPLETE:
          startNextStep();
          break;
        case STATE_PROGRAM_COMPLETE:
          resetToHome();
          break;
        default:
          // 处理所有未明确列出的状态
          break;
      }
    }
  }

  lastBtnState = currentBtnState;
}

void displayHomeScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Film Processor   ");
  lcd.setCursor(0, 1);
  lcd.print("Press to start   ");
}

void displayProgramSelection() {
  lcd.setCursor(0, 0);
  lcd.print("Select Program:  ");
  lcd.setCursor(0, 1);

  int selected = encoderPos % 2;
  if (selected < 0) selected += 2;

  if (selected == 0) {
    lcd.print(">C41    E6       ");
    currentProgram = 0;
  } else {
    lcd.print(" C41   >E6       ");
    currentProgram = 1;
  }
}

void startSelectedProgram() {
  currentStep = 0;
  programRunning = true;
  currentState = STATE_RUNNING;
  stepStartTime = millis();

  // 设置第一步的目标温度
  if (currentProgram == 0) {  // C41
    targetTemp = c41Steps[0].temp;
    stepRemainingTime = c41Steps[0].time;
  } else {  // E6
    targetTemp = e6Steps[0].temp;
    stepRemainingTime = e6Steps[0].time;
  }

  tempControlEnabled = true;
  digitalWrite(PUMP_PIN, HIGH);  // 开启水泵
  lcd.clear();
}

void runProcessStep(unsigned long currentTime) {
  if (currentProgram == 0) {  // C41
    if (currentStep >= c41StepCount) {
      programComplete();
      return;
    }
    stepRemainingTime = c41Steps[currentStep].time - (currentTime - stepStartTime) / 1000;
    targetTemp = c41Steps[currentStep].temp;

    if (stepRemainingTime <= 0) {
      stepComplete();
    }
  } else {  // E6
    if (currentStep >= e6StepCount) {
      programComplete();
      return;
    }
    stepRemainingTime = e6Steps[currentStep].time - (currentTime - stepStartTime) / 1000;
    targetTemp = e6Steps[currentStep].temp;

    if (stepRemainingTime <= 0) {
      stepComplete();
    }
  }
}

void displayRunningScreen() {
  // 第一行：温度信息
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(currentTemp, 1);
  lcd.print("C Tgt:");
  lcd.print(targetTemp, 1);
  lcd.print("C");

  // 第二行：时间信息和进度条
  lcd.setCursor(0, 1);

  // 显示当前步骤名称
  const char* stepName;
  if (currentProgram == 0) {
    stepName = c41Steps[currentStep].name;
  } else {
    stepName = e6Steps[currentStep].name;
  }

  // 由于LCD只有16列，我们需要合理布局
  // 显示步骤名称（最多6字符）和时间
  lcd.print(stepName);

  // 确保显示格式整齐
  int nameLength = strlen(stepName);
  if (nameLength < 6) {
    for (int i = nameLength; i < 6; i++) {
      lcd.print(" ");
    }
  }

  lcd.print(" ");

  // 显示剩余时间
  int minutes = stepRemainingTime / 60;
  int seconds = stepRemainingTime % 60;
  if (minutes < 10) lcd.print("0");
  lcd.print(minutes);
  lcd.print(":");
  if (seconds < 10) lcd.print("0");
  lcd.print(seconds);

  // 简单的进度条（基于步骤）
  int totalSteps = (currentProgram == 0) ? c41StepCount : e6StepCount;
  int progressBars = map(currentStep + 1, 0, totalSteps, 0, 6);  // 最多显示6个#

  // 在行尾显示进度条
  lcd.setCursor(15 - progressBars, 1);
  for (int i = 0; i < progressBars; i++) {
    lcd.print("#");
  }
}

void controlTemperature() {
  float tempDiff = targetTemp - currentTemp;

  // 4kW加热棒：快速加热，当温度低于目标温度2度时启动
  if (tempDiff > 2.0) {
    digitalWrite(HEATER_4KW, HIGH);
    digitalWrite(HEATER_450W, LOW);
  }
  // 450W加热棒：精确控温
  else if (tempDiff > TEMP_HYSTERESIS) {
    digitalWrite(HEATER_4KW, LOW);
    digitalWrite(HEATER_450W, HIGH);
  }
  // 温度达到目标，关闭加热
  else if (tempDiff < -TEMP_HYSTERESIS) {
    digitalWrite(HEATER_4KW, LOW);
    digitalWrite(HEATER_450W, LOW);
  }
}

void controlMotor(unsigned long currentTime) {
  if (currentTime - lastMotorSwitch >= MOTOR_INTERVAL) {
    motorDirection = !motorDirection;
    digitalWrite(MOTOR_DIR, motorDirection ? HIGH : LOW);
    lastMotorSwitch = currentTime;
  }
  digitalWrite(MOTOR_POWER, HIGH);  // 保持电机供电
}

void stepComplete() {
  currentState = STATE_STEP_COMPLETE;
  digitalWrite(MOTOR_POWER, LOW);  // 停止电机
  lcd.clear();
}

void displayStepCompleteScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Step Complete!   ");
  lcd.setCursor(0, 1);
  lcd.print("Press to continue");
}

void startNextStep() {
  currentStep++;
  currentState = STATE_RUNNING;
  stepStartTime = millis();
  lcd.clear();
}

void programComplete() {
  currentState = STATE_PROGRAM_COMPLETE;
  programRunning = false;
  tempControlEnabled = false;
  digitalWrite(HEATER_4KW, LOW);
  digitalWrite(HEATER_450W, LOW);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(MOTOR_POWER, LOW);
  lcd.clear();
}

void displayProgramCompleteScreen() {
  lcd.setCursor(0, 0);
  lcd.print("Program Complete!");
  lcd.setCursor(0, 1);
  lcd.print("Press for home   ");
}

void resetToHome() {
  currentState = STATE_HOME;
  encoderPos = 0;
  lcd.clear();
}