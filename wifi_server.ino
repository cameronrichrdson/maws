#include "WiFiS3.h"

//**************************************************
//        SETUP          ///
char ssid[] = "Pump_Controller";   // Name of the AP
char pass[] = "password";          // At least 8 characters

WiFiServer server(80);

// Define the pump control pin
const int pumpPin = 9;

// Default timing values (in milliseconds)
const unsigned long defaultStartDelay = 0;
const unsigned long defaultRunTime    = 5000;
//**************************************************

void setup() {
  Serial.begin(9600);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);  // Ensure pump is off at startup

  // Start Wi-Fi Access Point
  if (!WiFi.beginAP(ssid, pass)) {
    Serial.println("Failed to start AP");
    while (true);
  }

  server.begin();
  printWifiStatus();
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("New client connected");
    String currentLine = "";
    String request = "";

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
  Serial.println("Received request:");
  Serial.println(req);

  if (req.indexOf("GET /run") != -1) {
    unsigned long startDelay = defaultStartDelay;
    unsigned long runTime = defaultRunTime;

    int posDelay = req.indexOf("startDelay=");
    if (posDelay != -1) {
      int endPos = req.indexOf('&', posDelay);
      if (endPos == -1) endPos = req.indexOf(' ', posDelay);
      String delayStr = req.substring(posDelay + 11, endPos);
      unsigned long tempDelay = delayStr.toInt();
      if (tempDelay > 0) {
        startDelay = tempDelay;
      }
    }

    int posDur = req.indexOf("duration=");
    if (posDur != -1) {
      int endPos = req.indexOf(' ', posDur);
      String durStr = req.substring(posDur + 9, endPos);
      unsigned long tempRun = durStr.toInt();
      if (tempRun > 0) {
        runTime = tempRun;
      }
    }

    Serial.print("Pump will start after ");
    Serial.print(startDelay);
    Serial.print(" ms and run for ");
    Serial.print(runTime);
    Serial.println(" ms");

    delay(startDelay);
    digitalWrite(pumpPin, HIGH);
    delay(runTime);
    digitalWrite(pumpPin, LOW);
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"></head>");
  client.println("<body>");
  client.println("<h2>Arduino Pump Controller</h2>");
  client.println("<p>Specify when to fire the pump and for how long.</p>");
  client.println("<form action=\"/run\" method=\"GET\">");
  client.println("Start Delay (ms):<br>");
  client.println("<input type=\"number\" name=\"startDelay\" placeholder=\"0\" min=\"0\"><br><br>");
  client.println("Run Time (ms):<br>");
  client.println("<input type=\"number\" name=\"duration\" placeholder=\"5000\" min=\"100\"><br><br>");
  client.println("<input type=\"submit\" value=\"Fire Pump\">");
  client.println("</form>");
  client.println("<p>Defaults: 0 ms delay and 5000 ms run time if left blank.</p>");
  client.println("</body>");
  client.println("</html>");
  client.println();
}

void printWifiStatus() {
  Serial.print("Access Point SSID: ");
  Serial.println(ssid);

  IPAddress ip = WiFi.localIP();
  Serial.print("Connect to: ");
  Serial.println(ssid);
  Serial.print("Then open browser to: http://");
  Serial.println(ip);
}
