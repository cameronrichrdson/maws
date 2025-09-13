#include "RTC.h"
#include <SD.h>
#include <SPI.h>

const int PIN_ON_INTERRUPT  = D7;
const int chipSelect = 10; // SD card CS pin

bool periodicFlag = false;
bool alarmFlag = false;
bool loggingEnabled = true; // Flag to control logging

// Callback function for periodic interrupt
void periodic_cbk() {
  periodicFlag = true;
}

// Callback function for alarm interrupt
void alarm_cbk() {
  alarmFlag = true;
}

// Helper function to format numbers with leading zero
String twoDigits(int number) {
  return (number < 10 ? "0" : "") + String(number);
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_ON_INTERRUPT, OUTPUT);

  // Initialize the RTC
  RTC.begin();

  // Set initial time ONLY ONCE if RTC is not running
  // if (RTC.isRunning()) {
  //   // Example time: 12 September 2025, 14:52:00
  //   RTCTime mytime(12, Month::SEPTEMBER, 2025, 16, 56, 0, DayOfWeek::FRIDAY, SaveLight::SAVING_TIME_ACTIVE);
  //   RTC.setTime(mytime);
  //   //Serial.println("RTC time set to 12/09/2025 14:52:00");
  // }

  // Initialize SD card
  if (!SD.begin(chipSelect)) {
    Serial.println("SD card initialization failed!");
    while (1);
  }
  Serial.println("SD card initialized.");

  // Periodic and alarm callbacks
  if (!RTC.setPeriodicCallback(periodic_cbk, Period::ONCE_EVERY_2_SEC)) {
    Serial.println("ERROR: periodic callback not set");
  }

  RTCTime alarmtime;
  alarmtime.setSecond(35);
  AlarmMatch am;
  am.addMatchSecond();

  if (!RTC.setAlarmCallback(alarm_cbk, alarmtime, am)) {
    Serial.println("ERROR: alarm callback not set");
  }

  Serial.println("Commands: STOP, START, RESET, SETTIME dd/mm/yyyy hh:mm:ss");
}

void loop() {
  static bool status = false;
  RTCTime currenttime;

  // Check for Serial input
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    // Convert to uppercase for simple command matching
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
      if (SD.exists("datalog.txt")) {
        SD.remove("datalog.txt");
      }
      Serial.println("Log file reset.");
    }
    else if (cmdUpper.startsWith("SETTIME")) {
      // Expected format: SETTIME dd/mm/yyyy hh:mm:ss
      int d, m, y, h, mi, s;
      if (sscanf(cmd.c_str(), "SETTIME %d/%d/%d %d:%d:%d", &d, &m, &y, &h, &mi, &s) == 6) {
        RTCTime t(d, Month(m), y, h, mi, s, DayOfWeek::FRIDAY, SaveLight::SAVING_TIME_ACTIVE);
        RTC.setTime(t);
        Serial.println("RTC updated via Serial.");
      } else {
        Serial.println("Invalid format. Use: SETTIME dd/mm/yyyy hh:mm:ss");
      }
    }
  }

  if(periodicFlag){
    Serial.println("PERIODIC INTERRUPT");
    static bool clb_st = false;
    digitalWrite(PIN_ON_INTERRUPT, clb_st ? HIGH : LOW);
    clb_st = !clb_st;
    periodicFlag = false;
  }

  if(alarmFlag){
    Serial.println("ALARM INTERRUPT");
    alarmFlag = false;
  }

  // Toggle status every second
  status = !status;
  if(status) {
    RTC.getTime(currenttime);

    // Format the time string with leading zeros
    String timeStr = String(currenttime.getDayOfMonth()) + "/" +
                     String(Month2int(currenttime.getMonth())) + "/" +
                     String(currenttime.getYear()) + " - " +
                     twoDigits(currenttime.getHour()) + ":" +
                     twoDigits(currenttime.getMinutes()) + ":" +
                     twoDigits(currenttime.getSeconds());

    Serial.println("Current time: " + timeStr);

    // Write time to SD card if logging enabled
    if (loggingEnabled) {
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
      if (dataFile) {
        dataFile.println(timeStr);
        dataFile.close();
      } else {
        Serial.println("Error opening datalog.txt");
      }
    }

    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  delay(1000);
}
