#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DFRobotDFPlayerMini.h> // DFRobotDFPlayerMini 라이브러리 사용
#include <DHT.h>
#include <Adafruit_Sensor.h> // DHT 라이브러리의 의존성
#include <time.h>
#include <FS.h>
#include <SPIFFS.h> // ✅ SPIFFS 라이브러리 추가
#include <MD_MAX72xx.h> // MAX7219 라이브러리
#include <vector>       // std::vector 사용을 위해
#include <algorithm>    // std::shuffle
#include <random>       // 난수 생성기
#include <chrono>       // 시간 기반 난수 초기화

// --- 전역 객체 및 변수 정의 ---
WebServer server(80);
#define CLK_PIN   18 // CLK 핀 (ESP32 GPIO 18)
#define DATA_PIN  11 // DIN 핀 (ESP32 GPIO 11)
#define CS_PIN    8  // CS 핀 (ESP32 GPIO 8)
#define MAX_DEVICES 1 // MAX7219 디바이스 개수
// FC16_HW: 일반적인 모듈 하드웨어 타입
MD_MAX72XX display = MD_MAX72XX(MD_MAX72XX::FC16_HW, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES); // 또는 GENERIC_HW

#define DHTPIN 12 // ✅ GPIO 6번 핀으로 변경했습니다!
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
const long utcOffsetInSeconds = 9 * 3600;
const char* ntpServer = "pool.ntp.org";

float currentHumidity = 0.0;
float currentTemperature = 0.0;
// DFPlayer 관련 객체
DFRobotDFPlayerMini mp3; 

String currentLanguage = "";
int currentLevel = 0;
int currentWordIndex = 0;
struct WordEntry {
    String english;
    String korean;
    int mp3Track;
};
WordEntry words_en_level1[] = {
    {"apple", "사과", 1},
    {"banana", "바나나", 2},
    {"cat", "고양이", 3},
    {"dog", "개", 4},
    {"elephant", "코끼리", 5}
};
int totalWords_en_level1 = sizeof(words_en_level1) / sizeof(words_en_level1[0]);
WordEntry words_ko_level1[] = {
    {"안녕하세요", "Hello", 101},
    {"감사합니다", "Thank you", 102},
    {"사랑해요", "I love you", 103},
    {"환영합니다", "Welcome", 104},
    {"죄송합니다", "Sorry", 105}
};
int totalWords_ko_level1 = sizeof(words_ko_level1) / sizeof(words_ko_level1[0]);

// Wi-Fi 연결 시간을 저장할 변수
String wifiConnectTime = "로딩중...";

// ✅ 센서 로그 관련 변수
const char* SENSOR_LOG_FILE = "/sensor_log.txt";
unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL = 30 * 60 * 1000; // 30분 (밀리초)

WordEntry getWord(String lang, int level, int index) {
    if (lang == "en" && level == 1) {
        if (index >= 0 && index < totalWords_en_level1) {
            return words_en_level1[index];
        }
    } else if (lang == "ko" && level == 1) {
         if (index >= 0 && index < totalWords_ko_level1) {
            return words_ko_level1[index];
        }
    }
    return {"N/A", "N/A", 0};
}

void readDHTSensor() {
    // delay(1000); // ✅ 이 delay는 비동기 웹 서버에 좋지 않습니다. 필요하다면 제거하거나 최소화하세요.
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
        Serial.println("Failed to read from DHT sensor!");
        // currentHumidity와 currentTemperature는 이전 값을 유지하거나 오류 값을 설정
        currentHumidity = -99.9; 
        currentTemperature = -99.9;
    } else {
        currentHumidity = h;
        currentTemperature = t;
        Serial.print("Temperature: ");
        Serial.print(currentTemperature);
        Serial.print(" *C, Humidity: ");
        Serial.print(currentHumidity);
        Serial.println(" %");
    }
}

String getCurrentTimeFormatted() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return "시간 동기화 오류";
  }
  char timeBuffer[50];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y년 %m월 %d일 %H:%M:%S", &timeinfo);
  return String(timeBuffer);
}

// ✅ 센서 로그 파일에 기록하는 함수
void logSensorData() {
    readDHTSensor(); // 최신 센서 데이터 읽기

    String logEntry = getCurrentTimeFormatted();
    logEntry += ", 온도: " + String(currentTemperature, 1) + "°C, 습도: " + String(currentHumidity, 1) + "%\n";

    File file = SPIFFS.open(SENSOR_LOG_FILE, FILE_APPEND);
    if (!file) {
        Serial.println("Failed to open log file for appending");
        return;
    }
    if (file.print(logEntry)) {
        Serial.print("Logged sensor data: ");
        Serial.print(logEntry);
    } else {
        Serial.println("Failed to write to log file");
    }
    file.close();
}

// ✅ 센서 로그 파일을 읽어서 웹 페이지로 보여주는 핸들러
void handleSensorLog() {
    if (!SPIFFS.exists(SENSOR_LOG_FILE)) {
        server.send(200, "text/plain", "No sensor log found yet.");
        return;
    }

    File file = SPIFFS.open(SENSOR_LOG_FILE, FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Failed to open sensor log file for reading.");
        return;
    }

    String logContent = "";
    while (file.available()) {
        logContent += (char)file.read();
    }
    file.close();

    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>센서 기록 로그</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 800px;} h1 { color: #333; }
          pre { background-color: #e9ecef; padding: 15px; border-radius: 5px; text-align: left; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }
          .back-button {
            background-color: #007bff;
            color: white;
            padding: 10px 15px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 14px;
            margin: 20px 5px;
            cursor: pointer;
            border-radius: 5px;
            border: none;
          }
          .back-button:hover { opacity: 0.8; }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>📊 센서 기록 로그</h1>
          <pre>)rawliteral" + logContent + R"rawliteral(</pre>
          <a class="back-button" href="/">메인 메뉴로</a>
        </div>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}

void handleDataAPI() {
    readDHTSensor();
    String jsonResponse = "{";
    jsonResponse += "\"time\":\"" + getCurrentTimeFormatted() + "\",";
    jsonResponse += "\"temperature\":" + String(currentTemperature, 1) + ",";
    jsonResponse += "\"humidity\":" + String(currentHumidity, 1) + ",";
    jsonResponse += "\"connect_time\":\"" + wifiConnectTime + "\""; // Wi-Fi 연결 시간
    jsonResponse += "}";
    server.send(200, "application/json", jsonResponse);
}

// 🌐 메인 메뉴 핸들러 (HTML 내용은 동일)
void handleRoot() {
    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>ESP32-S3 학습 환경 모니터링</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; padding: 30px; border-radius: 8px; 
            box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 500px;} h1 { color: #333; }
          .button {
            background-color: #007bff;
            color: white;
            padding: 15px 25px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            margin: 10px 5px;
            cursor: pointer;
            border-radius: 5px;
            width: calc(50% - 20px);
            /* Adjust width to fit two buttons per line */
            box-sizing: border-box;
          }
          .button:hover { background-color: #0056b3;
          }
          .info-box {
            background-color: #e9ecef;
            padding: 15px;
            border-radius: 5px;
            margin-top: 20px;
            text-align: left;
            font-size: 0.9em;
            color: #495057;
          }
          .value { font-weight: bold; color: #007bff;
          }
          .temp { color: #dc3545;
          }
          .hum { color: #17a2b8;
          }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>💡 학습 도우미 메인 메뉴</h1>
          <p class="info-box">
            현재 시간: <span id="currentTime" class="value">로딩중...</span><br>
            현재 온도: <span id="currentTemp" class="value temp">로딩중...</span>&deg;C<br>
            현재 
            습도: <span id="currentHum" class="value hum">로딩중...</span>%<br>
            연결 시간: <span id="connectTime" class="value">로딩중...</span>
          </p>
          <a class="button" href="/learn">👩‍🏫 학습 모드</a>
          <a class="button" href="/quiz_select">🧠 퀴즈 모드</a>
          <a class="button" href="/mp3_control">🎵 MP3 컨트롤</a>
          <a class="button" href="/led_matrix_control">💡 LED 매트릭스 제어</a> <a class="button" href="/sensor_log">📊 센서 기록 보기</a> <a class="button" href="/restart_esp">🔄 ESP 재시작</a>
        </div>
        <script>
          function fetchData() {
  
            var xhr = new XMLHttpRequest();
            xhr.onreadystatechange = function() {
              if (this.readyState == 4 && this.status == 200) {
                var data = JSON.parse(this.responseText);
                document.getElementById('currentTime').innerText = data.time;
                document.getElementById('currentTemp').innerText = data.temperature;
                document.getElementById('currentHum').innerText = data.humidity;
                document.getElementById('connectTime').innerText = data.connect_time; 
              }
            };
            xhr.open("GET", "/data", true);
            xhr.send();
          }

          // 3초마다 데이터 업데이트
          setInterval(fetchData, 3000);
          // 페이지 로드 시 즉시 데이터 가져오기
          window.onload = fetchData;
        </script>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}

// 🎵 MP3 컨트롤 핸들러
void handleMP3Control() {
    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>MP3 컨트롤</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 500px;} h1 { color: #333; }
          .button {
            background-color: #4CAF50;
            color: white;
            padding: 15px 25px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            margin: 10px 5px;
            cursor: pointer;
            border-radius: 5px;
          }
          .button.red { background-color: #f44336;
          }
          .button.blue { background-color: #008CBA;
          }
          .button.back { background-color: #555;
          }
          /* 슬라이더 스타일 */
          .slider-container {
            margin-top: 20px;
            padding: 15px;
            background-color: #f9f9f9;
            border-radius: 5px;
          }
          .slider {
            width: 80%;
            height: 25px;
            background: #d3d3d3;
            outline: none;
            opacity: 0.7;
            -webkit-transition: .2s;
            transition: opacity .2s;
            border-radius: 5px;
            margin: 0 auto;
            display: block;
          }
          .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 25px;
            height: 25px;
            background: #007bff;
            cursor: pointer;
            border-radius: 50%;
          }
          .slider::-moz-range-thumb {
            width: 25px;
            height: 25px;
            background: #007bff;
            cursor: pointer;
            border-radius: 50%;
          }
          #volumeValue {
            font-weight: bold;
            color: #007bff;
            margin-top: 10px;
          }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>MP3 플레이어 컨트롤</h1>
          <a class="button" href="/mp3_play?track=1">트랙 1 재생</a>
          <a class="button" href="/mp3_play?track=2">트랙 2 재생</a>
          
          <div class="slider-container">
            <label for="volumeSlider">볼륨 조절:</label>
            <input type="range" min="0" max="30" value="20" class="slider" id="volumeSlider">
            <p>현재 볼륨: <span id="volumeValue">20</span></p>
          </div>

          <a class="button red" href="/mp3_pause">일시정지</a>
          <a class="button blue" href="/mp3_resume">재생</a>
          <a class="button back" href="/">메인 메뉴로</a>
        </div>

        <script>
            const volumeSlider = document.getElementById('volumeSlider');
            const volumeValueSpan = document.getElementById('volumeValue');

            // 페이지 로드 시 현재 볼륨 값을 ESP32에서 가져와 슬라이더에 반영
            function fetchCurrentVolume() {
                fetch('/get_current_volume')
                    .then(response => response.json())
                    .then(data => {
                        volumeSlider.value = data.volume;
                        volumeValueSpan.innerText = data.volume;
                    })
                    .catch(error => console.error('Error fetching volume:', error));
            }

            // 슬라이더 값이 변경될 때마다 ESP32로 볼륨 값 전송
            volumeSlider.oninput = function() {
                volumeValueSpan.innerText = this.value;
                fetch(`/set_volume?vol=${this.value}`)
                    .then(response => response.text())
                    .then(data => console.log(data))
                    .catch(error => console.error('Error setting volume:', error));
            };

            window.onload = fetchCurrentVolume; // 페이지 로드 시 볼륨 값 가져오기
        </script>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}

// ✅ 추가: 볼륨 설정 핸들러
void handleSetVolume() {
    if (server.hasArg("vol")) {
        int volume = server.arg("vol").toInt();
        if (volume >= 0 && volume <= 30) {
            mp3.volume(volume);
            server.send(200, "text/plain", "Volume set to " + String(volume));
        } else {
            server.send(400, "text/plain", "Invalid volume value. (0-30)");
        }
    } else {
        server.send(400, "text/plain", "Missing volume parameter.");
    }
}

// ✅ 추가: 현재 볼륨 가져오는 핸들러 (웹 페이지에서 초기 슬라이더 값 설정용)
void handleGetCurrentVolume() {
    int currentVol = mp3.readVolume();
    String jsonResponse = "{\"volume\":" + String(currentVol) + "}";
    server.send(200, "application/json", jsonResponse);
}


void handleMp3Play() {
    if (server.hasArg("track")) {
        int track = server.arg("track").toInt();
        mp3.play(track); 
       
        server.sendHeader("Location", "/mp3_control"); // 변경된 볼륨 슬라이더가 있는 페이지로 리다이렉트
        server.send(302, "text/plain", "Playing track " + String(track));
    } else {
        server.send(400, "text/plain", "Missing track parameter");
    }
}

// 기존 볼륨 UP/DOWN 핸들러는 이제 사용하지 않으므로 제거됩니다.
// void handleMp3VolUp() { ... }
// void handleMp3VolDown() { ... }

void handleMp3Pause() {
    mp3.pause(); 
    server.sendHeader("Location", "/mp3_control");
    server.send(302, "text/plain", "Paused");
}

void handleMp3Resume() {
    mp3.start(); // 재생 재개
    server.sendHeader("Location", "/mp3_control");
    server.send(302, "text/plain", "Resumed");
}

void handleRestartESP() {
    server.send(200, "text/html", "<html><body><h1>ESP32 재시작 중...</h1><p>잠시 후 다시 접속해주세요.</p></body></html>");
    delay(100);
    ESP.restart();
}

// 📚 학습 모드 핸들러 (HTML 내용은 동일)
void handleLearnPage() {
    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>학습 모드</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 
4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 500px;} h1 { color: #333; }
          form { margin-top: 20px; }
          select, input[type="submit"] {
            padding: 10px;
            margin: 5px;
            border-radius: 5px;
            border: 1px solid #ddd;
       
            font-size: 16px;
          }
          input[type="submit"] {
            background-color: #28a745;
            color: white;
            cursor: pointer;
            border: none;
          }
          input[type="submit"]:hover { background-color: #218838;
          }
          .current-word-display {
            background-color: #e9ecef;
            padding: 15px;
            margin-top: 20px;
            border-radius: 5px;
            font-size: 1.1em;
            color: #333;
          }
          .current-word-display .english { font-weight: bold; color: #007bff; font-size: 1.5em;
            margin-bottom: 5px;}
          .current-word-display .korean { color: #6c757d; font-size: 1.2em;
          }
          .control-buttons .button {
            background-color: #007bff;
            color: white;
            padding: 10px 15px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 14px;
            margin: 5px;
            cursor: pointer;
            border-radius: 5px;
          }
          .control-buttons .button.red { background-color: #dc3545;
          }
          .control-buttons .button.orange { background-color: #ffc107;
            color: #333;}
          .control-buttons .button:hover { opacity: 0.8;
          }
          .back-button {
            background-color: #555;
            margin-top: 20px;
          }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>📝 학습 모드</h1>
          <form action="/learn_start" method="get">
            <label for="lang">언어 선택:</label>
            <select name="lang" id="lang">
              <option value="en">영어</option>
  
              <option value="ko">한국어</option>
            </select>
            <label for="level">레벨 선택:</label>
            <select name="level" id="level">
              <option value="1">레벨 1</option>
            </select>
            <input type="submit" value="학습 시작">
    
          </form>

          <div class="current-word-display" id="currentWordDisplay" style="display:none;">
            <p><span class="english" id="displayEnglish"></span></p>
            <p><span class="korean" id="displayKorean"></span></p>
          </div>

          <div class="control-buttons">
            <button class="button" onclick="playCurrentWord()">다시 듣기</button>
            <button class="button" onclick="nextWord()">다음 단어</button>
  
            <button class="button red" onclick="stopLearning()">학습 종료</button>
          </div>

          <a class="button back-button" href="/">메인 메뉴로</a>
        </div>

        <script>
          let currentLang = "";
          let currentLevel = 0;
          let currentWordIndex = -1;

          function updateWordDisplay(english, korean) {
            document.getElementById('displayEnglish').innerText = english;
            document.getElementById('displayKorean').innerText = korean;
            document.getElementById('currentWordDisplay').style.display = 'block';
          }

          function fetchCurrentWord() {
            fetch('/learn_play_current_data')
              .then(response => response.json())
              .then(data => {
                if (data.english && data.korean) {
                  
                  updateWordDisplay(data.english, data.korean);
                  currentWordIndex = data.index;
                  currentLang = data.lang;
                  currentLevel = data.level;
                } else {
                  updateWordDisplay("---", "---");
  
                  document.getElementById('currentWordDisplay').style.display = 'block';
                }
              })
              .catch(error => {
                console.error('Error fetching current word:', error);
                
                updateWordDisplay("데이터 로드 오류", "다시 시도해주세요");
              });
          }

          function playCurrentWord() {
            fetch('/learn_play_current')
              .then(response => response.text())
              .then(data => console.log(data))
              .catch(error => console.error('Error playing current word:', error));
          }

          function nextWord() {
            fetch('/learn_next')
              .then(response => response.json())
              .then(data => {
                if (data.english && data.korean) {
                  updateWordDisplay(data.english, data.korean);
     
                  currentWordIndex = data.index;
                } else {
                  updateWordDisplay("학습 완료!", "모든 단어를 학습했습니다.");
                  document.getElementById('currentWordDisplay').style.display = 'block';
                }
         
              })
              .catch(error => console.error('Error getting next word:', error));
          }

          function stopLearning() {
            fetch('/learn_stop')
              .then(response => response.text())
              .then(data => {
                console.log(data);
                alert("학습 모드를 종료합니다.");
          
                window.location.href = "/";
              })
              .catch(error => console.error('Error stopping learning:', error));
          }

          window.onload = function() {
              fetchCurrentWord();
          };
        </script>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}


void handleLearnStart() {
    if (server.hasArg("lang") && server.hasArg("level")) {
        currentLanguage = server.arg("lang");
        currentLevel = server.arg("level").toInt();
        currentWordIndex = 0;

        WordEntry word = getWord(currentLanguage, currentLevel, currentWordIndex);
        if (word.mp3Track != 0) {
            
            mp3.play(word.mp3Track); 
        }
        
        server.sendHeader("Location", "/learn");
        server.send(302, "text/plain", "Learning started.");
    } else {
        server.send(400, "text/plain", "Invalid parameters for learn_start.");
    }
}

void handleLearnPlayCurrent() {
    WordEntry word = getWord(currentLanguage, currentLevel, currentWordIndex);
    if (word.mp3Track != 0) {
        mp3.play(word.mp3Track); 
        server.send(200, "text/plain", "Playing " 
+ word.english);
    } else {
        server.send(404, "text/plain", "No word or MP3 track found.");
    }
}

void handleLearnPlayCurrentData() {
    WordEntry word = getWord(currentLanguage, currentLevel, currentWordIndex);
    String jsonResponse = "{";
    jsonResponse += "\"english\":\"" + word.english + "\",";
    jsonResponse += "\"korean\":\"" + word.korean + "\",";
    jsonResponse += "\"index\":" + String(currentWordIndex) + ",";
    jsonResponse += "\"lang\":\"" + currentLanguage + "\",";
    jsonResponse += "\"level\":" + String(currentLevel);
    jsonResponse += "}";
    server.send(200, "application/json", jsonResponse);
}


void handleLearnNext() {
    int maxIndex = 0;
    if (currentLanguage == "en" && currentLevel == 1) {
        maxIndex = totalWords_en_level1;
    } else if (currentLanguage == "ko" && currentLevel == 1) {
        maxIndex = totalWords_ko_level1;
    }

    currentWordIndex++;
    if (currentWordIndex < maxIndex) {
        WordEntry word = getWord(currentLanguage, currentLevel, currentWordIndex);
        if (word.mp3Track != 0) {
            mp3.play(word.mp3Track);
        }
        String jsonResponse = "{";
        jsonResponse += "\"english\":\"" + word.english + "\",";
        jsonResponse += "\"korean\":\"" + word.korean + "\",";
        jsonResponse += "\"index\":" + String(currentWordIndex);
        jsonResponse += "}";
        server.send(200, "application/json", jsonResponse);
    } else {
        currentWordIndex = -1;
        currentLanguage = "";
        currentLevel = 0;
        mp3.stop();
        server.send(200, "application/json", "{\"message\":\"Learning finished\"}");
    }
}

void handleLearnStop() {
    currentWordIndex = -1;
    currentLanguage = "";
    currentLevel = 0;
    mp3.stop();
    server.send(200, "text/plain", "Learning stopped.");
}

// 🧠 퀴즈 모드 선택 핸들러 (HTML 내용은 동일)
void handleQuizSelectPage() {
    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>퀴즈 모드 선택</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; 
            padding: 30px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 500px;} h1 { color: #333; }
          .quiz-buttons .button {
            background-color: #ff9800;
            color: white;
            padding: 15px 25px;
            text-align: center;
   
            text-decoration: none;
            display: inline-block;
            font-size: 16px;
            margin: 10px 5px;
            cursor: pointer;
            border-radius: 5px;
            width: calc(50% - 20px);
            box-sizing: border-box;
          }
          .quiz-buttons .button:hover { background-color: #e68900;
          }
          .back-button {
            background-color: #555;
            margin-top: 20px;
          }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>퀴즈 모드</h1>
          <p>퀴즈 언어를 선택하세요:</p>
          <div class="quiz-buttons">
            <a class="button" href="/quiz?lang=en&level=1">영어 퀴즈 (레벨 1)</a>
            <a class="button" href="/quiz?lang=ko&level=1">한국어 퀴즈 (레벨 1)</a>
 
          </div>
          <a class="button back-button" href="/quiz_select">다른 퀴즈 선택</a>
        </div>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}

String quizCurrentLanguage = "";
int quizCurrentLevel = 0;
int quizCurrentWordIndex = -1;
String quizCurrentAnswer = "";

void handleQuizPageRender() {
    if (server.hasArg("lang") && server.hasArg("level")) {
        quizCurrentLanguage = server.arg("lang");
        quizCurrentLevel = server.arg("level").toInt();
    
        quizCurrentWordIndex = -1;

        String html = R"rawliteral(
          <!DOCTYPE HTML><html>
          <head>
            <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
            <title>퀴즈 모드</title>
            <style>
              body { font-family: Arial, Helvetica, sans-serif;
                text-align: center; background-color: #f0f0f0; margin-top: 50px;}
              .container { background-color: #fff;
                padding: 30px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 500px;} h1 { color: #333;
              }
              .question-box {
                background-color: #e9ecef;
                padding: 20px;
                margin-top: 20px;
                border-radius: 8px;
                font-size: 1.2em;
                color: #333;
                min-height: 80px;
                display: flex;
                align-items: center;
                justify-content: center;
                flex-direction: column;
              }
              .question-text { font-weight: bold; font-size: 1.8em;
                color: #007bff; margin-bottom: 10px;}
              .answer-input {
                width: calc(100% - 20px);
                padding: 10px;
                margin-top: 15px;
                border: 1px solid #ccc;
                border-radius: 5px;
                font-size: 1.1em;
              }
              .button-group { margin-top: 20px;
              }
              .button {
                background-color: #28a745;
                color: white;
                padding: 12px 20px;
                text-align: center;
                text-decoration: none;
                display: inline-block;
                font-size: 16px;
                margin: 5px;
                cursor: pointer;
                border-radius: 5px;
                border: none;
              }
              .button.orange { background-color: #ffc107;
                color: #333;}
              .button.red { background-color: #dc3545;
              }
              .button.back { background-color: #555;
              }
              .button:hover { opacity: 0.9;
              }
              .result-message {
                margin-top: 15px;
                font-size: 1.1em;
                font-weight: bold;
              }
              .result-correct { color: #28a745;
              }
              .result-wrong { color: #dc3545;
              }
              .result-info { color: #007bff;
              }
            </style>
          </head>
          <body>
            <div class="container">
              <h1>🧠 퀴즈 모드</h1>
              <div class="question-box">
                <span id="questionText" class="question-text">퀴즈를 시작하세요!</span>
     
              </div>
              <input type="text" id="answerInput" class="answer-input" placeholder="여기에 답을 입력하세요" onkeyup="if(event.keyCode === 13) checkAnswer()">
              <div class="button-group">
                <button class="button" onclick="startQuiz()">퀴즈 시작</button>
                <button class="button orange" onclick="playCurrentQuizWord()">다시 듣기</button>
             
                <button class="button" onclick="checkAnswer()">정답 확인</button>
                <button class="button" onclick="nextQuizWord()">다음 문제</button>
                <button class="button red" onclick="stopQuiz()">퀴즈 종료</button>
              </div>
              <div id="resultMessage" class="result-message"></div>
              <a class="button back-button" href="/quiz_select">다른 퀴즈 선택</a>
      
            </div>

            <script>
              let currentQuizWord = { english: "", korean: "", mp3Track: 0 };
              let quizWordIndex = -1;
              let quizLang = "";
              let quizLevel = 0;
              function startQuiz() {
                const urlParams = new URLSearchParams(window.location.search);
                quizLang = urlParams.get('lang');
                quizLevel = urlParams.get('level');

                fetch(`/quiz_start_action?lang=${quizLang}&level=${quizLevel}`)
                  .then(response => response.json())
                  .then(data => {
                    if (data.english || data.korean) {
                      currentQuizWord = data;
      
                      quizWordIndex = data.index;
                      document.getElementById('questionText').innerText = currentQuizWord.english;
                      document.getElementById('answerInput').value = '';
                      document.getElementById('resultMessage').innerText = '';
          
                      playCurrentQuizWord();
                    } else {
                      document.getElementById('questionText').innerText = "퀴즈 데이터를 불러올 수 없습니다.";
                      document.getElementById('resultMessage').innerText = "관리자에게 문의해주세요.";
             
                    }
                  })
                  .catch(error => {
                    console.error('Error starting quiz:', error);
                    document.getElementById('resultMessage').innerText = "퀴즈 시작 중 오류 발생.";
                  });
              }

              function playCurrentQuizWord() {
                fetch('/quiz_play_current')
                  .then(response => response.text())
                  .then(data => console.log(data))
                  .catch(error => console.error('Error playing current quiz word:', error));
              }

              function checkAnswer() {
                const userAnswer = document.getElementById('answerInput').value.trim();
                if (userAnswer === "") {
                    document.getElementById('resultMessage').innerText = "답변을 입력해주세요.";
                    document.getElementById('resultMessage').className = "result-message result-info";
                    return;
                }

                fetch(`/quiz_check?answer=${encodeURIComponent(userAnswer)}`)
                  .then(response => response.json())
                  .then(data => {
                    const resultDiv = document.getElementById('resultMessage');
                
                    if (data.isCorrect) {
                      resultDiv.innerText = `정답! (${data.correctAnswer})`;
                      resultDiv.className = "result-message result-correct";
                    } else {
                      
                      resultDiv.innerText = `오답! 정답은 '${data.correctAnswer}' 입니다.`;
                      resultDiv.className = "result-message result-wrong";
                    }
                  })
                  .catch(error => {
            
                    console.error('Error checking answer:', error);
                    document.getElementById('resultMessage').innerText = "정답 확인 중 오류 발생.";
                  });
              }

              function nextQuizWord() {
                fetch('/quiz_next')
                  .then(response => response.json())
                  .then(data => {
                    const questionText = document.getElementById('questionText');
     
                    const resultDiv = document.getElementById('resultMessage');
                    if (data.english || data.korean) {
                      currentQuizWord = data;
                      quizWordIndex = data.index;
          
                      questionText.innerText = currentQuizWord.english;
                      document.getElementById('answerInput').value = '';
                      resultDiv.innerText = '';
                      resultDiv.className = "result-message";
              
                      playCurrentQuizWord();
                    } else {
                      questionText.innerText = "퀴즈 완료!";
                      resultDiv.innerText = "모든 퀴즈를 풀었습니다. 수고하셨습니다!";
                  
                      resultDiv.className = "result-message result-info";
                      document.getElementById('answerInput').style.display = 'none';
                    }
                  })
                  .catch(error => {
                    console.error('Error getting next quiz word:', error);
                    document.getElementById('resultMessage').innerText = "다음 문제 로드 중 오류 발생.";
          
                  });
              }

              function stopQuiz() {
                fetch('/quiz_stop')
                  .then(response => response.text())
                  .then(data => {
                   
                    console.log(data);
                    alert("퀴즈 모드를 종료합니다.");
                    window.location.href = "/quiz_select";
                  })
                  .catch(error => console.error('Error stopping quiz:', error));
              }

              window.onload = function() {
              };
            </script>
          </body>
          </html>
    )rawliteral";
    server.send(200, "text/html", html);

    } else {
        server.send(400, "text/plain", "Invalid parameters for quiz.");
    }
}


void handleQuizStartAction() {
    if (server.hasArg("lang") && server.hasArg("level")) {
        quizCurrentLanguage = server.arg("lang");
        quizCurrentLevel = server.arg("level").toInt();
        quizCurrentWordIndex = 0;

       
        WordEntry word = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
        if (word.mp3Track != 0) {
            mp3.play(word.mp3Track); 
        }
        quizCurrentAnswer = word.korean;

        String jsonResponse = "{";
        jsonResponse += "\"english\":\"" + word.english + "\",";
        jsonResponse += "\"korean\":\"" + word.korean + "\",";
        jsonResponse += "\"index\":" + 
        String(quizCurrentWordIndex);
        jsonResponse += "}";
        server.send(200, "application/json", jsonResponse);
    } else {
        server.send(400, "text/plain", "Invalid parameters for quiz_start_action.");
    }
}


void handleQuizPlayCurrent() {
    WordEntry word = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
    if (word.mp3Track != 0) {
        mp3.play(word.mp3Track);
        server.send(200, "text/plain", "Playing quiz word " + word.english);
    } else {
        server.send(404, "text/plain", "No quiz word or MP3 track found.");
    }
}

void handleQuizCheck() {
    if (server.hasArg("answer")) {
        String userAnswer = server.arg("answer");
        bool isCorrect = false;

        if (quizCurrentLanguage == "en") {
             WordEntry word = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
            if (userAnswer.equalsIgnoreCase(word.korean)) {
                isCorrect = true;
            }
        } else if (quizCurrentLanguage == "ko") {
             WordEntry word = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
            if (userAnswer.equalsIgnoreCase(word.korean)) {
                isCorrect = true;
            }
        }
       
        WordEntry currentQuestionWord = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
        String correctAns = currentQuestionWord.korean;

        String jsonResponse = "{";
        jsonResponse += "\"isCorrect\":" + String(isCorrect ? "true" : "false") + ",";
        jsonResponse += "\"correctAnswer\":\"" + correctAns + "\"";
        jsonResponse += "}";
        server.send(200, "application/json", jsonResponse);
    } else {
        server.send(400, "text/plain", "Missing answer parameter for quiz_check.");
    }
}

void handleQuizNext() {
    int maxIndex = 0;
    if (quizCurrentLanguage == "en" && quizCurrentLevel == 1) {
        maxIndex = totalWords_en_level1;
    } else if (quizCurrentLanguage == "ko" && quizCurrentLevel == 1) {
        maxIndex = totalWords_ko_level1;
    }

    quizCurrentWordIndex++;
    if (quizCurrentWordIndex < maxIndex) {
        WordEntry word = getWord(quizCurrentLanguage, quizCurrentLevel, quizCurrentWordIndex);
        if (word.mp3Track != 0) {
            mp3.play(word.mp3Track);
        }
        quizCurrentAnswer = word.korean;

        String jsonResponse = "{";
        jsonResponse += "\"english\":\"" + word.english + "\",";
        jsonResponse += "\"korean\":\"" + word.korean + "\",";
        jsonResponse += "\"index\":" + String(quizCurrentWordIndex);
        jsonResponse += "}";
        server.send(200, "application/json", jsonResponse);
    } else {
        quizCurrentWordIndex = -1;
        quizCurrentLanguage = "";
        quizCurrentLevel = 0;
        mp3.stop(); 
        server.send(200, "application/json", "{\"message\":\"Quiz finished\"}");
    }
}


void handleQuizStop() {
    quizCurrentWordIndex = -1;
    quizCurrentLanguage = "";
    quizCurrentLevel = 0;
    mp3.stop(); 
    server.send(200, "text/plain", "Quiz stopped.");
}


// --- MAX7219 웹 컨트롤 관련 추가 코드 시작 ---

// MAX7219의 현재 상태를 저장할 8x8 배열 (열 기준)
// 이 배열은 ESP32의 실제 MAX7219 상태를 반영합니다.
byte currentDisplayColumns[8] = {
    0b00000000, // Col 0
    0b00000000, // Col 1
    0b00000000, // Col 2
    0b00000000, // Col 3
    0b00000000, // Col 4
    0b00000000, // Col 5
    0b00000000, // Col 6
    0b00000000  // Col 7
};

// 💡 LED 매트릭스 제어 페이지 핸들러
void handleLedMatrixControl() {
    String html = R"rawliteral(
      <!DOCTYPE HTML><html>
      <head>
        <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>MAX7219 LED 매트릭스 제어</title>
        <style>
          body { font-family: Arial, Helvetica, sans-serif; text-align: center; background-color: #f0f0f0; margin-top: 50px;}
          .container { background-color: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 400px;} h1 { color: #333; }
          .grid-container {
            display: grid;
            grid-template-columns: repeat(8, 40px); /* 8칸, 각 칸 40px */
            grid-template-rows: repeat(8, 40px);
            gap: 2px;
            width: 336px; /* 8 * 40px + 7 * 2px */
            margin: 20px auto;
            border: 1px solid #ccc;
          }
          .grid-cell {
            width: 40px;
            height: 40px;
            background-color: #eee;
            border: 1px solid #ddd;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 0.8em;
            transition: background-color 0.1s;
          }
          .grid-cell.on {
            background-color: #ffcc00; /* LED 켜진 색 */
          }
          .grid-cell:hover {
            background-color: #ccc;
          }
          .control-buttons { margin-top: 20px; }
          .control-buttons button, .back-button {
            background-color: #007bff;
            color: white;
            padding: 10px 15px;
            text-align: center;
            text-decoration: none;
            display: inline-block;
            font-size: 14px;
            margin: 5px;
            cursor: pointer;
            border-radius: 5px;
            border: none;
          }
          .control-buttons button.red { background-color: #dc3545; }
          .control-buttons button:hover, .back-button:hover { opacity: 0.8; }
        </style>
      </head>
      <body>
        <div class="container">
          <h1>💡 LED 매트릭스 제어</h1>
          <div class="grid-container" id="ledGrid">
            </div>
          <div class="control-buttons">
            <button onclick="clearAll()" class="red">모두 끄기</button>
            <button onclick="setFacePattern()">얼굴 패턴 설정</button>
          </div>
          <a class="button back-button" href="/">메인 메뉴로</a>

          <script>
            const grid = document.getElementById('ledGrid');
            const gridSize = 8;
            // 2차원 배열로 LED 상태를 관리 (true: 켜짐, false: 꺼짐)
            let ledStates = Array(gridSize).fill(0).map(() => Array(gridSize).fill(false)); 

            // 그리드 셀 생성
            function createGrid() {
              grid.innerHTML = ''; // 기존 그리드 초기화
              for (let r = 0; r < gridSize; r++) {
                for (let c = 0; c < gridSize; c++) {
                  const cell = document.createElement('div');
                  cell.classList.add('grid-cell');
                  cell.dataset.row = r;
                  cell.dataset.col = c;
                  cell.addEventListener('click', toggleLed);
                  grid.appendChild(cell);
                }
              }
            }

            // LED 상태 토글 및 ESP32에 전송
            function toggleLed(event) {
              const row = event.target.dataset.row;
              const col = event.target.dataset.col;
              const currentState = ledStates[row][col];
              const newState = !currentState;

              ledStates[row][col] = newState; // 상태 업데이트
              event.target.classList.toggle('on', newState); // CSS 클래스 토글

              // ESP32에 HTTP 요청 전송
              fetch(`/set_pixel?row=${row}&col=${col}&state=${newState ? 1 : 0}`)
                .then(response => response.text())
                .then(data => console.log(data))
                .catch(error => console.error('Error toggling LED:', error));
            }
            
            // 모든 LED 끄기
            function clearAll() {
                fetch('/clear_display')
                    .then(response => response.text())
                    .then(data => {
                        console.log(data);
                        // 웹 그리드 상태도 업데이트
                        ledStates = Array(gridSize).fill(0).map(() => Array(gridSize).fill(false));
                        updateGridDisplay(); 
                    })
                    .catch(error => console.error('Error clearing display:', error));
            }

            // 얼굴 패턴 설정
            function setFacePattern() {
                // 이전에 계산했던 얼굴 패턴의 열 바이트 값들
                const facePatternCols = [
                    0b11111111, // Col 0
                    0b10011001, // Col 1
                    0b10011101, // Col 2
                    0b11111101, // Col 3
                    0b11111101, // Col 4
                    0b10011101, // Col 5
                    0b10011001, // Col 6
                    0b11111111  // Col 7
                ];

                fetch('/set_pattern', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ columns: facePatternCols })
                })
                .then(response => response.text())
                .then(data => {
                    console.log(data);
                    // 웹 그리드 상태 업데이트
                    fetchLedStates(); 
                })
                .catch(error => console.error('Error setting pattern:', error));
            }

            // 현재 LED 상태를 ESP32에서 가져와 웹 그리드에 반영
            function fetchLedStates() {
                fetch('/get_display_state')
                    .then(response => response.json())
                    .then(data => {
                        for (let c = 0; c < gridSize; c++) {
                            const colByte = data.columns[c];
                            for (let r = 0; r < gridSize; r++) {
                                // MAX7219 라이브러리의 setColumn은 일반적으로
                                // 비트 0이 Row 0, 비트 7이 Row 7을 의미합니다.
                                ledStates[r][c] = ((colByte >> r) & 0x01) === 1; // r번째 비트가 켜져있는지 확인
                            }
                        }
                        updateGridDisplay();
                    })
                    .catch(error => console.error('Error fetching LED states:', error));
            }

            // ledStates 배열에 따라 웹 그리드 업데이트
            function updateGridDisplay() {
                const cells = grid.children;
                for (let r = 0; r < gridSize; r++) {
                    for (let c = 0; c < gridSize; c++) {
                        const cellIndex = r * gridSize + c; // 1차원 배열 인덱스 계산
                        cells[cellIndex].classList.toggle('on', ledStates[r][c]);
                    }
                }
            }

            // 페이지 로드 시 초기화
            window.onload = () => {
              createGrid();
              fetchLedStates(); // ESP32의 현재 상태를 가져와 초기화
              setInterval(fetchLedStates, 3000); // 3초마다 상태 동기화 (선택 사항)
            };
          </script>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
}

// 픽셀 설정 핸들러
void handleSetPixel() {
    if (server.hasArg("row") && server.hasArg("col") && server.hasArg("state")) {
        int row = server.arg("row").toInt();
        int col = server.arg("col").toInt();
        int state = server.arg("state").toInt(); // 0 또는 1

        if (col >= 0 && col < 8 && row >= 0 && row < 8) {
            // MD_MAX72XX는 setColumn 방식으로 제어되므로,
            // 해당 열의 바이트 값에서 특정 비트를 변경합니다.
            // Row 0이 최하위 비트(LSB), Row 7이 최상위 비트(MSB)라고 가정합니다.
            if (state == 1) { // 켜기
                currentDisplayColumns[col] |= (1 << row);
            } else { // 끄기
                currentDisplayColumns[col] &= ~(1 << row);
            }
            display.setColumn(0, col, currentDisplayColumns[col]); // 디스플레이 업데이트
            server.send(200, "text/plain", "LED at (" + String(row) + "," + String(col) + ") set to " + String(state));
            return;
        }
    }
    server.send(400, "text/plain", "Invalid pixel parameters.");
}

// 디스플레이 전체를 끄는 핸들러
void handleClearDisplay() {
    display.clear(); // MAX7219 디스플레이 초기화
    for (int i = 0; i < 8; i++) {
        currentDisplayColumns[i] = 0b00000000; // 저장된 상태도 초기화
    }
    server.send(200, "text/plain", "Display cleared.");
}

// 패턴 설정 핸들러 (JSON POST 요청 받음)
void handleSetPattern() {
    if (server.method() == HTTP_POST && server.hasArg("plain")) {
        String body = server.arg("plain");
        // JSON 파싱 (간단한 파싱)
        // {"columns":[byte0, byte1, ..., byte7]} 형태라고 가정
        int start = body.indexOf("[");
        int end = body.indexOf("]");
        if (start != -1 && end != -1) {
            String colsStr = body.substring(start + 1, end);
            int colIndex = 0;
            while (colsStr.length() > 0 && colIndex < 8) {
                int commaPos = colsStr.indexOf(",");
                String valStr;
                if (commaPos == -1) {
                    valStr = colsStr;
                    colsStr = "";
                } else {
                    valStr = colsStr.substring(0, commaPos);
                    colsStr = colsStr.substring(commaPos + 1);
                }
                currentDisplayColumns[colIndex] = valStr.toInt();
                display.setColumn(0, colIndex, currentDisplayColumns[colIndex]);
                colIndex++;
            }
            server.send(200, "text/plain", "Pattern set successfully.");
            return;
        }
    }
    server.send(400, "text/plain", "Invalid pattern data.");
}


// 현재 디스플레이 상태를 JSON으로 반환하는 핸들러
void handleGetDisplayState() {
    String jsonResponse = "{\"columns\":[";
    for (int i = 0; i < 8; i++) {
        jsonResponse += String(currentDisplayColumns[i]);
        if (i < 7) {
            jsonResponse += ",";
        }
    }
    jsonResponse += "]}";
    server.send(200, "application/json", jsonResponse);
}

// --- MAX7219 웹 컨트롤 관련 추가 코드 끝 ---


// --------------------------------------------------------------------------------------
// ⚡ Setup 및 Loop 함수 ⚡
// --------------------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);

    dht.begin();
    Serial.println("DHT11 sensor initialized.");
    Serial.print("Connecting to WiFi ");
    // ✅ 여기에 WiFi SSID와 비밀번호를 입력하세요.
    WiFi.begin("Test", "worms6964");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    configTime(utcOffsetInSeconds, 0, ntpServer);
    Serial.println("Time synchronized from NTP server.");
    
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
        // Wi-Fi 연결 시간 기록
        char connectTimeBuffer[50];
        strftime(connectTimeBuffer, sizeof(connectTimeBuffer), "%Y년 %m월 %d일 %H:%M:%S", &timeinfo);
        wifiConnectTime = String(connectTimeBuffer);
        Serial.println("WiFi connected at: " + wifiConnectTime);
    } else {
        Serial.println("Failed to get time after WiFi connect.");
        wifiConnectTime = "시간 동기화 실패"; // 시간 동기화 실패 시 메시지
    }

    // ✅ SPIFFS 초기화
    if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
        // return; // 치명적인 오류이므로 여기서 멈추거나 재시작을 고려할 수 있습니다.
    } else {
        Serial.println("SPIFFS mounted successfully.");
    }


    // MP3 플레이어 초기화 (DFRobotDFPlayerMini 라이브러리)
    // 사용자님이 지정하신 핀: RX 10번, TX 9번
    // DFRobotDFPlayerMini 라이브러리는 HardwareSerial 객체를 인자로 받습니다.
    // ESP32의 Serial2를 GPIO10(RX), GPIO9(TX)로 설정합니다.
    Serial.println(F("Attempting to begin DFPlayer Mini on Serial2 (GPIO10 RX, GPIO9 TX)..."));
    // 메시지 변경
    // Serial2.begin(baudrate, config, RX_PIN, TX_PIN)
    Serial2.begin(9600, SERIAL_8N1, 10, 9);
    // DFPlayer Mini 통신 속도

    if (!mp3.begin(Serial2)) { 
        Serial.println(F("Error: Unable to begin DFPlayer Mini. Check connections and SD card!"));
        Serial.println(F("Please ensure DFPlayer TX is connected to ESP32 GPIO10 (RX2) and DFPlayer RX to ESP32 GPIO9 (TX2)."));
        // while(true);
        // 이 줄은 현재 주석 처리되어 있습니다. 웹 서버는 계속 시작됩니다.
    } else { 
        Serial.println(F("DFPlayer Mini initialized.")); 
        mp3.volume(20);
        // 초기 볼륨 설정 (0-30)
    }

    // MAX7219 디스플레이 초기화
    display.begin();
    display.clear();          // 디스플레이 초기화
    display.control(MD_MAX72XX::INTENSITY, 7);  // 밝기 (0~15) 설정

    // 초기 디스플레이 상태를 currentDisplayColumns 배열에 따라 설정
    for (int i = 0; i < MAX_DEVICES; i++) {
        for (int col = 0; col < 8; col++) {
            display.setColumn(i, col, currentDisplayColumns[col]);
        }
    }


    Serial.println("Setting up server routes...");
    server.on("/", handleRoot);
    server.on("/mp3_control", handleMP3Control);
    server.on("/mp3_play", handleMp3Play);
    // server.on("/mp3_vol_up", handleMp3VolUp); // ✅ 제거됨
    // server.on("/mp3_vol_down", handleMp3VolDown); // ✅ 제거됨
    server.on("/mp3_pause", handleMp3Pause);
    server.on("/mp3_resume", handleMp3Resume);
    // ✅ 추가: 볼륨 슬라이더 제어 라우트
    server.on("/set_volume", handleSetVolume);
    server.on("/get_current_volume", handleGetCurrentVolume);

    server.on("/restart_esp", handleRestartESP);

    server.on("/data", handleDataAPI);

    server.on("/learn", handleLearnPage);
    server.on("/learn_start", handleLearnStart);
    server.on("/learn_play_current", handleLearnPlayCurrent);
    server.on("/learn_play_current_data", handleLearnPlayCurrentData);
    server.on("/learn_next", handleLearnNext);
    server.on("/learn_stop", handleLearnStop);

    server.on("/quiz_select", handleQuizSelectPage);
    server.on("/quiz", HTTP_GET, handleQuizPageRender);
    server.on("/quiz_start_action", handleQuizStartAction);
    server.on("/quiz_check", handleQuizCheck);
    server.on("/quiz_play_current", handleQuizPlayCurrent);
    server.on("/quiz_next", handleQuizNext);
    server.on("/quiz_stop", handleQuizStop);

    // MAX7219 웹 컨트롤 라우트 추가
    server.on("/led_matrix_control", handleLedMatrixControl);
    server.on("/set_pixel", handleSetPixel);
    server.on("/clear_display", handleClearDisplay);
    server.on("/set_pattern", HTTP_POST, handleSetPattern); // POST 요청으로 패턴 받음
    server.on("/get_display_state", handleGetDisplayState);

    // ✅ 센서 기록 보기 라우트 추가
    server.on("/sensor_log", handleSensorLog);


    server.begin();
    Serial.println("HTTP server started.");

    // ✅ Setup 시 첫 번째 센서 데이터 기록 (선택 사항)
    logSensorData();
    lastLogTime = millis();
}

void loop() {
    server.handleClient();

    // ✅ 30분마다 센서 데이터 기록
    if (millis() - lastLogTime >= LOG_INTERVAL) {
        logSensorData();
        lastLogTime = millis();
    }
    
    // DFRobotDFPlayerMini는 loop() 함수 호출이 필수는 아니지만,
    // mp3 모듈로부터 응답을 받거나 이벤트를 처리하려면 아래 주석을 해제하세요.
    // mp3.read(); 
}
