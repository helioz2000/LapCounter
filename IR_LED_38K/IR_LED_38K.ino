/*
 * 38kHz driver for IR LED's
 * 
 */

//Attiny85 , running @ 8MHz
// Note: factory default is 1MHz, use "Burn Bootloader" before download to clock frequency

// Using timer 1
//
//                           +-\/-+
//  Ain0       (D  5)  PB5  1|    |8   VCC
//  Ain3       (D  3)  PB3  2|    |7   PB2  (D  2)  INT0  Ain1
//  Ain2       (D  4)  PB4  3|    |6   PB1  (D  1)        pwm1
//                     GND  4|    |5   PB0  (D  0)        pwm0
//                           +----+

void setup() {
  pinMode(1, OUTPUT);
  TCNT1 = 0;
  TCCR1 = 0;
  GTCCR |= (1 << PSR1); //section 13.3.2 reset the prescaler
  TCCR1 |= (1 << CTC1); // section 12.3.1 CTC mode
  TCCR1 |= (1 << COM1A0); //togle pin PB1 table 12-4
  TCCR1 |= (1 << CS10); //prescaler 1 table 12-5
  OCR1C = 104;
  OCR1A = 104;
}

void loop() {
}
