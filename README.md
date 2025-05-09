User setup

1. Required packages 
- WiFi101 (0.16.1)

2. Specify host network name (ssid[]) and password (pass[]). Once the code is flashed onto the Arduino, and is consistently recieving power, you will be able to connect to the network.

char ssid[] = "Pump_Controller";   // Name of the network

char pass[] = "password";          // Password: at least 8 characters

3. Ensure that your board is wired to pin 9. 

const int pumpPin = 9; // pump control pin

4. User commands

Start delay (ms) // User specified delay for pump to fire in milliseconds (ms). Defaults to 0 ms.

Run time (ms) // User specified run time for pump in milliseconds. Deaults to 100 ms.
