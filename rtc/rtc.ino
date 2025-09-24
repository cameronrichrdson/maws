#include <Wire.h>
#include <DS3231.h>
#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

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

// Helper: format numbers with leading zero
String twoDigits(int number) {
  return (number < 10 ? "0" : "") + String(number);
}

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
}

void loop() {
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
}

// === Parse EC string from Atlas probe ===
void parseEC() {
  char sensorstring_array[30];
  sensorstring.toCharArray(sensorstring_array, 30);

  char *val = strtok(sensorstring_array, ","); // first token = EC
  if (val != NULL) {
    conductivity = atof(val); // convert to float
  }
}
