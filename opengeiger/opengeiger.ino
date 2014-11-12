#include <RFduinoBLE.h>
#include "./Adafruit_NeoPixel.h"
// CS 10/11/2014
// Version experimentale : inclut la transmission de l'information concernant
// le PWM d'asservissement HT
// en vue du signalement de la saturation du tube

// Pins
#define PIN_ALIM 1
#define PIN_PWM 2
#define PIN_MESURE_HT 3
#define PIN_COMPTEUR 4
#define PIN_LEDS 5
// define PIN_BUZZER 6 inutilisee pour l instant

// Asservissement
#define VOLTAGE_DIVIDER_INV 315
#define kP 0.0152 // Valeure déterminée expérimentalement
#define TOLERANCE 8.0

float actual_tension = 0.0;
float ref_tension = 0;

// PWM
#define PERIOD 2000
#define PWM_RESOLUTION 255
#define max_duty_cycle 0.8  //Cette valeur est le pourcentage maximal du rapport cyclique que le PWM peut atteindre

int fake_counter = 0;
int pwm_duty_cycle = 0;
int pwm_count = 0;
//int limite_PWM=max_duty_cycle*PWM_RESOLUTION;
int limite_PWM=204;
// int pdc = 0;
// int sum_pdc = 0;
// float avg_pdc = 0.0;
// int nb_pdc = 0;

// Bluetooth et comptage
int count = 0;
long precTime;
int isCo = 0;
// int isSaturated  = 0;
// int isWarningOn = 0;
// long satTime;
long precTime2;


// Alimentation
#define ALIM_VOLT_DIV_INV 1.3
#define BAT_LOW 3.5
int isBatLowOn = 0;

// Indicateurs à leds
#define LED_ETAT 0
#define LED_BLUETOOTH 1
// #define LED_PULSE 2
#define NB_LEDS 2
//#define NB_LEDS 3
#define LED_BRIGHTNESS 32
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NB_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

// Utilitaires de conversion
union _intToChar {
  int i;
  char c[2];
} intToChar;

union _floatToChar {
  float f;
  char c[4];
} floatToChar;

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

int countCallback(uint32_t ulPin) {  //interruption generé par un coup
  count++;
  return 0;
}
 
void setup() {
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_MESURE_HT, INPUT);
  pinMode(PIN_COMPTEUR, INPUT);
  
  fake_counter = 1;
  
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  
  strip.setPixelColor(LED_BLUETOOTH, strip.Color(0, 0, 0));
  strip.setPixelColor(LED_ETAT, strip.Color(0, 255, 0));
  strip.show();
  
  analogReference(VBG); // Référence de 1.2V interne
 
  RFduino_pinWakeCallback(PIN_COMPTEUR, LOW, countCallback);
 
  RFduinoBLE.deviceName = "OpenGeiger"; // Le nom et la description doivent faire
  RFduinoBLE.advertisementData = "ChS-2"; // moins de 18 octets en tout.
 
  RFduinoBLE.begin();
 
  precTime = millis();
  configTimer(NRF_TIMER1, TIMER1_IRQn, TIMER1_INTERUPT);
}

void loop() {
  if (millis() - precTime > 1000) { // Toutes les secondes, on envoi le comptage et la tension au smartphone
   int alim_tension = ((analogRead(PIN_ALIM) * 360.0 * ALIM_VOLT_DIV_INV) / 1023.0);
   
   if ((alim_tension<BAT_LOW*100) && (!isBatLowOn)) {
     strip.setPixelColor(LED_ETAT, strip.Color(255, 255, 0));
     strip.show();
     isBatLowOn=1;
   }
   
   char buff[8]; // 3 int ! attention la limite est à 20 bytes ?
   intToChar.i = pwm_duty_cycle;
   buff[0] = intToChar.c[0];
   buff[1] = intToChar.c[1];
   
   intToChar.i = alim_tension;
   buff[2] = intToChar.c[0];
   buff[3] = intToChar.c[1];
   
   intToChar.i = count;
   buff[4] = intToChar.c[0];
   buff[5] = intToChar.c[1];
   
   intToChar.i = (int) (actual_tension);
   buff[6] = intToChar.c[0];
   buff[7] = intToChar.c[1];
   
   while (! RFduinoBLE.send((const char*)buff, 8));
   
   precTime = millis();
   count = 0;
 }
 
 actual_tension = analogRead(PIN_MESURE_HT);    
 actual_tension = ((actual_tension * 3.6) / 1023.0) * VOLTAGE_DIVIDER_INV;
 
 if (isCo==0) {
   ref_tension = 0.0;
   pwm_duty_cycle = 0;
 }
 
 if (abs(ref_tension - actual_tension) > TOLERANCE) {
   float d = (ref_tension - actual_tension) * kP;
   int a = (int)(pwm_duty_cycle + d);
   pwm_duty_cycle = max(0, min(a, limite_PWM));
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
