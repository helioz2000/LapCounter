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

// enable for serial debugging
//#define DEBUG_SERIAL 1
//#define DEBUG_SHOW_AI_V           // show voltage at analog input
//#define DEBUG_SHOW_AI_RAW         // overrides DEBUG_SHOW_AI_V

#ifdef DEBUG_SERIAL
#include <SoftwareSerial.h>
#endif

#include "TinyWireM.h"                  // I2C Master lib for ATTinys which use USI
#include "LiquidCrystal_attiny.h"       // for LCD w/ GPIO MODIFIED for the ATtiny85
#include "Smoothed.h"                   // to filter ACS712 readings

#define LCD_I2C_ADDR     0x3F              // (PCA8574A A0-A2 @5V) typ. A0-A3 Gnd 0x20 / 0x38 for A
const byte LCD_ROWS = 4;
const byte LCD_COLUMNS = 20;


// I/O Pin definitions
#define BAT1_SEL_PIN 10   // pin 2
#define BAT2_SEL_PIN 9    // pin 3
#define BAT1_I_PIN A7     // pin 6
#define BAT2_I_PIN A3     // pin 10
#define BAT2_V_PIN A2     // pin 11
#define BAT1_V_PIN A1     // pin 12

#ifdef DEBUG_SERIAL
// for commissioning only
const byte RX = 8;    // pin 5
const byte TX = 0;    // pin 13
SoftwareSerial Serial(RX, TX);  // use the software-serial library
#endif

LiquidCrystal_I2C lcd(LCD_I2C_ADDR,LCD_COLUMNS,LCD_ROWS);  // set address & LCD size
Smoothed <int> bat1_I_raw;
Smoothed <int> bat2_I_raw;

// Analog conversion constant
const int vcc_mV = 5000;              // VCC
const int adc_resolution = 1024;      // bits ADC
const float mV_per_bit = (float)vcc_mV / (float)adc_resolution;       // for analog conversion
const int mA_per_mV = 2;                    // ACS712 5A = 2.5V, 2mA per mV
const int acs712_zero_offset_i1 = -15;         // ACS712 zero point offset from VCC/2 [mV]
const int acs712_zero_offset_i2 = -20;         // ACS712 zero point offset from VCC/2 [mV]

// Analog readings
int bat1_I_mA, bat2_I_mA, bat1_V_mV, bat2_V_mV;

void setup() {
  // For debugging
#ifdef DEBUG_SERIAL
  Serial.begin(9600);
#endif

  // I/O pins
  pinMode(BAT1_SEL_PIN, OUTPUT);
  pinMode(BAT2_SEL_PIN, OUTPUT);
  digitalWrite(BAT1_SEL_PIN, LOW);
  digitalWrite(BAT1_SEL_PIN, LOW);

  // Analog pins
  analogReference(DEFAULT);   // use VCC as reference [INTERNAL = 1.1V, EXTERNAL]

  bat1_I_raw.begin(SMOOTHED_AVERAGE, 16);
  bat2_I_raw.begin(SMOOTHED_AVERAGE, 16);
  
  // initialize the lcd 
  lcd.init();                           
  lcd.backlight();
  lcd.clear();
  lcd.print("Lap Counter");  // Print a message to the LCD.
  lcd.setCursor(0,2);
  lcd.print("Control Technologies");
  lcd.setCursor(0,3);
#ifdef DEBUG_SHOW_AI_RAW
  lcd.print("DEBUG_SHOW_AI_RAW");
#else
#ifdef DEBUG_SHOW_AI_V
  lcd.print("DEBUG_SHOW_AI_V");
#endif
#endif
  delay(2000);
  lcd.clear();
}

// convert raw analog reading to mV
float analogVoltage(int rawValue) {
  return float(rawValue) * mV_per_bit;
}

// returns mV reading for Analog Input
float readAnalogVoltage(byte analogPin) {
  return analogVoltage(analogRead(analogPin));
}

// read all analogs
void readAnalogs(void) {
  // current inputs are filtered
  bat1_I_raw.add(analogRead(BAT1_I_PIN));
  bat2_I_raw.add(analogRead(BAT2_I_PIN));
  
  int analogIn_mV = (int)analogVoltage(bat1_I_raw.get()); 
#ifdef DEBUG_SHOW_AI_V
  bat1_I_mA = analogIn_mV;
#else
  bat1_I_mA = (analogIn_mV - (vcc_mV / 2) - acs712_zero_offset_i1) * mA_per_mV;
#endif

  analogIn_mV = (int)analogVoltage(bat2_I_raw.get()); 
#ifdef DEBUG_SHOW_AI_V
  bat2_I_mA = analogIn_mV;
#else
  bat2_I_mA = (analogIn_mV - (vcc_mV / 2) - acs712_zero_offset_i2) * mA_per_mV;
#endif

  analogIn_mV = (int)readAnalogVoltage(BAT1_V_PIN);
#ifdef DEBUG_SHOW_AI_V
  bat1_V_mV = analogIn_mV;
#else
  bat1_V_mV = int(float(analogIn_mV - 913) / 0.337) + 13000;
#endif

  analogIn_mV = (int)readAnalogVoltage(BAT2_V_PIN);
#ifdef DEBUG_SHOW_AI_V
  bat2_V_mV = analogIn_mV;
#else
  bat2_V_mV = int(float(analogIn_mV - 913) / 0.337) + 13000;
#endif
}

void displayValues () {
  lcd.setCursor(0,0);
  lcd.print("I1=");
  lcd.print(bat1_I_mA);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif

  lcd.setCursor(10,0);
  lcd.print("V1=");
  lcd.print(bat1_V_mV);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif
  
  lcd.setCursor(0,1);
  lcd.print("I2=");
  lcd.print(bat2_I_mA);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif

  lcd.setCursor(10,1);
  lcd.print("V2=");
  lcd.print(bat2_V_mV);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif
}

#ifdef DEBUG_SHOW_AI_RAW
void displayRawValues () {
  lcd.setCursor(0,0);
  lcd.print("I1=");
  lcd.print(analogRead(BAT1_I_PIN));  
  lcd.print("  ");

  lcd.setCursor(10,0);
  lcd.print("V1=");
  lcd.print(analogRead(BAT1_V_PIN));
  lcd.print("  ");

  
  lcd.setCursor(0,1);
  lcd.print("I2=");
  lcd.print(analogRead(BAT2_I_PIN));
  lcd.print("  ");

  lcd.setCursor(10,1);
  lcd.print("V2=");
  lcd.print(analogRead(BAT2_V_PIN));
  lcd.print("  ");


  bat1_I_raw.add(analogRead(BAT1_I_PIN));
  bat2_I_raw.add(analogRead(BAT2_I_PIN));
  lcd.setCursor(0,2);
  lcd.print("I1 Filtered =");
  lcd.print(bat1_I_raw.get());
  lcd.print("  ");

  lcd.setCursor(0,3);
  lcd.print("I2 Filtered =");
  lcd.print(bat2_I_raw.get());
  lcd.print("  ");
}
#endif

void loop() {
#ifdef DEBUG_SERIAL  
  Serial.println("lc_power is operational");
#endif
  delay(100);
#ifdef DEBUG_SHOW_AI_RAW
  displayRawValues();
#else
  readAnalogs();
  displayValues();
#endif
}
