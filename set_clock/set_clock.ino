#include <DS3231.h>
#include <Wire.h>

DS3231 myRTC;

// Temporary storage for setting
byte year, month, date, dOW, hour, minute, second;

void getDateStuff(byte& year, byte& month, byte& date, byte& dOW,
                  byte& hour, byte& minute, byte& second) {
    boolean gotString = false;
    char inChar;
    byte temp1, temp2;
    char inString[20];
    byte j = 0;

    // Read until 'x' is found
    while (!gotString) {
        if (Serial.available()) {
            inChar = Serial.read();
            inString[j] = inChar;
            j++;
            if (inChar == 'x') {
                gotString = true;
            }
        }
    }

    Serial.print("Input string: ");
    Serial.println(inString);

    // Parse year
    temp1 = (byte)inString[0] - 48;
    temp2 = (byte)inString[1] - 48;
    year = temp1 * 10 + temp2;

    // Parse month
    temp1 = (byte)inString[2] - 48;
    temp2 = (byte)inString[3] - 48;
    month = temp1 * 10 + temp2;

    // Parse date
    temp1 = (byte)inString[4] - 48;
    temp2 = (byte)inString[5] - 48;
    date = temp1 * 10 + temp2;

    // Day of week
    dOW = (byte)inString[6] - 48;

    // Hour
    temp1 = (byte)inString[7] - 48;
    temp2 = (byte)inString[8] - 48;
    hour = temp1 * 10 + temp2;

    // Minute
    temp1 = (byte)inString[9] - 48;
    temp2 = (byte)inString[10] - 48;
    minute = temp1 * 10 + temp2;

    // Second
    temp1 = (byte)inString[11] - 48;
    temp2 = (byte)inString[12] - 48;
    second = temp1 * 10 + temp2;
}

void setup() {
    Serial.begin(57600);
    Wire.begin();

    Serial.println("DS3231 RTC Test");
    Serial.println("Enter time as YYMMDDwHHMMSSx");
    Serial.println("Example: 2510035163050x = 2025-10-03 Friday 16:30:50");
}

void loop() {
    // If serial input available, update the RTC
    if (Serial.available()) {
        getDateStuff(year, month, date, dOW, hour, minute, second);

        myRTC.setClockMode(false); // 24h format
        myRTC.setYear(year);
        myRTC.setMonth(month);
        myRTC.setDate(date);
        myRTC.setDoW(dOW);
        myRTC.setHour(hour);
        myRTC.setMinute(minute);
        myRTC.setSecond(second);

        Serial.println("RTC time updated!");
    }

    // Variables for library calls
    bool Century = false;
    bool h12, PM;

    // Print current time every second
    Serial.print("Date: 20");   // DS3231 stores only last 2 digits of year
    Serial.print(myRTC.getYear(), DEC);
    Serial.print("-");
    Serial.print(myRTC.getMonth(Century), DEC);
    Serial.print("-");
    Serial.print(myRTC.getDate(), DEC);
    Serial.print("  DOW: ");
    Serial.print(myRTC.getDoW(), DEC);

    Serial.print("  Time: ");
    Serial.print(myRTC.getHour(h12, PM), DEC);   // Pass required refs
    Serial.print(":");
    Serial.print(myRTC.getMinute(), DEC);
    Serial.print(":");
    Serial.println(myRTC.getSecond(), DEC);

    delay(1000);
}
