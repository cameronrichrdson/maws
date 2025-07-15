#include <WiFiS3.h>

const char ssid[] = "Pump_Controller";
const char pass[] = "password";
WiFiServer server(80);

// === Pump Configuration ===
const int pumpPins[] = {2, 3, 4, 5, 6, 7, 8, 9};
const int numPumps = sizeof(pumpPins) / sizeof(pumpPins[0]);
const float flowRate = 0.014583;  // mL/ms

struct Pump {
  bool scheduled = false;
  unsigned long startTime = 0;
  unsigned long runTime = 0;
  int lastDelay = 0;
  float lastVolume = 0.0;
};

Pump pumps[numPumps];

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < numPumps; i++) {
    pinMode(pumpPins[i], OUTPUT);
    digitalWrite(pumpPins[i], LOW);
  }

  if (!WiFi.beginAP(ssid, pass)) {
    Serial.println("Failed to start access point");
    while (true);
  }

  server.begin();
  printWifiStatus();
}

void loop() {
  unsigned long now = millis();

  // Check each pump
  for (int i = 0; i < numPumps; i++) {
    if (pumps[i].scheduled && now >= pumps[i].startTime) {
      digitalWrite(pumpPins[i], HIGH);
      delay(pumps[i].runTime);
      digitalWrite(pumpPins[i], LOW);
      pumps[i].scheduled = false;
      Serial.print("Pump ");
      Serial.print(i + 1);
      Serial.println(" completed");
    }
  }

  WiFiClient client = server.available();
  if (client) {
    String req = readRequest(client);
    processRequest(req, client);
    client.stop();
  }
}

String readRequest(WiFiClient& client) {
  String request = "";
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
  }

  // Check for POST body
  while (client.available()) {
    request += (char)client.read();
  }
  return request;
}

void processRequest(const String& req, WiFiClient& client) {
  Serial.println("Incoming request:");
  Serial.println(req);

  // Handle pump cancel
  if (req.indexOf("POST /cancel") != -1) {
    int pumpIndex = getParam(req, "pump").toInt();
    if (pumpIndex >= 0 && pumpIndex < numPumps) {
      pumps[pumpIndex].scheduled = false;
      pumps[pumpIndex].lastDelay = 0;
      pumps[pumpIndex].lastVolume = 0.0;
      Serial.print("Canceled Pump ");
      Serial.println(pumpIndex + 1);
    }
  }

  // Handle scheduling
  if (req.indexOf("POST /run") != -1) {
    int pumpIndex = getParam(req, "pump").toInt();
    if (pumpIndex >= 0 && pumpIndex < numPumps) {
      int delayMin = getParam(req, "delay").toInt();
      float volume = getParam(req, "volume").toFloat();
      if (volume > 0) {
        pumps[pumpIndex].runTime = (unsigned long)(volume / flowRate);
        pumps[pumpIndex].startTime = millis() + delayMin * 60000UL;
        pumps[pumpIndex].scheduled = true;
        pumps[pumpIndex].lastDelay = delayMin;
        pumps[pumpIndex].lastVolume = volume;
        Serial.print("Scheduled Pump ");
        Serial.println(pumpIndex + 1);
      }
    }
  }

  // Build and send response
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html\n");
  client.println("<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<style>body{font-family:Arial;}form{margin-bottom:20px;}button{margin-top:5px;}</style></head><body>");
  client.println("<h2>8-Pump Web Scheduler</h2>");

  for (int i = 0; i < numPumps; i++) {
    client.println("<form method=\"POST\" action=\"/run\">");
    client.println("<b>Pump " + String(i + 1) + "</b><br>");
    client.println("<input type=\"hidden\" name=\"pump\" value=\"" + String(i) + "\">");
    client.println("Start Delay (min): <input name=\"delay\" type=\"number\" min=\"0\" required><br>");
    client.println("Volume (mL): <input name=\"volume\" type=\"number\" step=\"any\" min=\"1\" required><br>");
    client.println("<button type=\"submit\">Schedule Pump</button>");
    client.println("</form>");

    if (pumps[i].scheduled) {
      client.println("<form method=\"POST\" action=\"/cancel\">");
      client.println("<input type=\"hidden\" name=\"pump\" value=\"" + String(i) + "\">");
      client.println("<button type=\"submit\">Cancel Pump " + String(i + 1) + "</button>");
      client.println("</form>");
      client.println("Status: Scheduled - Delay: " + String(pumps[i].lastDelay) + 
                     " min, Volume: " + String(pumps[i].lastVolume, 2) + " mL<br><br>");
    } else {
      client.println("Status: Idle<br><br>");
    }
  }

  client.println("</body></html>");
}

String getParam(const String& req, const String& key) {
  int keyIndex = req.indexOf(key + "=");
  if (keyIndex == -1) return "";
  int valueStart = keyIndex + key.length() + 1;
  int valueEnd = req.indexOf('&', valueStart);
  if (valueEnd == -1) valueEnd = req.indexOf('\n', valueStart);
  return req.substring(valueStart, valueEnd);
}

void printWifiStatus() {
  Serial.print("AP SSID: ");
  Serial.println(ssid);
  Serial.print("Browse to: http://");
  Serial.println(WiFi.localIP());
}
