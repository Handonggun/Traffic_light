// -------------------------
// 📌 전역 변수 선언
// -------------------------

// 🚀 시리얼 포트를 저장할 변수 (사용자가 선택한 포트)
let port;

// 🔄 시리얼 포트 연결 상태 플래그 (true: 연결됨, false: 미연결)
let portConnected = false;

// 📡 수신한 시리얼 데이터 저장 변수 (누적 문자열)
let latestData = "";

// 💡 파싱된 데이터 저장 변수 (각 LED 밝기 및 상태 관리)
let brightnessValue = "";  // LED 밝기 값 (문자열)
let modeValue = "";        // 현재 모드 정보 (문자열)
// 🔴🟡🟢 LED 상태 배열: [red, yellow, green] (각 값은 0 또는 1로 LED ON/OFF 표현)
let ledState = [0, 0, 0];

// 🖥️ HTML 요소 참조용 변수
let connectButton; // 시리얼 포트 연결 버튼
let rSlider, ySlider, gSlider; // 🔴🟡🟢 LED 밝기 조절 슬라이더

// ⏳ 마지막으로 데이터를 전송한 시간 기록 (밀리초 단위)
let lastSentTime = 0;  
// ⏱️ 슬라이더 값 전송 간격 (500ms마다 전송)
let sendInterval = 500;

// -------------------------
// 🛠️ p5.js setup 함수 (초기 설정)
// -------------------------

function setup() {
  // 📌 HTML 요소 연결
  connectButton = select("#connectButton"); // 시리얼 연결 버튼 가져오기
  rSlider = select("#rSlider");   // 🔴 빨간 LED 슬라이더
  ySlider = select("#ySlider");   // 🟡 노란 LED 슬라이더
  gSlider = select("#gSlider");   // 🟢 초록 LED 슬라이더

  // 🔗 버튼 클릭 시 시리얼 포트 연결 함수 실행
  connectButton.mousePressed(connectSerial);
}

// -------------------------
// 🔄 p5.js draw 함수 (500ms마다 슬라이더 값 전송)
// -------------------------

function draw() {
  // ⏳ 포트가 연결되어 있고, 마지막 전송 이후 sendInterval(500ms) 이상 경과했을 때
  if (portConnected && millis() - lastSentTime > sendInterval) {
    sendSliderValues();       // 📡 슬라이더 값을 시리얼 포트로 전송
    lastSentTime = millis();  // ⏱️ 전송한 시간 갱신
  }
}

// -------------------------
// 🔗 시리얼 포트 연결 함수 (비동기)
// -------------------------

async function connectSerial() {
  try {
    // 🖥️ 사용자가 시리얼 포트를 선택하도록 요청
    port = await navigator.serial.requestPort();
    // ⚡ 선택한 포트를 baudRate 9600으로 설정 후 열기
    await port.open({ baudRate: 9600 });
    
    // ✅ 연결 성공: 상태 업데이트
    portConnected = true;
    connectButton.html("Serial Connected"); // 버튼 텍스트 변경

    // 📡 데이터 수신 시작 (무한 루프 실행)
    readLoop();
  } catch (error) {
    console.log("🚨 Serial connection error: " + error); // ❌ 연결 오류 출력
  }
}

// -------------------------
// 📡 시리얼 데이터 수신 루프 (비동기)
// -------------------------

async function readLoop() {
  const decoder = new TextDecoder(); // 🧩 바이트 데이터를 문자열로 변환하는 디코더

  // 🔁 포트가 읽기 가능한 동안 계속 실행
  while (port.readable) {
    const reader = port.readable.getReader(); // 📖 데이터 읽기 객체 생성
    try {
      while (true) {
        const { value, done } = await reader.read(); // 📡 데이터 읽기
        if (done) break; // 🛑 더 이상 읽을 데이터가 없으면 종료

        if (value) {
          latestData += decoder.decode(value); // 📥 수신된 데이터를 누적 저장

          // 🔍 줄바꿈("\n")이 포함된 경우, 한 줄씩 분리하여 처리
          if (latestData.indexOf("\n") !== -1) {
            let lines = latestData.split("\n");
            let completeLine = lines[0].trim(); // 첫 번째 줄 가져오기
            processSerialData(completeLine); // 데이터 파싱 함수 호출
            latestData = lines.slice(1).join("\n"); // 📜 나머지 데이터 보존
          }
        }
      }
    } catch (error) {
      console.log("❌ Read error: " + error); // 🚨 데이터 읽기 오류 출력
    } finally {
      reader.releaseLock(); // 🔓 리더 잠금 해제 (다음 읽기 수행 가능)
    }
  }
}

// -------------------------
// 📊 시리얼 데이터 파싱 함수
// -------------------------

function processSerialData(dataStr) {
  // 📌 데이터 예시: "B: 160 M: PCINT2 O: 1,0,1"
  const pattern = /^B:\s*(\d+)\s*M:\s*(\S+)\s*O:\s*([\d,]+)/;
  const match = dataStr.match(pattern);

  if (match) {
    let newBrightness = match[1];  // 💡 밝기 값
    let newMode = match[2];        // 🎛️ 모드 정보
    let ledStates = match[3];      // 🔴🟡🟢 LED 상태

    // 📡 값 검증 후 변수 업데이트
    if (!isNaN(newBrightness) && newBrightness !== "") {
      brightnessValue = newBrightness;
    }

    // 🎛️ 모드 값 업데이트
    let validModes = ["PCINT1", "PCINT2", "PCINT3", "Default"];
    if (validModes.includes(newMode)) {
      switch (newMode) {
        case "PCINT1": modeValue = "Mode 1"; break;
        case "PCINT2": modeValue = "Mode 2"; break;
        case "PCINT3": modeValue = "Mode 3"; break;
        default: modeValue = "Default Mode"; break;
      }
    }

    // 🔴🟡🟢 LED 상태 업데이트
    let states = ledStates.split(",");
    if (states.length === 3) {
      ledState[0] = parseInt(states[0]); // 🔴 빨간불 상태
      ledState[1] = parseInt(states[1]); // 🟡 노란불 상태
      ledState[2] = parseInt(states[2]); // 🟢 초록불 상태
    }

    updateInfoDisplay(); // 📌 UI 정보 업데이트
    updateIndicators();  // 🎨 LED 인디케이터 업데이트
  }
}

// -------------------------
// 📤 슬라이더 값을 시리얼 포트로 전송 (비동기)
// -------------------------

async function sendSliderValues() {
  if (port && port.writable) {
    const encoder = new TextEncoder();
    let dataToSend = `${rSlider.value()},${ySlider.value()},${gSlider.value()}\n`;

    const writer = port.writable.getWriter(); // 📝 데이터 쓰기 객체 가져오기
    await writer.write(encoder.encode(dataToSend)); // ✍️ 데이터 전송
    writer.releaseLock(); // 🔓 리소스 해제
  }
}

// -------------------------
// 🖥️ UI 정보 표시 업데이트 함수
// -------------------------

function updateInfoDisplay() {
  const infoElement = document.getElementById("serialInfo");
  infoElement.textContent = `Brightness: ${brightnessValue} / Mode: ${modeValue}`;
}

// -------------------------
// 🎨 LED 인디케이터 업데이트 함수
// -------------------------

function updateIndicators() {
  let bVal = parseInt(brightnessValue);
  if (isNaN(bVal)) bVal = 0;

  const redIndicator = document.getElementById("red-indicator");
  const yellowIndicator = document.getElementById("yellow-indicator");
  const greenIndicator = document.getElementById("green-indicator");

  redIndicator.style.backgroundColor = ledState[0] === 1 ? `rgb(${bVal},0,0)` : "gray";
  yellowIndicator.style.backgroundColor = ledState[1] === 1 ? `rgb(${bVal},${bVal},0)` : "gray";
  greenIndicator.style.backgroundColor = ledState[2] === 1 ? `rgb(0,${bVal},0)` : "gray";
}
