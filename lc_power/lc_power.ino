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
 * 
 * Functional description:
 * Selects the supply from one of two connected batteries based on charge state.
 * Measures and displays actual battery voltage, current and consumed power.
 * The batteries can be either 12V Lead Acid or 18V LiIon (tool) batteries.
 * The type of battery is automatically determined by the measured voltage.
 * 
 * Battery volateg ranges:
 * Lead Acid: 
 * voltage range: 12-14.7V. 
 * At rest: 100% > 12.6V, 50%=12.4V, Dead Flat=12.0V
 * 
 * LiIon:
 * packs consist of 10 cells in 2P5S configuration.
 * volatge range: 15.0 - 21V (3.0V-4.2V per cell)
 * 
 * 
 */

// enable for serial debugging
//#define DEBUG_SERIAL 1
//#define DEBUG_SHOW_AI_V           // show voltage at analog input
//#define DEBUG_SHOW_AI_RAW         // overrides DEBUG_SHOW_AI_V

#ifdef DEBUG_SERIAL
#include <SoftwareSerial.h>
#endif

#include "lc_power.h"                   // typedefs need to be in a header file
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
int bat_I_mA[2], bat_V_mV[2];

const int ANALOG_READ_INTERVAL = 100;       // ms between analog update
const int DISPLAY_UPDATE_INTERVAL = 250;    // ms between display update
const int PROCESS_INTERVAL = 500;           // ms between logic processing

// Battery variables
#define B1 0    // index for arrays
#define B2 1

BatteryType bat_type[] = { NONE, NONE };
bool bat_low[] = { false, false };
unsigned long bat_low_time[] = { 0, 0 };

unsigned long nextAnalogRead, nextDisplayUpdate, nextProcess;

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

  // start update loops
  nextAnalogRead = millis()+ANALOG_READ_INTERVAL;
  nextDisplayUpdate = millis()+DISPLAY_UPDATE_INTERVAL;
  nextProcess = millis()+PROCESS_INTERVAL;
}

BatteryType getBatteryType(int voltage) {
  if (voltage < 10500) return NONE;
  if (voltage > 14900) return LI_ION;
  return LEAD_ACID;
}

bool checkBatteryLow(int bat_index) {
  int lowVoltage;
  switch (bat_type[bat_index])  {
  case NONE:
    return false;
  case LI_ION:
    lowVoltage = 15000;
    break;
  case LEAD_ACID:
    lowVoltage = 11500;
    break;
  }
  return false;
}

// process logic
void process() {
  bat_type[B1] = getBatteryType(bat_V_mV[B1]);
  bat_type[B2] = getBatteryType(bat_V_mV[B2]);
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
  bat_I_mA[B1] = analogIn_mV;
#else
  bat_I_mA[B1] = (analogIn_mV - (vcc_mV / 2) - acs712_zero_offset_i1) * mA_per_mV;
#endif

  analogIn_mV = (int)analogVoltage(bat2_I_raw.get()); 
#ifdef DEBUG_SHOW_AI_V
  bat_I_mA[B2] = analogIn_mV;
#else
  bat_I_mA[B2] = (analogIn_mV - (vcc_mV / 2) - acs712_zero_offset_i2) * mA_per_mV;
#endif

  analogIn_mV = (int)readAnalogVoltage(BAT1_V_PIN);
#ifdef DEBUG_SHOW_AI_V
  bat_V_mV[B1] = analogIn_mV;
#else
  bat_V_mV[B1] = int(float(analogIn_mV - 913) / 0.337) + 13000;
#endif

  analogIn_mV = (int)readAnalogVoltage(BAT2_V_PIN);
#ifdef DEBUG_SHOW_AI_V
  bat_V_mV[B2] = analogIn_mV;
#else
  bat_V_mV[B2] = int(float(analogIn_mV - 913) / 0.337) + 13000;
#endif
}

void displayValues () {
#ifdef DEBUG_SHOW_AI_RAW
  displayRawValues();
#else  
  lcd.setCursor(0,0);
  lcd.print("I1=");
  lcd.print(bat_I_mA[B1]);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif

  lcd.setCursor(10,0);
  lcd.print("V1=");
  lcd.print(bat_V_mV[B1]);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif
  
  lcd.setCursor(0,1);
  lcd.print("I2=");
  lcd.print(bat_I_mA[B2]);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif

  lcd.setCursor(10,1);
  lcd.print("V2=");
  lcd.print(bat_V_mV[B2]);
#ifdef DEBUG_SHOW_AI_V
  lcd.print("mV");
#else
  lcd.print("  ");
#endif
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

  // Update analog values
  if (millis() >= nextAnalogRead) {
    nextAnalogRead = millis() + ANALOG_READ_INTERVAL;
    readAnalogs();
  }

  // Update display
  if (millis() >= nextDisplayUpdate) {
    nextDisplayUpdate = millis() + DISPLAY_UPDATE_INTERVAL;
    displayValues();
  }

  if (millis() >= nextProcess) {
    nextProcess = millis() + PROCESS_INTERVAL;
    process();
  }
}
