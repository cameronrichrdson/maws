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
const int pumpPins[] = {0, 1, 2, 3, 4, 5, 6, 7}; // MCP23017 pins PA0-PA7
const int numPumps = sizeof(pumpPins) / sizeof(pumpPins[0]);
const float flowRate = 0.014583;  // mL/ms

// === Temperature sensor (DS18B20) ===
#define ONE_WIRE_BUS 8
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temperature;

// === Atlas Scientific EC Setup ===
#define RX 2
#define TX 7
SoftwareSerial myserial(RX, TX);
String inputstring = "";
String sensorstring = "";
boolean sensor_string_complete = false;
float conductivity = NAN;

// === RTC + SD Setup ===
DS3231 myRTC;
const int chipSelect = 10;    // SD card CS pin
bool loggingEnabled = true;   // Control logging
bool century = false;
bool h12Flag;
bool pmFlag;
bool fileInitialized = false; // To track header creation

// ===== Pump Structure =====
struct Pump {
  bool scheduled = false;
  bool running = false;
  bool completed = false;
  unsigned long startTime = 0;
  unsigned long runTime = 0;
  unsigned long stopTime = 0;
  int lastDelay = 0;       // minutes
  float lastVolume = 0.0;  // mL
};
Pump pumps[numPumps];

String twoDigits(int number) {
  return (number < 10 ? "0" : "") + String(number);
}
// ===== Function Declarations =====
void schedulePump(int idx, unsigned long delayMs, float volume);
void stopPump(int idx);
void processRequest(String req, WiFiClient &client);
String getParam(const String &req, const String &key);
void printWifiStatus();
// === Parse EC string from Atlas probe ===
void parseEC() {
  char sensorstring_array[30];
  sensorstring.toCharArray(sensorstring_array, 30);

  char *val = strtok(sensorstring_array, ","); // first token = EC
  if (val != NULL) {
    conductivity = atof(val); // convert to float
  }
}
String readRequest(WiFiClient &client);


void setup() {
  Serial.begin(9600);
  myserial.begin(9600);
  sensors.begin();
  inputstring.reserve(10);
  sensorstring.reserve(30);

  Wire.begin();
  myRTC.setClockMode(false); // Use 24h mode

  // Initialize SD card
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    while (1);
  }
  Serial.println("SD card initialized.");
  Serial.println("Commands: STOP, START, RESET, SETTIME dd/mm/yyyy hh:mm:ss");
  while (!Serial) {}

  // Initialize MCP23017 at default address 0x27 (param=7)
  mcp.begin(7);
  for (int i = 0; i < numPumps; i++) {
    mcp.pinMode(pumpPins[i], OUTPUT);
    mcp.digitalWrite(pumpPins[i], LOW); // Start OFF (LOW-level trigger)
  }

  // Start Wi-Fi Access Point
  if (!WiFi.beginAP(ssid, pass)) {
    Serial.println("Failed to start access point");
    while (true);
  }
  server.begin();
  printWifiStatus();
  Serial.println("Pump Controller Ready!");
}

void loop() {
  unsigned long now = millis();

 // === Read DS18B20 ===
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);

  // === Handle Serial Commands ===
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    String cmdUpper = cmd;
    cmdUpper.toUpperCase();

    if (cmdUpper == "STOP") {
      loggingEnabled = false;
      Serial.println("Logging stopped.");
    } 
    else if (cmdUpper == "START") {
      loggingEnabled = true;
      Serial.println("Logging started.");
    } 
    else if (cmdUpper == "RESET") {
      if (SD.exists("datalog.txt")) SD.remove("datalog.txt");
      Serial.println("Log file reset.");
      fileInitialized = false;
    } 
    else if (cmdUpper.startsWith("SETTIME")) {
      int d, m, y, h, mi, s;
      if (sscanf(cmd.c_str(), "SETTIME %d/%d/%d %d:%d:%d", &d, &m, &y, &h, &mi, &s) == 6) {
        myRTC.setClockMode(false);
        myRTC.setYear(y - 2000);
        myRTC.setMonth(m);
        myRTC.setDate(d);
        myRTC.setHour(h);
        myRTC.setMinute(mi);
        myRTC.setSecond(s);
        Serial.println("RTC updated via Serial.");
      } else {
        Serial.println("Invalid format. Use: SETTIME dd/mm/yyyy hh:mm:ss");
      }
    }
  }

  // === Handle Atlas Scientific EC Input ===
  if (myserial.available() > 0) {
    char inchar = (char)myserial.read();
    sensorstring += inchar;
    if (inchar == '\r') sensor_string_complete = true;
  }

  if (sensor_string_complete == true) {
    if (isdigit(sensorstring[0]) == false) {
      Serial.println(sensorstring);   // not numeric, print raw
    } else {
      parseEC();                      // parse conductivity
    }
    sensorstring = "";
    sensor_string_complete = false;
  }

  // === Get RTC Time ===
  int year = myRTC.getYear();
  int month = myRTC.getMonth(century);
  int day = myRTC.getDate();
  int hour = myRTC.getHour(h12Flag, pmFlag);
  int minute = myRTC.getMinute();
  int second = myRTC.getSecond();

  String timeStr = twoDigits(day) + "/" +
                   twoDigits(month) + "/" +
                   String(year + 2000) + "," +
                   twoDigits(hour) + ":" +
                   twoDigits(minute) + ":" +
                   twoDigits(second);

  // === Build log string ===
  String logStr = timeStr + "," +
                  String(temperature, 2) + "," +
                  (isnan(conductivity) ? "nan" : String(conductivity, 3));

  Serial.println(logStr);

  // === Check if file needs header ===
  if (!fileInitialized) {
    if (!SD.exists("datalog.txt")) {
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
      if (dataFile) {
        dataFile.println("Date,Time,Temperature,Conductivity");
        dataFile.close();
        Serial.println("Log file created with header.");
      } else {
        Serial.println("Error creating datalog.txt");
      }
    }
    fileInitialized = true;
  }

  // === Write to SD ===
  if (loggingEnabled) {
    File dataFile = SD.open("datalog.txt", FILE_WRITE);
    if (dataFile) {
      dataFile.println(logStr);
      dataFile.close();
    } else {
      Serial.println("Error opening datalog.txt");
    }
  }

  delay(1000);



  // Check scheduled pumps
  for (int i = 0; i < numPumps; i++) {
    if (pumps[i].scheduled && !pumps[i].running && now >= pumps[i].startTime) {
      mcp.digitalWrite(pumpPins[i], HIGH);  // Start pump (HIGH-level trigger)
      pumps[i].running = true;
      pumps[i].stopTime = now + pumps[i].runTime;
      Serial.print("Pump ");
      Serial.print(i + 1);
      Serial.println(" started");
    }

    if (pumps[i].running && now >= pumps[i].stopTime) {
      mcp.digitalWrite(pumpPins[i], LOW);   // Stop pump
      pumps[i].scheduled = false;
      pumps[i].running = false;
      pumps[i].completed = true;
      Serial.print("Pump ");
      Serial.print(i + 1);
      Serial.println(" completed");
    }
  }

  // Handle incoming web requests
  WiFiClient client = server.available();
  if (client) {
    String req = readRequest(client);
    processRequest(req, client);
    client.stop();
  }
}

// ===== Schedule Pump =====
void schedulePump(int idx, unsigned long delayMs, float volume) {
  pumps[idx].scheduled = true;
  pumps[idx].running = false;
  pumps[idx].completed = false;
  pumps[idx].startTime = millis() + delayMs;
  pumps[idx].runTime = volume / flowRate;
  pumps[idx].lastDelay = delayMs / 60000UL;
  pumps[idx].lastVolume = volume;
  Serial.print("Scheduled Pump ");
  Serial.print(idx + 1);
  Serial.print(" - Delay: ");
  Serial.print(pumps[idx].lastDelay);
  Serial.print(" min, Volume: ");
  Serial.print(pumps[idx].lastVolume);
  Serial.println(" mL");
}

// ===== Stop Pump Immediately =====
void stopPump(int idx) {
  mcp.digitalWrite(pumpPins[idx], LOW); // Stop pump
  pumps[idx].scheduled = false;
  pumps[idx].running = false;
  pumps[idx].completed = true;
  Serial.print("Pump ");
  Serial.print(idx + 1);
  Serial.println(" stopped manually");
}

// ===== Process Web Request =====
void processRequest(String req, WiFiClient &client) {
  Serial.println("Incoming request:");
  Serial.println(req);

  // POST /run
  if (req.indexOf("POST /run") >= 0) {
    for (int i = 0; i < numPumps; i++) {
      String pumpStr = "pump" + String(i);
      if (req.indexOf(pumpStr + "=") >= 0) {
        int delayMin = getParam(req, "delay" + String(i)).toInt();
        float volume = getParam(req, "volume" + String(i)).toFloat();
        schedulePump(i, delayMin * 60000UL, volume);
      }
    }
  }

  // POST /cancel
  if (req.indexOf("POST /cancel") >= 0) {
    for (int i = 0; i < numPumps; i++) {
      String pumpStr = "pump" + String(i);
      if (req.indexOf(pumpStr + "=") >= 0) stopPump(i);
    }
  }

  // Build HTML page
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html\n");
  client.println("<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>body{font-family:Arial;}form{margin-bottom:20px;}button{margin-top:5px;}</style></head><body>");
  client.println("<h2>8-Pump Scheduler</h2>");

  for (int i = 0; i < numPumps; i++) {
    // Schedule form
    client.println("<form method='POST' action='/run'>");
    client.println("<b>Pump " + String(i + 1) + "</b><br>");
    client.println("<input type='hidden' name='pump" + String(i) + "' value='1'>");
    client.println("Start Delay (min): <input type='number' name='delay" + String(i) + "' min='0' required><br>");
    client.println("Volume (mL): <input type='number' name='volume" + String(i) + "' step='any' min='0.1' required><br>");
    client.println("<button type='submit'>Schedule Pump</button>");
    client.println("</form>");

    // Cancel button
    if (pumps[i].scheduled || pumps[i].running) {
      client.println("<form method='POST' action='/cancel'>");
      client.println("<input type='hidden' name='pump" + String(i) + "' value='1'>");
      client.println("<button type='submit'>Cancel Pump " + String(i + 1) + "</button>");
      client.println("</form>");
    }

    // Status
    if (pumps[i].running)
      client.println("Status: <b>Running</b> - Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else if (pumps[i].scheduled)
      client.println("Status: <b>Scheduled</b> - Delay: " + String(pumps[i].lastDelay) + " min, Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else if (pumps[i].completed)
      client.println("Status: <b>Completed</b> - Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    else
      client.println("Status: Idle<br><br>");
  }

  client.println("</body></html>");
}

// ===== Extract Parameter =====
String getParam(const String &req, const String &key) {
  int keyIndex = req.indexOf(key + "=");
  if (keyIndex == -1) return "";
  int valueStart = keyIndex + key.length() + 1;
  int valueEnd = req.indexOf('&', valueStart);
  if (valueEnd == -1) valueEnd = req.indexOf('\n', valueStart);
  return req.substring(valueStart, valueEnd);
}




// ===== Read Request =====
String readRequest(WiFiClient &client) {
  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (c == '\n') break;
    }
  }
  // Read any remaining POST body
  while (client.available()) request += (char)client.read();
  return request;
}

// ===== Wi-Fi Info =====
void printWifiStatus() {
  Serial.print("AP IP address: ");
  Serial.println(WiFi.localIP());
}
