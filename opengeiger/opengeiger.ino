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
#define kP 0.152 // Valeure déterminée expérimentalement
#define TOLERANCE 8.0

float actual_tension = 0.0;
float ref_tension = 0;

// PWM
#define PERIOD 2000
#define PWM_RESOLUTION 255
#define max_duty_cycle 0.8  //Cette valeur est le pourcantage maximal de la rapport cyclique que le PWM peut atteindre

int pwm_duty_cycle = 0;
int pwm_count = 0;
int limite_PWM=max_duty_cycle*PWM_RESOLUTION;

// Bluetooth et comptage
int count = 0;
long precTime;
int isCo = 0;
int isSaturated  = 0;
int isWarningOn = 0;
long satTime;

// Alimentation
#define ALIM_VOLT_DIV_INV 1.3
#define BAT_LOW 3.5
int isBatLowOn = 0;

// Indicateurs à leds
#define LED_ETAT 0
#define LED_BLUETOOTH 1
#define LED_PULSE 2
#define LED_BRIGHTNESS 32
#define NB_LEDS 3
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NB_LEDS, PIN_LEDS, NEO_GRB + NEO_KHZ800);

/* //essais de controle logiciel de la LED comptant les coups
bool etat_led_coup_a_mettre;
bool etat_led_coup_actuelle;

#define LOOP_COUNTER_LIMIT 200;
unsigned short loop_counter=0;
*/

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
//  etat_led_coup_a_mettre=true;  //s'il y a un coup alors à prochaine mise à jour d'état du LED blanc, on va l'allumer
  return 0;
}
 
void setup() {
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_MESURE_HT, INPUT);
  pinMode(PIN_COMPTEUR, INPUT);
  
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  
  strip.setPixelColor(LED_BLUETOOTH, strip.Color(0, 0, 0));
  strip.setPixelColor(LED_PULSE, strip.Color(0, 0, 0));
  strip.setPixelColor(LED_ETAT, strip.Color(0, 255, 0));
  strip.show();
  
  analogReference(VBG); // Référence de 1.2V interne
 
  RFduino_pinWakeCallback(PIN_COMPTEUR, LOW, countCallback);
 
  RFduinoBLE.deviceName = "OpenGeiger"; // Le nom et la description doivent faire
  RFduinoBLE.advertisementData = "SBM20"; // moins de 18 octets en tout.
 
  RFduinoBLE.begin();
 
  precTime = millis();
  configTimer(NRF_TIMER1, TIMER1_IRQn, TIMER1_INTERUPT);
/*  
  etat_led_coup_a_mettre=false;
  etat_led_coup_actuelle=false; //LED de coup est etaignt au demarrage
*/
}

void loop() {
  if (millis() - precTime > 1000) { // Toutes les secondes, on envoi le comptage et la tension au smartphone
   int alim_tension = ((analogRead(PIN_ALIM) * 360.0 * ALIM_VOLT_DIV_INV) / 1023.0);
   
   if((alim_tension<BAT_LOW*100)&&(!isBatLowOn)) {
     strip.setPixelColor(LED_ETAT, strip.Color(255, 255, 0));
     strip.show();
     isBatLowOn=1;
   }
   
//  char buff[4];
  char buff[6]; // 3 int ! attention la limite est à 20 bytes ?
   memcpy(&buff[0], &pwm_duty_cycle, sizeof(int));
   memcpy(&buff[2], &alim_tension, sizeof(int));
   memcpy(&buff[4], &count, sizeof(int));
// while (! RFduinoBLE.send((const char*)buff, 4));
   while (! RFduinoBLE.send((const char*)buff, 6));

/*   if ((isSaturated)&&(!isWarningOn)); {
    strip.setPixelColor(LED_PULSE, strip.Color(255, 0, 0));
    strip.show();
    isWarningOn = 1;
   } 
*/
/*   if ((isWarningOn)&&(!isSaturated)&&(millis()-satTime>5000)); {
    strip.setPixelColor(LED_PULSE, strip.Color(0, 0, 0));
    strip.show();
    isWarningOn = 0;
   } 
*/   
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
   pwm_duty_cycle = min(a, limite_PWM);
   
   if(pwm_duty_cycle == limite_PWM) {
    isSaturated = 1; 
    satTime = millis();
   }
   
   if(pwm_duty_cycle < 0.95*limite_PWM) {
    isSaturated = 0; 
   }
 }
 

}

/*
if(loop_counter>LOOP_COUNTER_LIMIT){
   if (etat_led_coup_a_mettre!=etat_led_coup_actuelle) control_LED_PULSE(); // S'il faut faire une changement dans l'étatde led, alos on fait le changement
   etat_led_coup_a_mettre=false;
   loop_counter=0;
   }
   else loop_counter++;
}
*/

/* void control_LED_PULSE(){
   if (!etat_led_coup_a_mettre) {  //si le LED doit s'eteindre
     strip.setPixelColor(LED_PULSE, strip.Color(0, 0, 0)); //eteignt
     etat_led_coup_actuelle=false; //maintenant il est eteignt
     etat_led_coup_a_mettre=false; //il va rester eteignt
   }
   else  { strip.setPixelColor(LED_PULSE, strip.Color(255, 255, 255)); //allume
           etat_led_coup_actuelle=true;  //maintenant il est allumé
           etat_led_coup_a_mettre=false;  //il va s'eteindre (ou il va rester allumer s'il y a une coup qui apparait dans les 200 boucles qui arrivent)
         }
  strip.show();
}
*/

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
