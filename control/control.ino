#include <Wire.h>
#include "MCP23017.h"
#include <WiFiS3.h>
#include <DS3231.h>
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===== Wi-Fi Setup =====
const char ssid[] = "Pump_Controller";
const char pass[] = "password";
WiFiServer server(80);

// ===== MCP23017 Setup =====
MCP23017 mcp;
const int pumpPins[] = {0, 1, 2, 3, 4, 5, 6, 7};
const int numPumps = sizeof(pumpPins) / sizeof(pumpPins[0]);
const float flowRate = 0.014583;  // mL/ms

// ===== Temperature (DS18B20) =====
#define ONE_WIRE_BUS 8
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temperature = NAN;

// ===== Atlas EC =====
#define RX 2
#define TX 7
SoftwareSerial myserial(RX, TX);
String sensorstring = "";
boolean sensor_string_complete = false;
float conductivity = NAN;

// ===== RTC + SD =====
DS3231 myRTC;
const int chipSelect = 10;
bool loggingEnabled = true;
bool century = false;
bool h12Flag;
bool pmFlag;
bool fileInitialized = false;

// ===== Pump Structure =====
struct Pump {
  bool scheduled = false;
  bool running = false;
  bool completed = false;
  unsigned long stopTime = 0;
  float runTime = 0;       // milliseconds
  int scheduledYear = 0;   // RTC year
  int scheduledMonth = 0;  // RTC month
  int scheduledDay = 0;    // RTC day
  int scheduledHour = 0;   // RTC hour
  int scheduledMinute = 0; // RTC minute
  float lastVolume = 0.0;  // mL
};
Pump pumps[numPumps];

// ===== Helpers =====
String twoDigits(int n) { return (n < 10 ? "0" : "") + String(n); }

// ===== Logging =====
void logPumpEvent(int pumpID, float volume) {
  int year = myRTC.getYear();
  int month = myRTC.getMonth(century);           
  int day = myRTC.getDate();
  int hour = myRTC.getHour(h12Flag, pmFlag);    
  int minute = myRTC.getMinute();
  int second = myRTC.getSecond();

  String timeStr = twoDigits(day) + "/" + twoDigits(month) + "/" + String(year + 2000) + "," +
                   twoDigits(hour) + ":" + twoDigits(minute) + ":" + twoDigits(second);

  String logStr = timeStr + "," +
                  String(temperature, 2) + "," +
                  (isnan(conductivity) ? "nan" : String(conductivity, 3)) + "," +
                  String(pumpID + 1) + "," +
                  String(volume, 2);

  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile) {
    static bool headerWritten = false;
    if (!headerWritten) {
      dataFile.println("Date,Time,Temperature,Conductivity,Pump,Volume(mL)");
      headerWritten = true;
    }
    dataFile.println(logStr);
    dataFile.close();
    Serial.println("Pump event logged: " + logStr);
  } else Serial.println("Error opening datalog.txt");
}

// ===== EC Parser =====
void parseEC() {
  char arr[30];
  sensorstring.toCharArray(arr, 30);
  char *val = strtok(arr, ",");
  if (val != NULL) conductivity = atof(val);
}

// ===== Pump Functions =====
void schedulePump(int idx, int year, int month, int day, int hour, int minute, float volume) {
  pumps[idx].scheduled = true;
  pumps[idx].running = false;
  pumps[idx].completed = false;
  pumps[idx].scheduledYear = year;
  pumps[idx].scheduledMonth = month;
  pumps[idx].scheduledDay = day;
  pumps[idx].scheduledHour = hour;
  pumps[idx].scheduledMinute = minute;
  pumps[idx].runTime = volume / flowRate;
  pumps[idx].lastVolume = volume;

  Serial.print("Scheduled Pump "); Serial.print(idx + 1);
  Serial.print(" for "); Serial.print(day); Serial.print("/"); Serial.print(month); Serial.print("/"); Serial.print(year);
  Serial.print(" "); Serial.print(hour); Serial.print(":"); Serial.print(minute);
  Serial.print(" - Volume: "); Serial.println(volume);
}

void stopPump(int idx) {
  mcp.digitalWrite(pumpPins[idx], LOW);
  pumps[idx].scheduled = false;
  pumps[idx].running = false;
  pumps[idx].completed = true;
  Serial.print("Pump "); Serial.print(idx + 1); Serial.println(" stopped manually");
}

// ===== Check Pump Status =====
void checkPumps() {
  int rtcYear = myRTC.getYear() + 2000;
  int rtcMonth = myRTC.getMonth(century);
  int rtcDay = myRTC.getDate();
  int rtcHour = myRTC.getHour(h12Flag, pmFlag);
  int rtcMinute = myRTC.getMinute();
  unsigned long now = millis();

  for (int i = 0; i < numPumps; i++) {
    if (pumps[i].scheduled && !pumps[i].running) {
      // Compare RTC time to scheduled time
      if (rtcYear == pumps[i].scheduledYear &&
          rtcMonth == pumps[i].scheduledMonth &&
          rtcDay == pumps[i].scheduledDay &&
          rtcHour == pumps[i].scheduledHour &&
          rtcMinute == pumps[i].scheduledMinute) {

        mcp.digitalWrite(pumpPins[i], HIGH);
        pumps[i].running = true;
        pumps[i].completed = false;
        pumps[i].stopTime = now + pumps[i].runTime;
        Serial.print("Pump "); Serial.print(i + 1); Serial.println(" started");
      }
    }

    if (pumps[i].running && now >= pumps[i].stopTime) {
      mcp.digitalWrite(pumpPins[i], LOW);
      pumps[i].running = false;
      pumps[i].scheduled = false;
      pumps[i].completed = true;
      Serial.print("Pump "); Serial.print(i + 1); Serial.println(" completed");
      if (loggingEnabled) logPumpEvent(i, pumps[i].lastVolume);
    }
  }
}

// ===== Web Helpers =====
String getParam(const String &req, const String &key) {
  int keyIndex = req.indexOf(key + "=");
  if (keyIndex == -1) return "";
  int valueStart = keyIndex + key.length() + 1;
  int valueEnd = req.indexOf('&', valueStart);
  if (valueEnd == -1) valueEnd = req.indexOf('\n', valueStart);
  return req.substring(valueStart, valueEnd);
}

String readRequest(WiFiClient &client) {
  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (c == '\n') break;
    }
  }
  while (client.available()) request += (char)client.read();
  return request;
}

// ===== Process Web Request =====
void processRequest(String req, WiFiClient &client) {
  Serial.println("Incoming request:"); Serial.println(req);

  // ===== RTC TIME ENDPOINT =====
  if (req.indexOf("GET /time") >= 0) {
    int year = myRTC.getYear() + 2000;
    int month = myRTC.getMonth(century);
    int day = myRTC.getDate();
    int hour = myRTC.getHour(h12Flag, pmFlag);
    int minute = myRTC.getMinute();
    int second = myRTC.getSecond();

    String timeStr = twoDigits(day) + "/" + twoDigits(month) + "/" + String(year) + " " +
                     twoDigits(hour) + ":" + twoDigits(minute) + ":" + twoDigits(second);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain\n");
    client.println(timeStr);
    return;
  }

  // ===== Run Pumps =====
  if (req.indexOf("POST /run") >= 0) {
    for (int i = 0; i < numPumps; i++) {
      String pumpStr = "pump" + String(i);
      if (req.indexOf(pumpStr + "=") >= 0) {
        int year = getParam(req, "year" + String(i)).toInt();
        int month = getParam(req, "month" + String(i)).toInt();
        int day = getParam(req, "day" + String(i)).toInt();
        int hour = getParam(req, "hour" + String(i)).toInt();
        int minute = getParam(req, "minute" + String(i)).toInt();
        float volume = getParam(req, "volume" + String(i)).toFloat();
        schedulePump(i, year, month, day, hour, minute, volume);
      }
    }
  }

  // ===== Cancel Pumps =====
  if (req.indexOf("POST /cancel") >= 0) {
    for (int i = 0; i < numPumps; i++) {
      String pumpStr = "pump" + String(i);
      if (req.indexOf(pumpStr + "=") >= 0) stopPump(i);
    }
  }

  // ===== Build Web Page =====
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html\n");
  client.println("<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>body{font-family:Arial;}form{margin-bottom:20px;}button{margin-top:5px;}</style></head><body>");
  client.println("<h2>8-Pump Scheduler</h2>");

  // RTC Time Display
  client.println("<div><b>RTC Time:</b> <span id='rtcTime'>Loading...</span></div><br>");
  client.println("<script>");
  client.println("function updateTime(){fetch('/time').then(r=>r.text()).then(t=>{document.getElementById('rtcTime').innerText=t;});}");
  client.println("updateTime(); setInterval(updateTime,60000);");
  client.println("</script>");

  for (int i = 0; i < numPumps; i++) {
    client.println("<form method='POST' action='/run'>");
    client.println("<b>Pump " + String(i + 1) + "</b><br>");
    client.println("<input type='hidden' name='pump" + String(i) + "' value='1'>");
    client.println("Year: <input type='number' name='year" + String(i) + "' min='2025' max='2100' required><br>");
    client.println("Month: <input type='number' name='month" + String(i) + "' min='1' max='12' required><br>");
    client.println("Day: <input type='number' name='day" + String(i) + "' min='1' max='31' required><br>");
    client.println("Hour: <input type='number' name='hour" + String(i) + "' min='0' max='23' required><br>");
    client.println("Minute: <input type='number' name='minute" + String(i) + "' min='0' max='59' required><br>");
    client.println("Volume (mL): <input type='number' name='volume" + String(i) + "' step='any' min='0.1' required><br>");
    client.println("<button type='submit'>Schedule Pump</button>");
    client.println("</form>");

    if (pumps[i].scheduled || pumps[i].running) {
      client.println("<form method='POST' action='/cancel'>");
      client.println("<input type='hidden' name='pump" + String(i) + "' value='1'>");
      client.println("<button type='submit'>Cancel Pump " + String(i + 1) + "</button>");
      client.println("</form>");
    }

    if (pumps[i].running)
      client.println("Status: <b>Running</b> - Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else if (pumps[i].scheduled)
      client.println("Status: <b>Scheduled</b> - Scheduled: " +
                     String(pumps[i].scheduledDay) + "/" + String(pumps[i].scheduledMonth) + " " +
                     String(pumps[i].scheduledHour) + ":" + String(pumps[i].scheduledMinute) +
                     " - Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else if (pumps[i].completed)
      client.println("Status: <b>Completed</b> - Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else
      client.println("Status: Idle<br><br>");
  }

  client.println("</body></html>");
}

// ===== Wi-Fi Info =====
void printWifiStatus() {
  Serial.print("AP IP address: "); Serial.println(WiFi.localIP());
}

// ===== Setup =====
void setup() {
  Serial.begin(9600);
  myserial.begin(9600);
  sensors.begin();
  Wire.begin();
  myRTC.setClockMode(false);

  // SD Card
  if (!SD.begin(chipSelect)) {
    Serial.println("SD init failed!");
    while (1);
  }
  Serial.println("SD card ready.");

  // MCP23017
  mcp.begin(7);
  for (int i = 0; i < numPumps; i++) {
    mcp.pinMode(pumpPins[i], OUTPUT);
    mcp.digitalWrite(pumpPins[i], LOW);
  }

  // Wi-Fi AP
  Serial.print("Starting Wi-Fi AP: "); Serial.println(ssid);
  if (!WiFi.beginAP(ssid, pass)) {
    Serial.println("Wi-Fi AP failed!");
    while (1);
  }

  while (WiFi.status() != WL_AP_LISTENING) { delay(500); Serial.print("."); }
  Serial.println(); Serial.println("Wi-Fi AP ready");

  server.begin();
  printWifiStatus();

  Serial.println("Pump Controller Ready!");
}

// ===== Loop =====
void loop() {
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);

  if (myserial.available() > 0) {
    char c = myserial.read();
    sensorstring += c;
    if (c == '\r') sensor_string_complete = true;
  }
  if (sensor_string_complete) { parseEC(); sensorstring = ""; sensor_string_complete = false; }

  checkPumps();

  WiFiClient client = server.available();
  if (client) { String req = readRequest(client); processRequest(req, client); client.stop(); }
}
