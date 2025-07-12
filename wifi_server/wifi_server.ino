#include "WiFiS3.h"

char ssid[] = "Pump_Controller";
char pass[] = "password";
WiFiServer server(80);

const int pump1Pin = 3;
const int pump2Pin = 2;
const float flowRate = 0.014583;  // mL/ms

// Schedule state for each pump
unsigned long pump1StartTime = 0;
unsigned long pump1RunTime = 0;
bool pump1Scheduled = false;

unsigned long pump2StartTime = 0;
unsigned long pump2RunTime = 0;
bool pump2Scheduled = false;

void setup() {
  Serial.begin(9600);
  pinMode(pump1Pin, OUTPUT);
  pinMode(pump2Pin, OUTPUT);
  digitalWrite(pump1Pin, LOW);
  digitalWrite(pump2Pin, LOW);

  if (!WiFi.beginAP(ssid, pass)) {
    Serial.println("Failed to start AP");
    while (true);
  }

  server.begin();
  printWifiStatus();
}

void loop() {
  unsigned long now = millis();

  // Pump 1 control
  if (pump1Scheduled && now >= pump1StartTime) {
    digitalWrite(pump1Pin, HIGH);
    delay(pump1RunTime);
    digitalWrite(pump1Pin, LOW);
    pump1Scheduled = false;
    Serial.println("Pump 1 completed");
  }

  // Pump 2 control
  if (pump2Scheduled && now >= pump2StartTime) {
    digitalWrite(pump2Pin, HIGH);
    delay(pump2RunTime);
    digitalWrite(pump2Pin, LOW);
    pump2Scheduled = false;
    Serial.println("Pump 2 completed");
  }

  WiFiClient client = server.available();
  if (client) {
    Serial.println("New client connected");
    String request = "", currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            processRequest(request, client);
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }
}

void processRequest(String req, WiFiClient &client) {
  Serial.println("Request: ");
  Serial.println(req);

  // Cancel commands
  if (req.indexOf("GET /cancel1") != -1) {
    pump1Scheduled = false;
  } else if (req.indexOf("GET /cancel2") != -1) {
    pump2Scheduled = false;
  }

  // Pump 1 schedule
  if (req.indexOf("GET /run1") != -1) {
    int delayMin = getParam(req, "delay").toInt();
    float volume = getParam(req, "volume").toFloat();
    if (volume > 0) {
      pump1RunTime = (unsigned long)(volume / flowRate);
      pump1StartTime = millis() + delayMin * 60000UL;
      pump1Scheduled = true;
      Serial.println("Scheduled Pump 1");
    }
  }

  // Pump 2 schedule
  if (req.indexOf("GET /run2") != -1) {
    int delayMin = getParam(req, "delay").toInt();
    float volume = getParam(req, "volume").toFloat();
    if (volume > 0) {
      pump2RunTime = (unsigned long)(volume / flowRate);
      pump2StartTime = millis() + delayMin * 60000UL;
      pump2Scheduled = true;
      Serial.println("Scheduled Pump 2");
    }
  }

  // Webpage
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html\n");
  client.println("<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<style>body{font-family:Arial;}form{margin-bottom:20px;}</style></head><body>");
  client.println("<h2>Dual Pump Scheduler</h2>");

  client.println("<form action=\"/run1\" method=\"GET\">");
  client.println("<b>Pump 1</b><br>");
  client.println("Start Delay (min): <input name=\"delay\" type=\"number\" min=\"0\" required><br>");
  client.println("Volume (mL): <input name=\"volume\" type=\"number\" step=\"any\" min=\"1\" required><br>");
  client.println("<input type=\"submit\" value=\"Schedule Pump 1\">");
  if (pump1Scheduled) {
    client.println(" <a href=\"/cancel1\">Cancel</a>");
    client.println("<br>Status: Scheduled");
  } else {
    client.println("<br>Status: Idle");
  }
  client.println("</form>");

  client.println("<form action=\"/run2\" method=\"GET\">");
  client.println("<b>Pump 2</b><br>");
  client.println("Start Delay (min): <input name=\"delay\" type=\"number\" min=\"0\" required><br>");
  client.println("Volume (mL): <input name=\"volume\" type=\"number\" step=\"any\" min=\"1\" required><br>");
  client.println("<input type=\"submit\" value=\"Schedule Pump 2\">");
  if (pump2Scheduled) {
    client.println(" <a href=\"/cancel2\">Cancel</a>");
    client.println("<br>Status: Scheduled");
  } else {
    client.println("<br>Status: Idle");
  }
  client.println("</form>");

  client.println("</body></html>");
}

String getParam(String req, String key) {
  int idx = req.indexOf(key + "=");
  if (idx == -1) return "";
  int end = req.indexOf('&', idx);
  if (end == -1) end = req.indexOf(' ', idx);
  return req.substring(idx + key.length() + 1, end);
}

void printWifiStatus() {
  Serial.print("AP SSID: ");
  Serial.println(ssid);
  Serial.print("Browse to: http://");
  Serial.println(WiFi.localIP());
}
