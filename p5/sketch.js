// 전역 변수 선언

// 시리얼 포트를 저장할 변수
let port;

// 시리얼 포트 연결 상태 플래그 (연결되면 true)
let portConnected = false;

// 수신한 데이터의 누적 문자열
let latestData = "";

// 파싱된 데이터 저장 변수
let brightnessValue = "";  // 시리얼 데이터에서 추출한 밝기 값 (문자열)
let modeValue = "";        // 시리얼 데이터에서 추출한 모드 정보 (문자열)
// LED 상태 배열: [red, yellow, green] 각 값은 0 또는 1로 LED의 OFF/ON 상태를 나타냄
let ledState = [0, 0, 0];

// HTML 요소 참조용 변수 선언
let connectButton; // 시리얼 연결 버튼
let rSlider, ySlider, gSlider; // 슬라이더 요소: 각각 빨강, 노랑, 초록 LED 밝기 조절용
let lastSentTime = 0;          // 마지막으로 슬라이더 값을 전송한 시간 기록
let sendInterval = 500;        // 슬라이더 값 전송 주기 (500ms마다 전송)

// p5.js의 setup 함수: 초기 설정 수행
function setup() {
  // (1) p5.js의 createCanvas()는 여기서는 생략하거나 숨길 수 있음
  // createCanvas(1, 1);  // 만약 필요하면 아주 작은 캔버스 생성
  // noLoop();           // draw()를 사용하지 않을 경우 호출할 수 있음

  // (2) p5.dom 함수를 사용하여 HTML 요소들을 선택
  connectButton = select("#connectButton"); // ID가 "connectButton"인 버튼 선택
  // 버튼 클릭 시 시리얼 연결 함수(connectSerial)를 실행하도록 설정
  connectButton.mousePressed(connectSerial);

  // 슬라이더 요소 선택: 각각의 슬라이더는 LED 밝기 값을 조절하기 위한 요소
  rSlider = select("#rSlider");
  ySlider = select("#ySlider");
  gSlider = select("#gSlider");

  // (3) draw() 함수 대신 setInterval 등으로 주기적인 작업도 가능함
}

// p5.js의 draw 함수: 주기적으로 실행되며, 슬라이더 값 전송을 담당
function draw() {
  // 포트가 연결되어 있고, 마지막 전송 이후 sendInterval(500ms) 이상 경과하면
  if (portConnected && millis() - lastSentTime > sendInterval) {
    sendSliderValues();       // 슬라이더 값을 전송
    lastSentTime = millis();  // 마지막 전송 시간을 업데이트
  }
}

/**
 * 시리얼 포트를 선택하고 연결을 시도하는 비동기 함수
 */
async function connectSerial() {
  try {
    // 사용자가 시리얼 포트를 선택하도록 요청
    port = await navigator.serial.requestPort();
    // 선택된 포트를 baudRate 9600으로 열기
    await port.open({ baudRate: 9600 });
    // 연결 상태 업데이트
    portConnected = true;
    // 연결 성공 시 버튼 텍스트 변경
    connectButton.html("Serial Connected");
    // 데이터 읽기 루프 시작
    readLoop();
  } catch (error) {
    // 연결 시 에러 발생 시 콘솔에 에러 메시지 출력
    console.log("Serial connection error: " + error);
  }
}

/**
 * 시리얼 데이터를 지속적으로 읽어들이는 비동기 루프 함수
 */
async function readLoop() {
  // TextDecoder를 사용하여 바이트 데이터를 문자열로 디코딩
  const decoder = new TextDecoder();
  // 포트가 읽기 가능한 동안 반복
  while (port.readable) {
    // 포트에서 리더(reader)를 얻어옴
    const reader = port.readable.getReader();
    try {
      while (true) {
        // 리더를 통해 데이터를 읽음
        const { value, done } = await reader.read();
        if (done) break;  // 더 이상 읽을 데이터가 없으면 반복 종료
        if (value) {
          // 읽은 값을 디코딩하여 latestData에 누적
          latestData += decoder.decode(value);
          // 누적된 데이터에서 줄바꿈("\n")이 포함되어 있으면 한 줄씩 분할 처리
          if (latestData.indexOf("\n") !== -1) {
            let lines = latestData.split("\n");
            // 첫 번째 완성된 라인(trim()을 통해 앞뒤 공백 제거)
            let completeLine = lines[0].trim();
            // 파싱 함수로 완성된 데이터 전달
            processSerialData(completeLine);
            // 남은 데이터를 다시 latestData에 저장
            latestData = lines.slice(1).join("\n");
          }
        }
      }
    } catch (error) {
      // 읽기 중 에러 발생 시 에러 메시지 출력
      console.log("Read error: " + error);
    } finally {
      // 리더의 잠금(lock)을 해제하여 다음 읽기 작업에 방해가 되지 않도록 함
      reader.releaseLock();
    }
  }
}

/**
 * 시리얼로부터 받은 문자열 데이터를 파싱하는 함수
 * 예시 데이터 형식: "B: 160 M: PCINT2 O: 1,0,1"
 *
 * @param {string} dataStr - 수신한 데이터 문자열
 */
function processSerialData(dataStr) {
  // 정규표현식을 사용해 데이터 형식에 맞는 값을 추출
  const pattern = /^B:\s*(\d+)\s*M:\s*(\S+)\s*O:\s*([\d,]+)/;
  const match = dataStr.match(pattern);

  // 정규표현식에 매칭되면
  if (match) {
    let newBrightness = match[1];  // 밝기 값 추출
    let newMode = match[2];        // 모드 정보 추출
    let ledStates = match[3];      // LED 상태(콤마로 구분된 값들) 추출

    // 밝기 값 업데이트: 숫자인지 확인 후 변수에 저장
    if (!isNaN(newBrightness) && newBrightness !== "") {
      brightnessValue = newBrightness;
    }

    // 모드 업데이트: 허용된 모드 값만 처리
    let validModes = ["PCINT1", "PCINT2", "PCINT3", "Default"];
    if (validModes.includes(newMode)) {
      // 모드에 따라 modeValue를 사용자 친화적인 문자열로 변환
      switch (newMode) {
        case "PCINT1":
          modeValue = "Mode1";
          break;
        case "PCINT2":
          modeValue = "Mode2";
          break;
        case "PCINT3":
          modeValue = "Mode3";
          break;
        default:
          modeValue = "Default";
          break;
      }
    }

    // LED 상태 업데이트: 콤마로 구분된 문자열을 분할하여 정수형으로 변환
    let states = ledStates.split(",");
    if (states.length === 3) {
      ledState[0] = parseInt(states[0]); // 빨강 LED 상태
      ledState[1] = parseInt(states[1]); // 노랑 LED 상태
      ledState[2] = parseInt(states[2]); // 초록 LED 상태
    }

    // 파싱된 데이터가 업데이트되었으므로, UI를 갱신하는 함수 호출
    updateInfoDisplay();
    updateIndicators();
  }
}

/**
 * 슬라이더의 값을 시리얼 포트를 통해 Arduino로 전송하는 비동기 함수
 */
async function sendSliderValues() {
  // 포트가 존재하고 쓰기 가능할 경우에만 전송
  if (port && port.writable) {
    const encoder = new TextEncoder();
    // 슬라이더 값들을 "rSlider,ySlider,gSlider\n" 형태의 문자열로 구성
    let dataToSend =
      rSlider.value() + "," + ySlider.value() + "," + gSlider.value() + "\n";
    // 포트의 쓰기 가능한 스트림에서 writer를 얻음
    const writer = port.writable.getWriter();
    // 문자열을 인코딩 후 전송
    await writer.write(encoder.encode(dataToSend));
    // 작업 후 writer의 잠금을 해제
    writer.releaseLock();
  }
}

/**
 * HTML의 시리얼 정보 표시 요소를 업데이트하는 함수
 * 현재 밝기와 모드 정보를 사용자에게 표시함
 */
function updateInfoDisplay() {
  // ID가 "serialInfo"인 요소를 선택하여 텍스트 업데이트
  const infoElement = document.getElementById("serialInfo");
  infoElement.textContent = `Brightness : ${brightnessValue} / Mode : ${modeValue}`;
}

/**
 * 신호등 인디케이터를 업데이트하는 함수
 * ledState 배열과 brightnessValue를 이용하여 각 LED 인디케이터의 색상을 조정함
 */
function updateIndicators() {
  // 문자열로 저장된 brightnessValue를 정수로 변환 (만약 NaN이면 0으로 설정)
  let bVal = parseInt(brightnessValue);
  if (isNaN(bVal)) bVal = 0;

  // 각 인디케이터 DOM 요소 선택 (ID: red-indicator, yellow-indicator, green-indicator)
  const redIndicator = document.getElementById("red-indicator");
  const yellowIndicator = document.getElementById("yellow-indicator");
  const greenIndicator = document.getElementById("green-indicator");

  // 빨강 LED 인디케이터 업데이트
  if (ledState[0] === 1) {
    // LED가 켜져 있으면 지정된 밝기(bVal)로 표시
    redIndicator.style.backgroundColor = `rgb(${bVal}, 0, 0)`;
  } else {
    // LED가 꺼져 있으면 밝기의 20% 수준으로 어둡게 표시
    redIndicator.style.backgroundColor = `rgb(${bVal * 0.2}, 0, 0)`;
  }

  // 노랑 LED 인디케이터 업데이트 (빨강+초록 조합)
  if (ledState[1] === 1) {
    yellowIndicator.style.backgroundColor = `rgb(${bVal}, ${bVal}, 0)`;
  } else {
    yellowIndicator.style.backgroundColor = `rgb(${bVal * 0.2}, ${bVal * 0.2}, 0)`;
  }

  // 초록 LED 인디케이터 업데이트
  if (ledState[2] === 1) {
    greenIndicator.style.backgroundColor = `rgb(0, ${bVal}, 0)`;
  } else {
    greenIndicator.style.backgroundColor = `rgb(0, ${bVal * 0.2}, 0)`;
  }
}
