#include <RFduinoBLE.h>
#include "Adafruit_NeoPixel.h"

// Pins
#define PIN_PWM 1
#define PIN_MESURE_HT 3
#define PIN_COMPTEUR 4
#define PIN_LEDS 6

// Asservissement
#define VOLTAGE_DIVIDER_INV 318.63
#define kP 0.152
#define TOLERANCE 8.0

float actual_tension = 0.0;
float ref_tension = 0;

// PWM
#define PERIOD 2000
#define PWM_RESOLUTION 255
int pwm_duty_cycle = 0;
int pwm_count = 0;

// Bluetooth et comptage
int count = 0;
long precTime;
int isCo = 0;

// Indicateurs à leds
#define LED_ETAT 0
#define LED_BLUETOOTH 1
#define LED_PULSE 2
#define LED_BRIGHTNESS 32
#define NB_LEDS 3
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NB_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

void TIMER1_INTERUPT(void) {
  if (NRF_TIMER1->EVENTS_COMPARE[0] != 0) {
    if (pwm_count < pwm_duty_cycle) {
      digitalWrite(PIN_PWM, HIGH);  
    } else {
      digitalWrite(PIN_PWM, LOW);
    }
    pwm_count++;
    if (pwm_count == PWM_RESOLUTION) {
      pwm_count = 0;
    }
    NRF_TIMER1->EVENTS_COMPARE[0] = 0;
  }
}

void configTimer(NRF_TIMER_Type* nrf_timer, IRQn_Type irqn, callback_t callback) {
  nrf_timer->TASKS_STOP = 1; // Arrete le timer
  nrf_timer->MODE = TIMER_MODE_MODE_Timer;
  nrf_timer->BITMODE = (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos);
  nrf_timer->PRESCALER = 4; // résolution de 1 usec
  nrf_timer->TASKS_CLEAR = 1;
  nrf_timer->CC[0] = PERIOD / PWM_RESOLUTION;
  nrf_timer->INTENSET = TIMER_INTENSET_COMPARE0_Enabled << TIMER_INTENSET_COMPARE0_Pos;
  nrf_timer->SHORTS = (TIMER_SHORTS_COMPARE0_CLEAR_Enabled << TIMER_SHORTS_COMPARE0_CLEAR_Pos);
  attachInterrupt(irqn, callback);
  nrf_timer->TASKS_START = 1; // Redemarre le timer
}

int countCallback(uint32_t ulPin) {
  count++;
  return 0;
}
 
void setup() {
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_MESURE_HT, INPUT);
  pinMode(PIN_COMPTEUR, INPUT);
  
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  strip.show();
 
  strip.setPixelColor(LED_ETAT, strip.Color(0, 255, 0));
  strip.show();
  
  analogReference(VBG); // Référence de 1.2V interne
 
  RFduino_pinWakeCallback(PIN_COMPTEUR, LOW, countCallback);
 
  RFduinoBLE.deviceName = "OpenGeiger"; // Le nom et la description doivent faire
  RFduinoBLE.advertisementData = ""; // moins de 18 octets en tout.
 
  RFduinoBLE.begin();
 
  precTime = millis();
  configTimer(NRF_TIMER1, TIMER1_IRQn, TIMER1_INTERUPT);
}

void loop() {
  if (millis() - precTime > 1000) {
   // Toutes les secondes, on envoi le comptage au smartphone
   RFduinoBLE.sendInt(count);
 
   precTime = millis();
   count = 0;
 }
 
 actual_tension = analogRead(PIN_MESURE_HT);    
 actual_tension = ((actual_tension * 3.6) / 1023.0) * VOLTAGE_DIVIDER_INV;
 
 if (isCo==0) {
   ref_tension = 0.0;
 }
 if (abs(ref_tension - actual_tension) > TOLERANCE) {
   float a = pwm_duty_cycle + (ref_tension - actual_tension) * kP;
   pwm_duty_cycle = min(a, 204.0);
 }
}
 
void RFduinoBLE_onConnect() {
  isCo = 1; // Un smartphone s'est connecté
  strip.setPixelColor(LED_BLUETOOTH, strip.Color(0, 0, 255)); // Etat bluetooth
  strip.show();
}
 
void RFduinoBLE_onDisconnect() {
 isCo = 0; // Un smartphone s'est déconnecté
 strip.setPixelColor(LED_BLUETOOTH, strip.Color(0, 0, 0));
 strip.show();
}
 
int getInt(char*data, int len) {
  int value=0;
  int p=1;
  for (int i=len-1 ; i>=0 ; i--) {
    value += p*(data[i]-'0');
    p*=10;
  }
  return value;
}
 
void RFduinoBLE_onReceive(char *data, int len) {
  ref_tension = getInt(data, len);
}
