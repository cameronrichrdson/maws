/*
File:   DS1881-Example.ino
Author: J. Ian Lindsay
Date:   2019.10.20

DS1881 example.
*/

#define TEST_PROG_VERSION "v1.1"

#include <Wire.h>
#include <DS1881.h>
#if defined(DS1881_DEBUG)
  // If debugging is enabled in the build, another dependency will be needed.
  // It can be disabled in the driver's header file.
  // https://github.com/jspark311/CppPotpourri
  #include <StringBuilder.h>
#endif  // DS1881_DEBUG


/*******************************************************************************
* Pin definitions and hardware constants.
*******************************************************************************/
#define SDA_PIN             18
#define SCL_PIN             19
#define LED0_PIN            13


/*******************************************************************************
* Globals
*******************************************************************************/
DS1881 ds1881(DS1881_BASE_I2C_ADDR);


/*******************************************************************************
* Functions to output things to the console
*******************************************************************************/

void printHelp() {
  Serial.print("\nDS1881E Example ");
  Serial.print(TEST_PROG_VERSION);
  Serial.print("\n---< Meta >-------------------------\n");
  Serial.print("?     This output\n");
  #if defined(DS1881_DEBUG)
  Serial.print("i     DS1881E info\n");
  #endif

  Serial.print("\n---< Channel Manipulation >-----------\n");
  Serial.print("[/]   Volume up/down for channel 0\n");
  Serial.print("{/}   Volume up/down for channel 1\n");
  Serial.print("+/-   Volume up/down for both channels at once\n");
  Serial.print("x     Refresh register shadows\n");
  Serial.print("I     Reinitialize\n");
  Serial.print("#     Store wiper settings in NV\n");
  Serial.print("S     Serialize\n");
  Serial.print("R/r   Set range to 63/33\n");
  Serial.print("E/e   (En/Dis)able channel (mute)/\n");
  Serial.print("Z/z   (En/Dis)able zerocross detection\n");
}



/*******************************************************************************
* Setup function
*******************************************************************************/

void setup() {
  Serial.begin(115200);
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  Wire.begin();
  ds1881.init(&Wire);
}


/*******************************************************************************
* Main loop
*******************************************************************************/

void loop() {
  DIGITALPOT_ERROR ret = DIGITALPOT_ERROR::NO_ERROR;
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '[':
      case ']':
        ret = ds1881.setValue(0, ds1881.getValue(0) + (('[' == c) ? 1 : -1));
        Serial.print("setValue() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case '{':
      case '}':
        ret = ds1881.setValue(1, ds1881.getValue(1) + (('{' == c) ? 1 : -1));
        Serial.print("setValue() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case '-':
      case '+':
        ret = ds1881.setValue(ds1881.getValue(0) + (('-' == c) ? 1 : -1));
        Serial.print("setValue() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'Z':
      case 'z':
        ret = ds1881.zerocrossWait('Z' == c);
        Serial.print("zerocrossWait() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'E':
      case 'e':
        ret = ds1881.enable('E' == c);
        Serial.print("enable() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case '#':
        ret = ds1881.storeWipers();
        Serial.print("storeWipers() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'R':
      case 'r':
        ret = ds1881.setRange(('R' == c) ? 63 : 33);
        Serial.print("setRange() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'I':
        ret = ds1881.init();
        Serial.print("init() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'x':
        ret = ds1881.refresh();
        Serial.print("refresh() returns ");
        Serial.println(DS1881::errorToStr(ret));
        break;
      case 'S':   // Save the state into a buffer for later reconstitution.
        {
          uint8_t buffer[DS1881_SERIALIZE_SIZE];
          uint8_t written = ds1881.serialize(buffer, DS1881_SERIALIZE_SIZE);
          if (DS1881_SERIALIZE_SIZE == written) {
            for (uint8_t i = 0; i < DS1881_SERIALIZE_SIZE; i++) {
              Serial.print((buffer[i] > 0x0F) ? "0x" : "0x0");
              Serial.print(buffer[i], HEX);
              Serial.print(((i+1) % 12) ? " " : "\n");
            }
            Serial.println();
          }
          else {
            Serial.print("serialize() returns ");
            Serial.print(written);
            Serial.print(". Was expecting ");
            Serial.println(DS1881_SERIALIZE_SIZE);
          }
        }
        break;

      case '?':  printHelp();           break;
      #if defined(DS1881_DEBUG)
      case 'i':
        {
          StringBuilder output;
          ds1881.printDebug(&output);
          Serial.print((char*) output.string());
        }
        break;
      #endif
    }
  }
}
