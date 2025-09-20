#include <Wire.h>
#include "MCP23017.h"
#include <WiFiS3.h>

// ===== Wi-Fi Setup =====
const char ssid[] = "Pump_Controller";
const char pass[] = "password";
WiFiServer server(80);

// ===== MCP23017 Setup =====
MCP23017 mcp;
const int pumpPins[] = {0, 1, 2, 3, 4, 5, 6, 7}; // MCP23017 pins PA0-PA7
const int numPumps = sizeof(pumpPins) / sizeof(pumpPins[0]);
const float flowRate = 0.014583;  // mL/ms

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

// ===== Function Declarations =====
void schedulePump(int idx, unsigned long delayMs, float volume);
void stopPump(int idx);
void processRequest(String req, WiFiClient &client);
String getParam(const String &req, const String &key);
void printWifiStatus();
String readRequest(WiFiClient &client);

void setup() {
  Serial.begin(9600);
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
