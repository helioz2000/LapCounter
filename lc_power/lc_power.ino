/*
 * lc_power.ino
 * Lap Counter base - mobile power management
 * 
 * Hardware: Atmel ATTiny84
 * Connections:
 * 1 - VCC
 * 2 - Digital Output - select Bat1 (Mosfet Q2)
 * 3 - Digital Output - select Bat2 (Mosfet Q3)
 * 4 - not connected (RESET)
 * 5 - not connected (Serial RX for commissioning)
 * 6 - Analog Input - Current Batt1 (ACS712 5A)
 * 7 - I2C SDA
 * 8 - not connected
 * 9 - I2C SCL
 * 10- Analog Input - Current Batt2 (ACS712 5A)
 * 11 - Analog Input - Voltage Batt2 (MCP6002 B)
 * 12 - Analog Input - Voltage Batt1 (MCP6002 A)
 * 13 - not connected (Serial TX for commissioning)
 * 14 - GND
 */

#include <SoftwareSerial.h>

// I/O Pin definitions
#define BAT1_SEL_PIN 10   // pin 2
#define BAT2_SEL_PIN 9    // pin 3
#define BAT1_I_PIN A7     // pin 6
#define BAT2_I_PIN A3     // pin 10
#define BAT2_V_PIN A2     // pin 11
#define BAT1_V_PIN A1     // pin 12

// for commissioning only
const byte RX = 8;    // pin 5
const byte TX = 0;    // pin 13

SoftwareSerial Serial(RX, TX);  // use the software-serial library  

void setup() {
  Serial.begin(9600);
}

void loop() {
  Serial.println("lc_power is operational");
  delay(5000);
}
