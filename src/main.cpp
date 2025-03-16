#include <Arduino.h>            // 아두이노 기본 라이브러리 포함
#include <TaskScheduler.h>       // 비동기적인 태스크 관리 라이브러리
#include <PinChangeInterrupt.h>  // 핀 체인지 인터럽트를 위한 라이브러리

// -------------------------
// 🛠️ 핀 설정 (LED 및 버튼)
// -------------------------

#define LED_RED    11   // 빨간 LED (PWM 지원 핀)
#define LED_YELLOW 10   // 노란 LED (PWM 지원 핀)
#define LED_GREEN   9   // 초록 LED (PWM 지원 핀)

#define BUTTON1  4   // 모드 변경 버튼 1 (긴급 모드)
#define BUTTON2  3   // 모드 변경 버튼 2 (모든 LED 깜빡임)
#define BUTTON3  2   // 모드 변경 버튼 3 (전원 ON/OFF 기능)

#define POTENTIOMETER A5  // LED 밝기 조절용 가변저항 (아날로그 입력)

// -------------------------
// 🕒 실행 주기 설정
// -------------------------

#define SERIAL_READ_INTERVAL   500   // 시리얼 입력 주기 (500ms마다 읽음)
#define SERIAL_WRITE_INTERVAL  100   // 시리얼 출력 주기 (100ms마다 데이터 전송)
#define STATE_UPDATE_INTERVAL  10    // 신호등 상태 업데이트 주기 (10ms 간격)

// TaskScheduler 객체 생성 (태스크 실행을 관리함)
Scheduler taskManager;

// -------------------------
// 🚦 신호등 상태 정의 (Traffic Light States)
// -------------------------

// 신호등의 5가지 상태를 정의하는 열거형 (enum)
enum TrafficState {
  RED_ON,            // 빨간불 켜짐
  YELLOW1_ON,        // 노란불 첫 번째 점등 (빨간 → 초록 전환)
  GREEN_ON,          // 초록불 점등
  GREEN_BLINK,       // 초록불 깜빡임 (점멸 후 노란불로 전환)
  YELLOW2_ON         // 노란불 두 번째 점등 (초록 → 빨간 전환)
};

// 현재 신호등 상태를 저장하는 변수 (초기 상태: RED_ON)
volatile TrafficState currentState = RED_ON;

// -------------------------
// ⏳ 각 상태 지속 시간 (밀리초 단위)
// -------------------------

unsigned int timeRed    = 2000;  // 빨간불 지속 시간: 2초
unsigned int timeYellow = 500;   // 노란불 지속 시간: 0.5초
unsigned int timeGreen  = 2000;  // 초록불 지속 시간: 2초
unsigned int timeBlink  = 1000 / 7; // 초록불 깜빡임 주기 (7번 점멸)

// 마지막 상태 변경 시간을 기록하는 변수 (millis() 기반)
unsigned long lastStateChange = 0;
int blinkCounter = 0; // 초록불 점멸 횟수 카운트

// -------------------------
// 🏴 모드 플래그 (특정 모드 활성화 여부)
// -------------------------

volatile bool emergencyMode = false;  // 긴급 모드 (빨간불 고정)
volatile bool blinkMode = false;      // 모든 LED 깜빡이는 모드
volatile bool powerMode = true;       // 기본 전원 상태 (ON)

// -------------------------
// 💡 LED 밝기 설정 (PWM 값)
// -------------------------

volatile int ledBrightness = 255; // 기본 밝기 (최대 255)

// -------------------------
// 🛠️ LED 제어 함수 (PWM 조절)
// -------------------------

/**
 * @brief 특정 밝기로 LED를 조절하는 함수
 * 
 * @param red    빨간불 밝기 (0~255)
 * @param yellow 노란불 밝기 (0~255)
 * @param green  초록불 밝기 (0~255)
 */
void setLED(int red, int yellow, int green) {
  analogWrite(LED_RED, red);
  analogWrite(LED_YELLOW, yellow);
  analogWrite(LED_GREEN, green);
}

// -------------------------
// 🎨 LED 렌더링 함수
// -------------------------

/**
 * @brief 현재 모드 및 상태에 따라 LED를 조절하는 함수
 */
void renderLEDs() {
  if (emergencyMode) {
    // 긴급 모드 활성화: 빨간불만 켜짐
    setLED(ledBrightness, 0, 0);
  } else if (blinkMode) {
    // 모든 LED가 깜빡이는 모드
    static unsigned long prevTime = millis();
    unsigned long now = millis();
    if (now - prevTime >= 500) {  // 500ms마다 상태 변경
      prevTime = now;
      setLED(ledBrightness, ledBrightness, ledBrightness);
      delay(250); 
      setLED(0, 0, 0);
    }
  } else {
    // 기본 신호등 동작
    switch (currentState) {
      case RED_ON:
        setLED(ledBrightness, 0, 0);
        break;
      case YELLOW1_ON:
      case YELLOW2_ON:
        setLED(0, ledBrightness, 0);
        break;
      case GREEN_ON:
        setLED(0, 0, ledBrightness);
        break;
      case GREEN_BLINK:
        setLED(0, 0, (blinkCounter % 2 == 0) ? ledBrightness : 0);
        break;
    }
  }
}

// -------------------------
// 🔄 신호등 상태 머신 (State Machine)
// -------------------------

/**
 * @brief 신호등 상태를 업데이트하는 함수
 */
void updateTrafficLight() {
  if (emergencyMode || blinkMode || !powerMode) return; // 모드 활성화 시 기본 동작 중지

  unsigned long now = millis();

  switch (currentState) {
    case RED_ON:
      if (now - lastStateChange >= timeRed) {
        lastStateChange = now;
        currentState = YELLOW1_ON;
      }
      break;

    case YELLOW1_ON:
      if (now - lastStateChange >= timeYellow) {
        lastStateChange = now;
        currentState = GREEN_ON;
      }
      break;

    case GREEN_ON:
      if (now - lastStateChange >= timeGreen) {
        lastStateChange = now;
        currentState = GREEN_BLINK;
        blinkCounter = 0;
      }
      break;

    case GREEN_BLINK:
      if (now - lastStateChange >= timeBlink) {
        lastStateChange = now;
        blinkCounter++;
        if (blinkCounter >= 7) {
          currentState = YELLOW2_ON;
          lastStateChange = now;
        }
      }
      break;

    case YELLOW2_ON:
      if (now - lastStateChange >= timeYellow) {
        lastStateChange = now;
        currentState = RED_ON;
      }
      break;
  }
}

// TaskScheduler에 등록
Task taskUpdateTraffic(STATE_UPDATE_INTERVAL, TASK_FOREVER, []() { updateTrafficLight(); });

// -------------------------
// 🔄 버튼 인터럽트 핸들러
// -------------------------

void handleButton1() { emergencyMode = !emergencyMode; blinkMode = false; powerMode = true; }
void handleButton2() { blinkMode = !blinkMode; emergencyMode = false; powerMode = true; }
void handleButton3() { powerMode = !powerMode; emergencyMode = false; blinkMode = false; }

// -------------------------
// 🛠️ 초기 설정
// -------------------------

void setup() {
  Serial.begin(9600);
  
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(BUTTON3, INPUT_PULLUP);

  attachPCINT(digitalPinToPCINT(BUTTON1), handleButton1, FALLING);
  attachPCINT(digitalPinToPCINT(BUTTON2), handleButton2, FALLING);
  attachPCINT(digitalPinToPCINT(BUTTON3), handleButton3, FALLING);

  taskManager.init();
  taskManager.addTask(taskUpdateTraffic);
  taskUpdateTraffic.enable();

  lastStateChange = millis();
}

// -------------------------
// 🔁 메인 루프
// -------------------------

void loop() {
  int potValue = analogRead(POTENTIOMETER);
  ledBrightness = map(potValue, 0, 1023, 0, 255);

  renderLEDs();
  taskManager.execute();
}
