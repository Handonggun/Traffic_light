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

/**
 * 신호등의 5가지 상태를 정의하는 열거형 (enum)
 * 각 상태에서 일정 시간 후 자동으로 다음 상태로 전환됨
 */
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
 * 
 * - 긴급 모드가 활성화되면 빨간불만 켜짐
 * - 깜빡임 모드가 활성화되면 모든 LED가 주기적으로 깜빡임
 * - 기본 상태에서는 신호등 상태 머신에 따라 LED가 제어됨
 */
void renderLEDs() {
  if (emergencyMode) {
    setLED(ledBrightness, 0, 0);  // 빨간불만 켜짐
  } else if (blinkMode) {
    static unsigned long prevTime = millis();
    unsigned long now = millis();
    if (now - prevTime >= 500) {  // 500ms마다 상태 변경
      prevTime = now;
      setLED(ledBrightness, ledBrightness, ledBrightness); // 전체 LED ON
      delay(250); 
      setLED(0, 0, 0); // 전체 LED OFF
    }
  } else {
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
// 🔄 신호등 상태 머신 (교통 신호등 상태 업데이트 함수)
// -------------------------

/**
 * @brief 신호등 상태를 관리하고, 일정 시간이 지나면 상태를 전환하는 함수.
 *
 * - 모드(비상 모드, 깜빡임 모드, 전원 OFF 모드)가 활성화되면 상태 머신을 실행하지 않음.
 * - 특정 상태에서 지정된 시간이 지나면 다음 상태로 자동 전환됨.
 * - 초록불 점멸(GREEN_BLINK) 상태에서는 7번 깜빡이면 다음 상태로 전환됨.
 */
void updateTrafficLight() {
  // 🛑 현재 비상 모드(emergencyMode) 또는 깜빡임 모드(blinkMode) 또는 전원이 꺼진 상태(powerMode == false)라면 상태 업데이트 중지
  if (emergencyMode || blinkMode || !powerMode) return; 

  // ⏰ 현재 시간(ms) 가져오기 (millis()를 사용하여 현재 시간을 기록)
  unsigned long now = millis();

  // 🔄 상태에 따라 동작 수행 (switch-case 사용)
  switch (currentState) {
    
    case RED_ON:  // 🔴 "빨간불 켜짐" 상태
      // 🕒 timeRed(기본 2000ms) 동안 유지되었으면 다음 상태(노란불1)로 변경
      if (now - lastStateChange >= timeRed) {
        lastStateChange = now;  // 상태 변경 시간 기록
        currentState = YELLOW1_ON;  // 다음 상태로 전환
      }
      break;

    case YELLOW1_ON:  // 🟡 "첫 번째 노란불" 상태
      // 🕒 timeYellow(기본 500ms) 동안 유지되었으면 다음 상태(초록불)로 변경
      if (now - lastStateChange >= timeYellow) {
        lastStateChange = now;  // 상태 변경 시간 기록
        currentState = GREEN_ON;  // 다음 상태로 전환
      }
      break;

    case GREEN_ON:  // 🟢 "초록불 켜짐" 상태
      // 🕒 timeGreen(기본 2000ms) 동안 유지되었으면 다음 상태(초록불 깜빡임)으로 변경
      if (now - lastStateChange >= timeGreen) {
        lastStateChange = now;  // 상태 변경 시간 기록
        currentState = GREEN_BLINK;  // 초록불 깜빡임 모드로 전환
        blinkCounter = 0;  // 깜빡임 횟수 초기화
      }
      break;

    case GREEN_BLINK:  // 🟢 "초록불 깜빡임" 상태
      // 🔄 일정 간격(timeBlink)마다 초록불을 ON/OFF 전환
      if (now - lastStateChange >= timeBlink) {
        lastStateChange = now;  // 상태 변경 시간 기록
        blinkCounter++;  // 깜빡임 횟수 증가

        // 🟢 초록불 ON/OFF 전환 (짝수 번째 → ON, 홀수 번째 → OFF)
        if (blinkCounter % 2 == 0) {
          setLED(0, 0, ledBrightness);  // 초록불 ON
        } else {
          setLED(0, 0, 0);  // 초록불 OFF
        }

        // ⏳ 7번 깜빡이면 다음 상태(노란불2)로 전환
        if (blinkCounter >= 7) {
          currentState = YELLOW2_ON;  // 다음 상태로 전환
          lastStateChange = now;  // 상태 변경 시간 기록
        }
      }
      break;

    case YELLOW2_ON:  // 🟡 "두 번째 노란불" 상태
      // 🕒 timeYellow(기본 500ms) 동안 유지되었으면 다시 "빨간불" 상태로 복귀
      if (now - lastStateChange >= timeYellow) {
        lastStateChange = now;  // 상태 변경 시간 기록
        currentState = RED_ON;  // 다시 처음(빨간불) 상태로 복귀
      }
      break;
  }
}

// 🔄 TaskScheduler에 신호등 상태 업데이트 함수 등록 (10ms마다 실행)
Task taskUpdateTraffic(STATE_UPDATE_INTERVAL, TASK_FOREVER, []() { updateTrafficLight(); });


// TaskScheduler에 등록
Task taskUpdateTraffic(STATE_UPDATE_INTERVAL, TASK_FOREVER, []() { updateTrafficLight(); });

// -------------------------
// 🔄 버튼 인터럽트 핸들러
// -------------------------

void handleButton1() { emergencyMode = !emergencyMode; blinkMode = false; powerMode = true; }
void handleButton2() { blinkMode = !blinkMode; emergencyMode = false; powerMode = true; }
void handleButton3() { powerMode = !powerMode; emergencyMode = false; blinkMode = false; }

// -------------------------
// 🛠️ 초기 설정 (Arduino의 기본 설정 수행)
// -------------------------

/**
 * @brief 초기 환경을 설정하는 함수로, 아두이노가 실행될 때 한 번만 실행됨.
 *
 * - 시리얼 통신(9600bps) 시작
 * - LED 핀을 출력 모드로 설정
 * - 버튼 핀을 입력 모드로 설정 (내부 풀업 저항 활성화)
 * - 핀 체인지 인터럽트(PCINT) 설정하여 버튼 입력 감지
 * - TaskScheduler를 초기화하고, 신호등 상태 업데이트 태스크를 등록 및 활성화
 * - 신호등 상태 변경을 위한 초기 타이머 값 설정 (millis() 활용)
 */

void setup() {
  // 🖥️ 시리얼 통신 초기화 (속도: 9600bps)
  Serial.begin(9600);

  // 💡 LED 핀을 출력 모드(OUTPUT)로 설정 (PWM 출력 가능)
  pinMode(LED_RED, OUTPUT);    // 🔴 빨간 LED
  pinMode(LED_YELLOW, OUTPUT); // 🟡 노란 LED
  pinMode(LED_GREEN, OUTPUT);  // 🟢 초록 LED

  // 🔘 버튼 핀을 입력 모드(INPUT_PULLUP)로 설정 (내부 풀업 저항 사용)
  // ⚠️ 버튼이 눌리면 LOW 신호가 입력됨 (기본 상태는 HIGH)
  pinMode(BUTTON1, INPUT_PULLUP);  // 버튼 1
  pinMode(BUTTON2, INPUT_PULLUP);  // 버튼 2
  pinMode(BUTTON3, INPUT_PULLUP);  // 버튼 3

  // 🎛️ 핀 체인지 인터럽트(PCINT) 등록 (FALLING 엣지에서 트리거)
  // 버튼이 눌릴 때(LOW로 변화할 때) 인터럽트 발생
  attachPCINT(digitalPinToPCINT(BUTTON1), handleButton1, FALLING);
  attachPCINT(digitalPinToPCINT(BUTTON2), handleButton2, FALLING);
  attachPCINT(digitalPinToPCINT(BUTTON3), handleButton3, FALLING);

  // ⏳ TaskScheduler(태스크 매니저) 초기화
  taskManager.init();

  // 🚦 신호등 상태 업데이트 태스크를 TaskScheduler에 추가
  taskManager.addTask(taskUpdateTraffic);
  taskUpdateTraffic.enable();  // 태스크 활성화

  // 🕒 상태 변경 시간 초기화 (현재 millis() 값 저장)
  lastStateChange = millis();
}

// -------------------------
// 🔁 메인 루프 (Arduino가 계속 실행하는 반복 함수)
// -------------------------

/**
 * @brief 아두이노의 메인 루프 함수로, 실행이 끝나면 다시 처음부터 반복 실행됨.
 *
 * - 가변저항(POTENTIOMETER) 값을 읽어 LED 밝기 조절
 * - 현재 상태에 따라 LED를 적절히 렌더링(renderLEDs() 호출)
 * - TaskScheduler를 실행하여 등록된 태스크 실행 (신호등 상태 업데이트)
 */
void loop() {
  // 🎚️ 가변저항(POTENTIOMETER) 값을 읽어 현재 밝기 값으로 변환 (0~1023 → 0~255)
  int potValue = analogRead(POTENTIOMETER);   // 아날로그 값(0~1023) 읽기
  ledBrightness = map(potValue, 0, 1023, 0, 255);  // 0~1023 범위를 0~255로 변환 (PWM 값)

  // 💡 현재 상태에 따라 LED를 적절히 렌더링
  renderLEDs();

  // 🔄 TaskScheduler 실행 (등록된 태스크 실행)
  taskManager.execute();
}
