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
 * 11 - Analog Input - Voltage Batt2 (MCP6002 B, range 10.3V-25V)
 * 12 - Analog Input - Voltage Batt1 (MCP6002 A, range 10.3V-25V)
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

// LCD Display
#define LCD_I2C_ADDR     0x27              // (PCA8574A A0-A2 @5V) typ. A0-A3 Gnd 0x20 / 0x38 for A
const byte LCD_ROWS = 2;
const byte LCD_COLUMNS = 16;

// I/O Pin definitions
const byte BAT_SEL_PIN[] = {10, 9 };   // pin 2 and 3
#define BAT1_I_PIN A7     // pin 6
#define BAT2_I_PIN A3     // pin 10
#define BAT2_V_PIN A2     // pin 11
#define BAT1_V_PIN A1     // pin 12

// for commissioning in case LCD is not working/present
#ifdef DEBUG_SERIAL
const byte RX = 8;    // pin 5
const byte TX = 0;    // pin 13
SoftwareSerial Serial(RX, TX);  // use the software-serial library
#endif

LiquidCrystal_I2C lcd(LCD_I2C_ADDR,LCD_COLUMNS,LCD_ROWS);  // set address & LCD size

Smoothed <int> bat_I_filter[2];     // filter raw analog values

// Analog conversion constants
const int VCC_MV = 5000;              // VCC
const int ADC_RESOLUTION = 1024;      // resolution of inbuilt ADC
const float MV_PER_BIT = (float)VCC_MV / (float)ADC_RESOLUTION;       // for analog conversion
const int MA_PER_MV = 2;                    // ACS712 5A = 2.5V, 2mA per mV
const int ACS712_ZERO_OFFSET[] = { -15, -20 };         // ACS712 zero point offset from VCC/2 [mV]

// Analog readings
int bat_I_mA[2], bat_V_mV[2];

// Processing time slots
const int ANALOG_READ_INTERVAL = 100;       // ms between analog update
const int DISPLAY_UPDATE_INTERVAL = 250;    // ms between display update
const int PROCESS_INTERVAL = 10;            // ms between logic processing
unsigned long nextAnalogRead, nextDisplayUpdate, nextProcess;

// Display valiables
unsigned long nextDisplayPageChange;
byte displayPageNumber;
const int DISPLAY_PAGE_CHANGE_TIME = 1000;  // ms between page changes

// Battery variables
#define B1 0    // index for Battery arrays
#define B2 1

// Battery variables
BatteryType bat_type[] = { NONE, NONE };
bool bat_low[] = { false, false };        // true when battery is below low level
bool bat_selected[] = {false, false };    // true when battery is selected to drive load
unsigned long bat_off_delay[] = { 0, 0 }; // off delay to create overlap on battery changeover
unsigned long bat_low_time[] = { 0, 0 };  // timer for battery low detection 
const int BAT_LOW_TIME = 5000;            // ms voltage below min before bat_Low is set
const int BAT_OFF_DELAY = 200;            // ms overlay for battery changeover
unsigned long NO_BATTERY_VOLTAGE = 10500; // mV measued when bettery is disconnected.
unsigned long LI_ION_DETECT_VOLTAGE = 14900;  // mV above which we have a Li-Ion battery
unsigned long LEAD_ACID_LOW_VOLTAGE = 11500;  // mV for low voltage detection
unsigned long LI_ION_LOW_VOLTAGE = 15000;     // mV for low voltage detection

void setup() {
  // For debugging
#ifdef DEBUG_SERIAL
  Serial.begin(9600);
#endif

  // configure I/O pins
  pinMode(BAT_SEL_PIN[B1], OUTPUT);
  pinMode(BAT_SEL_PIN[B2], OUTPUT);
  digitalWrite(BAT_SEL_PIN[B1], LOW);
  digitalWrite(BAT_SEL_PIN[B2], LOW);

  // Select reference for analog inputs
  analogReference(DEFAULT);   // use VCC as reference [INTERNAL = 1.1V, EXTERNAL]

  // Create filters for current signal
  bat_I_filter[B1].begin(SMOOTHED_AVERAGE, 16);
  bat_I_filter[B2].begin(SMOOTHED_AVERAGE, 16);
  
  // initialize the lcd 
  lcd.init();                           
  lcd.backlight();
  lcd.clear();
  lcd.print("  Lap Counter   ");  // Print a message to the LCD.
  lcd.setCursor(0,1);
  lcd.print("  Control Tech  ");
  //lcd.setCursor(0,3);
#ifdef DEBUG_SHOW_AI_RAW
  //lcd.print("DEBUG_SHOW_AI_RAW");
#else
#ifdef DEBUG_SHOW_AI_V
  //lcd.print("DEBUG_SHOW_AI_V");
#endif
#endif
  delay(2000);
  lcd.clear();

  // start update loops
  nextAnalogRead = millis()+ANALOG_READ_INTERVAL;
  nextDisplayUpdate = millis()+DISPLAY_UPDATE_INTERVAL;
  nextProcess = millis()+PROCESS_INTERVAL;
  nextDisplayPageChange = millis()+DISPLAY_PAGE_CHANGE_TIME;
  displayPageNumber = 1;
}

BatteryType getBatteryType(int batIndex) {
  if (bat_V_mV[batIndex] < NO_BATTERY_VOLTAGE) return NONE;
  if (bat_V_mV[batIndex] > LI_ION_DETECT_VOLTAGE) return LI_ION;
  return LEAD_ACID;
}

bool checkBatteryLow(int batIndex) {
  int lowVoltage;
  switch (bat_type[batIndex])  {
  case NONE:
    return false;
  case LI_ION:
    lowVoltage = LI_ION_LOW_VOLTAGE;
    break;
  case LEAD_ACID:
    lowVoltage = LEAD_ACID_LOW_VOLTAGE;
    break;
  }
  if (bat_V_mV[batIndex] <= lowVoltage) {
    if (bat_low_time[batIndex] == 0) {
      bat_low_time[batIndex] = millis() + BAT_LOW_TIME;
    } else {
      if (millis() > bat_low_time[batIndex] ) return true;
    }
    
  } else {
    bat_low_time[batIndex] = 0;     // reset timeout
  }
  return false;
}

/*
 * Choose which battery to use
 * first to discharge is #1
 */
void selectActiveBattery() {
  bat_selected[B1] = ( (bat_type[B1] != NONE) && (!bat_low[B1]) ) ? true : false;
  bat_selected[B1] = ( (bat_type[B2] != NONE) && (!bat_low[B2]) && (!bat_selected[B1]) ) ? true : false;
}

/*
 * Drive the relevant output to enable the selected battery
 * introduces overlaps and delays
 */
void enableSelectedBattery() {
  bool bat_enabled[] = { false, false };
  // if both batteries are de-selected respond immediately
  if (!bat_selected[B1] && !bat_selected[B2] ) goto writeOut;
  
  // if #1 is de-selected but is still enabled
  if (!bat_selected[B1] && digitalRead(BAT_SEL_PIN[B1])) {
    // switch #2 on
    bat_enabled[B2] = bat_selected[B2];
    // keep #1 on for overlap
    bat_enabled[B1] = true;
    // start off delay timer if required
    if (bat_off_delay[B1] == 0) 
      bat_off_delay[B1] = millis() + BAT_OFF_DELAY;
    // timer is already running
    else {
      // has the timer expired?
      if (millis() >= bat_off_delay[B1]) {
        bat_enabled[B1] = false;          // disable B1
        bat_off_delay[B1] = 0;            // reset timer
      } 
    }
  } else {
    bat_enabled[B1] = bat_selected[B1];
  }

  // overlap [off] delay for battery 2
  if (!bat_selected[B2] && digitalRead(BAT_SEL_PIN[B2])) {
    // switch #1 on
    bat_enabled[B1] = bat_selected[B1];
    // keep #2 on for overlap
    bat_enabled[B2] = true;
    // start off delay timer if required
    if (bat_off_delay[B2] == 0) 
      bat_off_delay[B2] = millis() + BAT_OFF_DELAY;
    // timer is already running
    else {
      // has the timer expired?
      if (millis() >= bat_off_delay[B2]) {
        bat_enabled[B2] = false;          // disable battery
        bat_off_delay[B2] = 0;            // reset timer
      } 
    }
  } else {
    bat_enabled[B2] = bat_selected[B2];
  }
 
writeOut:
  digitalWrite(BAT_SEL_PIN[B1], bat_enabled[B1] ? HIGH : LOW);
  digitalWrite(BAT_SEL_PIN[B2], bat_enabled[B2] ? HIGH : LOW);
}

// process logic
void process() {
  bat_type[B1] = getBatteryType(B1);
  bat_type[B2] = getBatteryType(B2);
  bat_low[B1] = checkBatteryLow(B1);
  bat_low[B2] = checkBatteryLow(B2);
  selectActiveBattery();
  enableSelectedBattery();
}

// convert raw analog reading to mV
float analogVoltage(int rawValue) {
  return float(rawValue) * MV_PER_BIT;
}

/*  read analog input voltage
 *  returns reading in mV
 */
float readAnalogVoltage(byte analogPin) {
  return analogVoltage(analogRead(analogPin));
}

/*
 * Convert analog input voltage to battery voltage
 * inputVoltage in mV
 */
int getBatteryVoltage(int inputVoltage) {
#ifdef DEBUG_SHOW_AI_V
  return inputVoltage;
#else
  int batV = int(float(inputVoltage - 239) / 0.337) + 11000;
  if (batV < 10500) batV = 0;
  return batV;
#endif
}

/*
 * Convert analog input voltage to current reading 
 * ACS712 output is VCC/2 at zero
 * inputVoltage in mV
 */
int getBatteryCurrent(byte batIndex) {
  int inputVoltage = (int)analogVoltage(bat_I_filter[batIndex].get());
#ifdef DEBUG_SHOW_AI_V
  return inputVoltage;
#else
  return (inputVoltage - (VCC_MV / 2) - ACS712_ZERO_OFFSET[batIndex]) * MA_PER_MV;
#endif
}

// read all analogs
void readAnalogs(void) {
  // run current inputs through filter
  bat_I_filter[B1].add(analogRead(BAT1_I_PIN));
  bat_I_filter[B2].add(analogRead(BAT2_I_PIN));

  bat_I_mA[B1] = getBatteryCurrent( B1 );
  bat_I_mA[B2] = getBatteryCurrent( B2 );

  bat_V_mV[B1] = getBatteryVoltage((int)readAnalogVoltage(BAT1_V_PIN));
  bat_V_mV[B2] = getBatteryVoltage((int)readAnalogVoltage(BAT2_V_PIN));

}

void displayVoltage (int millivolts) {
  int units = millivolts/1000;
  int fraction = millivolts - (units*1000);
  lcd.print(units);
  lcd.print(".");
  lcd.print(fraction / 10);
}

void displayBatInfo (byte batIndex) {
  if (bat_type[batIndex] != NONE) {    
    displayVoltage(bat_V_mV[batIndex]);
    lcd.print("V ");
    lcd.print(bat_I_mA[batIndex]);
    lcd.print("mA");
  } else {
    lcd.print("disconnected");
  }

  lcd.setCursor(0,1);
  if(bat_low[batIndex]) { lcd.print("Low Voltage"); }
  else {
    displayBatteryType(batIndex);
  }
  
}

void displayBatteryType(byte batIndex) {
  switch(bat_type[batIndex]) {
    case LEAD_ACID:
      lcd.print("Lead");
      break;
    case LI_ION:
      lcd.print("LiIon");
      break;
    default:
      lcd.print("-");
  }
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


void display_pg1() {
  lcd.print("B1:");
  displayBatInfo(B1);
}

void display_pg2() { 
  lcd.print("B2:");
  displayBatInfo(B2); 
}

void display_pg3() {
  lcd.print("Page 3");
}

// display tasks
void display() {
  lcd.clear();
  lcd.setCursor(0,0);
  if (millis() >= nextDisplayPageChange) {
    nextDisplayPageChange = millis()+DISPLAY_PAGE_CHANGE_TIME;
    displayPageNumber++;
  }
  switch(displayPageNumber) {
    case 2:
      display_pg2();
      break;
    case 3:
      display_pg3();
      break;     
    default:        // default applies to "case 1:" and will reset displayPageNumber when > 3 
      displayPageNumber=1;
      display_pg1();
      break; 
  } 
}

void clearLine() {
  int i;
  for (i=0; i< LCD_COLUMNS; i++) {
    lcd.print(" ");
  }
}

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
    display();
  }

  if (millis() >= nextProcess) {
    nextProcess = millis() + PROCESS_INTERVAL;
    process();
  }
}
