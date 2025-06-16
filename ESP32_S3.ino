#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DFMiniMp3.h>
#include <FS.h>
#include <SPIFFS.h> // SPIFFS는 사용하지 않지만, 원래 코드에 있어 일단 남겨둡니다.

// ✅ DFPlayer 알림 콜백 정의
class Mp3Notify {
public:
  static void OnError(DFMiniMp3<HardwareSerial, Mp3Notify>&, uint16_t errorCode) {
    Serial.print("DFPlayer Error: "); Serial.println(errorCode);
  }
  static void OnPlayFinished(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources, uint16_t track) {
    Serial.print("Finished track: "); Serial.println(track);
  }
  static void OnCardOnline(DFMiniMp3<HardwareSerial, Mp3Notify>&, uint16_t) { Serial.println("Card online"); }
  static void OnCardInserted(DFMiniMp3<HardwareSerial, Mp3Notify>&, uint16_t) { Serial.println("Card inserted"); }
  static void OnCardRemoved(DFMiniMp3<HardwareSerial, Mp3Notify>&, uint16_t) { Serial.println("Card removed"); }
  static void OnPlaySourceOnline(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
  static void OnPlaySourceInserted(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
  static void OnPlaySourceRemoved(DFMiniMp3<HardwareSerial, Mp3Notify>&, DfMp3_PlaySources) {}
};

// 🎵 DFPlayer Mini 핀 설정 및 객체 생성
#define MP3_RX 10
#define MP3_TX 9
HardwareSerial mp3Serial(2);
DFMiniMp3<HardwareSerial, Mp3Notify> mp3(mp3Serial);

// 🌐 WiFi 정보
const char* ssid = "Test";
const char* password = "worms6964";

WebServer server(80);

// 📄 HTML을 문자열로 선언 (버튼 크기 및 배경색 변경)
// CSS를 <style> 태그 안에 직접 추가하여 스타일을 적용합니다.
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>MP3 컨트롤</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background-color: #f0f8ff; /* 연한 하늘색 배경 */
      color: #333;
      margin-top: 50px;
    }
    h1 {
      color: #0056b3;
    }
    button {
      background-color: #4CAF50; /* 녹색 */
      color: white;
      padding: 15px 30px; /* 버튼 패딩을 늘려 크기 키움 */
      margin: 10px;
      border: none;
      border-radius: 8px; /* 둥근 모서리 */
      cursor: pointer;
      font-size: 20px; /* 글자 크기 키움 */
      transition: background-color 0.3s ease; /* 호버 효과 */
    }
    button:hover {
      background-color: #45a049;
    }
    input[type="range"] {
      width: 80%; /* 슬라이더 너비 조정 */
      margin-top: 20px;
      -webkit-appearance: none; /* 기본 스타일 제거 */
      height: 10px; /* 슬라이더 트랙 높이 */
      background: #ddd; /* 슬라이더 트랙 배경 */
      border-radius: 5px;
      outline: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 25px; /* 슬라이더 핸들 크기 */
      height: 25px;
      border-radius: 50%;
      background: #007bff; /* 슬라이더 핸들 색상 */
      cursor: pointer;
      margin-top: -8px; /* 핸들을 트랙 중앙으로 */
      box-shadow: 1px 1px 2px rgba(0,0,0,0.4);
    }
    label, span {
        font-size: 18px;
        font-weight: bold;
    }
  </style>
</head>
<body>
  <h1>🎵 MP3 컨트롤 패널</h1>
  <button onclick="fetch('/play')">▶ 재생</button>
  <button onclick="fetch('/stop')">⏹ 정지</button>
  <br><br>
  <label for="vol">볼륨 조절 (0~30): </label>
  <input type="range" id="vol" min="0" max="30" value="20" onchange="setVolume(this.value)">
  <span id="vlabel">20</span>
  <script>
    function setVolume(val) {
      document.getElementById('vlabel').innerText = val;
      fetch('/volume?level=' + val);
    }
  </script>
</body>
</html>
)rawliteral";

// ▶ 재생 핸들러
void handlePlay() {
  mp3.playMp3FolderTrack(1);
  Serial.println("Play command sent.");
  server.send(200, "text/plain", "Playing");
}

// ⏹ 정지 핸들러
void handleStop() {
  mp3.stop();
  Serial.println("Stop command sent.");
  server.send(200, "text/plain", "Stopped");
}

// 🔊 볼륨 핸들러
void handleVolume() {
  if (server.hasArg("level")) {
    int volume = server.arg("level").toInt();
    if (volume >= 0 && volume <= 30) {
      mp3.setVolume(volume);
      Serial.print("Volume set to ");
      Serial.println(volume);
      server.send(200, "text/plain", "Volume set to " + String(volume));
    } else {
      server.send(400, "text/plain", "Volume must be between 0 and 30");
    }
  } else {
    server.send(400, "text/plain", "Missing volume parameter");
  }
}

// 🌐 루트 핸들러 (HTML 반환)
void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup...");

  // 🎵 DFPlayer 설정
  mp3Serial.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  mp3.begin();
  mp3.setVolume(20);
  Serial.println("MP3 player started");

  // 📡 WiFi 연결
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // 🌐 라우팅 설정
  server.on("/", handleRoot);
  server.on("/play", handlePlay);
  server.on("/stop", handleStop);
  server.on("/volume", handleVolume);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  mp3.loop();
}