1. Required packages 
- WiFi101 (0.16.1)

2. User setup
Specify host network name (ssid[]) and password (pass[]). Once the code is flashed onto the Arduino, and is consistently recieving power, you will be able to connect to the network.

char ssid[] = "Pump_Controller";   // Name of the network

char pass[] = "password";          // Password: at least 8 characters

Ensure that your board is wired to pin 9. 
const int pumpPin = 9; // pump control pin

