#include <RFduinoBLE.h>

// GPIOTE :
#define GPIOTE_COUNT 0
#define GPIOTE_FOLLOW 1
#define GPIOTE_LED 2
#define GPIOTE_BUZZER 3

// Pins
#define PIN_FLASH 0
#define PIN_ALIM 1
#define PIN_PWM 2
#define PIN_MESURE_HT 3
#define PIN_COMPTEUR 4
#define PIN_LEDS 5
#define PIN_BUZZER 6 

// Asservissement
#define VOLTAGE_DIVIDER_INV 315
#define kP 0.0152 // Valeure déterminée expérimentalement
#define TOLERANCE 4.0

float actual_tension = 0.0;
float set_tension = 0;

// PWM
#define PERIOD 2000
#define PWM_RESOLUTION 255
#define max_duty_cycle 0.8  //Cette valeur est le pourcentage maximal du rapport cyclique que le PWM peut atteindre

float pwm_duty_cycle = 0;
int pwm_count = 0;
//int limite_PWM=max_duty_cycle*PWM_RESOLUTION;
float limite_PWM=204.0;

// Bluetooth et comptage
int count = 0;
long precTime;
int isCo = 0;

// Alimentation
#define ALIM_VOLT_DIV_INV 1.3
#define BAT_LOW 3.5
int isBatLowOn = 0;

// Indicateurs à leds
#define LED_ETAT 0
#define LED_BLUETOOTH 1
#define NB_PIXELS 2

const int nb_leds = NB_PIXELS*3;
uint8_t leds[nb_leds];

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

// Timer PWM 
// passer en paramètre NRF_TIMER1
// NRF_TIMER0 est utilise par le BT
// NRF_TIMER2 est utilise pour le comptage
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

// Timer counter
void configCounter() {
  // Config GPIO Event (gpiote_channel, pin, polaritée)
  nrf_gpiote_event_config(GPIOTE_COUNT, PIN_COMPTEUR, NRF_GPIOTE_POLARITY_LOTOHI);
 
  // Config counter
  NRF_TIMER2->MODE = TIMER_MODE_MODE_Counter;
  NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_16Bit;
  NRF_TIMER2->TASKS_START = 1;
 
  // Config PPI
  int ppi_counter = find_free_PPI_channel(255);
  NRF_PPI->CH[ppi_counter].EEP = (uint32_t)&NRF_GPIOTE->EVENTS_IN[GPIOTE_COUNT]; // Input
  NRF_PPI->CH[ppi_counter].TEP = (uint32_t)&NRF_TIMER2->TASKS_COUNT; // Output
 
  NRF_PPI->CHEN |= (1 << ppi_counter); // Active le canal PPI
}

void configFollower(){ 
  
  // GPIO Event (gpiote_channel, pin, polaritée)
  nrf_gpiote_event_config(GPIOTE_FOLLOW, PIN_COMPTEUR, NRF_GPIOTE_POLARITY_TOGGLE);
 
  // GPIO Task (gpiote_channel, pin, polaritée, etat_initial)
  nrf_gpiote_task_config(GPIOTE_LED, PIN_FLASH, NRF_GPIOTE_POLARITY_TOGGLE, NRF_GPIOTE_INITIAL_VALUE_LOW);
  nrf_gpiote_task_config(GPIOTE_BUZZER, PIN_BUZZER, NRF_GPIOTE_POLARITY_TOGGLE, NRF_GPIOTE_INITIAL_VALUE_LOW);
 
  // Valeures possibles pour la polaritée :
  // NRF_GPIOTE_POLARITY_LOTOHI
  // NRF_GPIOTE_POLARITY_HITOLO
  // NRF_GPIOTE_POLARITY_TOGGLE
 
  // Config PPI
  int ppi_led = find_free_PPI_channel(255);
  NRF_PPI->CH[ppi_led].EEP = (uint32_t)&NRF_GPIOTE->EVENTS_IN[GPIOTE_FOLLOW]; // Input
  NRF_PPI->CH[ppi_led].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[GPIOTE_LED]; // Output
  NRF_PPI->CHEN |= (1 << ppi_led); // Active le canal PPI
  
  int ppi_buzzer = find_free_PPI_channel(255);
  NRF_PPI->CH[ppi_buzzer].EEP = (uint32_t)&NRF_GPIOTE->EVENTS_IN[GPIOTE_FOLLOW]; // Input
  NRF_PPI->CH[ppi_buzzer].TEP = (uint32_t)&NRF_GPIOTE->TASKS_OUT[GPIOTE_BUZZER]; // Output
 
  NRF_PPI->CHEN |= (1 << ppi_buzzer); // Active le canal PPI
}

// Gestion des leds
// http://forum.rfduino.com/index.php?topic=30.30

void setRGB(int led, uint8_t r, uint8_t g, uint8_t b) {
  leds[led*3] = g;
  leds[led*3+1] = r;
  leds[led*3+2] = b;
}

void showLeds() {
  noInterrupts();
  for (int wsOut = 0; wsOut < nb_leds; wsOut++) {
    for (int x=7; x>=0; x--) {
      NRF_GPIO->OUTSET = (1UL << PIN_LEDS);
      if (leds[wsOut] & (0x01 << x)) {
        __ASM ( \
              " NOP\n\t" \
              " NOP\n\t" \
              " NOP\n\t" \
              " NOP\n\t" \
              " NOP\n\t" \
              );
        NRF_GPIO->OUTCLR = (1UL << PIN_LEDS);
      } else {
        NRF_GPIO->OUTCLR = (1UL << PIN_LEDS);
        __ASM ( \
              " NOP\n\t" \
              " NOP\n\t" \
              " NOP\n\t" \
              );      
      }
    }
  }
  delayMicroseconds(50); // latch and reset WS2812.
  interrupts();  
}
 
void setup() {
  pinMode(PIN_PWM, OUTPUT);
  pinMode(PIN_MESURE_HT, INPUT);
  pinMode(PIN_COMPTEUR, INPUT);
  pinMode(PIN_LEDS, OUTPUT);
  
  setRGB(LED_BLUETOOTH, 0, 0, 0);
  setRGB(LED_ETAT, 0, 32, 0);
  showLeds();
  
  analogReference(VBG); // Référence de 1.2V interne
 
  configCounter();
  configFollower();
 
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
     setRGB(LED_ETAT, 32, 32, 0);
     showLeds();
     isBatLowOn=1;
   }
   
   char buff[8]; // 3 int ! attention la limite est à 20 bytes ?
   intToChar.i = (int) pwm_duty_cycle;
   buff[0] = intToChar.c[0];
   buff[1] = intToChar.c[1];
   
   intToChar.i = alim_tension;
   buff[2] = intToChar.c[0];
   buff[3] = intToChar.c[1];
   
   NRF_TIMER2->TASKS_CAPTURE[0] = 1; // capture le comptage dans CC[0]
   count = (uint8_t)NRF_TIMER2->CC[0];
   
   intToChar.i = count;
   buff[4] = intToChar.c[0];
   buff[5] = intToChar.c[1];
   
   intToChar.i = (int) (actual_tension);
   buff[6] = intToChar.c[0];
   buff[7] = intToChar.c[1];
   
   while (! RFduinoBLE.send((const char*)buff, 8));
   
   precTime = millis();
   
   NRF_TIMER2->TASKS_CLEAR = 1; // count = 0;
   count = 0;
 }
 
 actual_tension = analogRead(PIN_MESURE_HT);    
 actual_tension = ((actual_tension * 3.6) / 1023.0) * VOLTAGE_DIVIDER_INV;
 
 if (isCo==0) {
   set_tension = 0.0;
   pwm_duty_cycle = 0.0;
 }
 
 if (abs(set_tension - actual_tension) > TOLERANCE) {
   float d = (set_tension - actual_tension) * kP;
   float a = pwm_duty_cycle + d;
   pwm_duty_cycle = max(0.0, min(a, limite_PWM));
 }
}

void RFduinoBLE_onConnect() {
  isCo = 1; // Un smartphone s'est connecté
  setRGB(LED_BLUETOOTH, 0, 0, 32); // Etat bluetooth
  showLeds();
}
 
void RFduinoBLE_onDisconnect() {
 isCo = 0; // Un smartphone s'est déconnecté
  setRGB(LED_BLUETOOTH, 0, 0, 0); // Etat bluetooth
  showLeds();
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
  set_tension = getInt(data, len);
}
