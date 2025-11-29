#include <Arduino.h>
#include <Servo.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <EEPROM.h>


const int EE_ADDR_POWER_CYCLES = 0;
uint32_t powerCycles = 0;

constexpr uint8_t PIN_SERVO_FEEDBACK = 2; //servo feedback signal
constexpr uint8_t PIN_SERVO_CTRL = 3; //servo command/control signal
constexpr uint8_t PIN_COIN_SENSOR = 4; //input for IR sensor flag 



constexpr uint8_t PIN_1_BTN = 5; // pin for qty 1 button
constexpr uint8_t PIN_2_BTN = 6; // pin for qty 2 button
constexpr uint8_t PIN_3_BTN = 7; // pin for qty 3 button
constexpr uint8_t PIN_VEND_BTN = 8; //pin for VEND command 


constexpr uint8_t PIN_STATUS_LED = 9; // signal pin for RGB LED - Status LED x1
constexpr uint8_t PIN_1COIN_LED = 10; //RGB pin for 1 coin status LED
constexpr uint8_t PIN_2COIN_LED = 11; // RGB pin for 2 coin status LED
constexpr uint8_t PIN_3COIN_LED = 12; // RGB pin for 3 coin status LED

constexpr uint8_t BLINK_COUNT = 3;

// Status RGB LED x1 - single LED that indicates conditions


Adafruit_NeoPixel statusLed(1, PIN_STATUS_LED, NEO_GRB + NEO_KHZ800); //create a "strip" of lights OBJECT - 1 light in this set up, pin to control LED from board, NEO_GRB is the standard for green red blue format, NEO_KHZ800 is 800hz setting
Adafruit_NeoPixel coin1Led(1, PIN_1COIN_LED, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel coin2Led(1, PIN_2COIN_LED, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel coin3Led(1, PIN_3COIN_LED, NEO_GRB + NEO_KHZ800);





inline void setRGB(uint8_t r,uint8_t g,uint8_t b){ 
  statusLed.setPixelColor(0, statusLed.Color(r,g,b)); 
  statusLed.show(); 
  } //function that returns nothing - takes 8bit int for r g and b values - rgb. notation going forward
//in this setRGB function we are setPixelColor then position 0 gets rgb.Color(passes in the values handed into function on call, then runs object.show(); for showing value)

//this is the logic for "blinking" the LED - sets it to the specified rgb values , blocks for x ms, turns rgb values to 0 or off, then delay for x ms and so on - could loop a couple times to repeat blink
inline void blinkRGB(uint8_t r,uint8_t g,uint8_t b, uint16_t ms=100){
  //for loop to make blink occur 3x (blocking code)
  for (uint8_t i = 0; i < BLINK_COUNT; i++){
    setRGB(r,g,b);
    delay(ms);
    setRGB(0,0,0);
    delay(ms);
  }

}

struct Tier { uint8_t steps, cost, r, g, b; };

const Tier TIERS[] = {
  {1 , 1 , 0,255,0},     // 1x, cost 1, green
  {2 , 2 , 255,255,0},     // 2x, cost 2, yellow
  {3 , 3 , 255,0,0},     // 3x, cost 3, red
};

constexpr uint8_t TIER_COUNT = sizeof(TIERS)/sizeof(TIERS[0]);


//button logic 

uint8_t selectedIndex() {
  static uint8_t lastSelected = 0;

  if (digitalRead(PIN_3_BTN) == LOW){
    lastSelected = 2;
    return lastSelected;
  }
  if (digitalRead(PIN_2_BTN) == LOW){
    lastSelected = 1;
    return lastSelected;
  }
  if (digitalRead(PIN_1_BTN) == LOW){
    lastSelected = 0;
    return lastSelected;
  }
  return lastSelected;
}

void showSelectionColor() {
  static uint8_t lastShown = 255;
  uint8_t qtyIndex = selectedIndex();
  if (qtyIndex >= TIER_COUNT) qtyIndex = 0;
  if (qtyIndex != lastShown) {
    const Tier& t = TIERS[qtyIndex];
    setRGB(t.r , t.g , t.b);
    lastShown = qtyIndex;
  }
}


volatile bool coinFlag = false;
uint32_t coinBank = 0;

volatile uint32_t lowStartMs = 0;
volatile bool inLow = false;
volatile uint32_t lastCoinMs = 0;



constexpr uint8_t MAX_BANK = 3;
constexpr uint16_t LOW_MIN_MS = 15; //must be LOW for this duration to trigger a coin
constexpr uint16_t COIN_COOLDOWN_MS = 120; //gap between coins

void onCoinISR() {
  
  uint32_t t = millis();
  bool s = digitalRead(PIN_COIN_SENSOR); //sensor flag for detection - red dot on module should mirror

  if (!s) {
    inLow = true;
    lowStartMs = t;
  } else {
    if(inLow) {
      uint32_t lowDur = t - lowStartMs;
      if  (lowDur >= LOW_MIN_MS && (t - lastCoinMs) >= COIN_COOLDOWN_MS){
        coinFlag = true;
        lastCoinMs = t;
      }
      inLow = false;
    }
  }
}

inline void showCoinLEDs(uint8_t selection){


  /*digitalWrite(PIN_1COIN_LED , selection>=1);
  digitalWrite(PIN_2COIN_LED , selection>=2);
  digitalWrite(PIN_3COIN_LED , selection>=3);*/

  uint32_t COIN1_ON = coin1Led.Color(0,255,0);
  uint32_t COIN2_ON = coin2Led.Color(255,255,0);
  uint32_t COIN3_ON = coin3Led.Color(255,0,0);

  uint32_t COIN1_OFF = coin1Led.Color(0,0,0);
  uint32_t COIN2_OFF = coin2Led.Color(0,0,0);
  uint32_t COIN3_OFF = coin3Led.Color(0,0,0);


  coin1Led.setPixelColor(0,selection >= 1 ? COIN1_ON : COIN1_OFF);
  coin1Led.show();

  coin2Led.setPixelColor(0, selection >= 2 ? COIN2_ON : COIN2_OFF);
  coin2Led.show();

  coin3Led.setPixelColor(0, selection >= 3 ? COIN3_ON : COIN3_OFF);
  coin3Led.show();


}


Servo drum;
int NEUTRAL_US = 1500; //this signal should hold motor still - above is fwd, below is rev and higher numbers = greater speed in respective direction
int MAX_FWD_SPEED = 1500 + 75; //max speeds  are neutral +/- 200 I did 150 here for safety and the fact this is a slow moving part
int MAX_REV_SPEED = 1500 - 75; //same as above but reverse
float Kp = 1.0f; //gain control
int OFFSET = 30;

const float CYCLE_MIN_US = 950.0f;
const float CYCLE_MAX_US = 1250.0f;
float DUTY_MIN = 0.027f;
float DUTY_MAX = 0.971f;
float bucketDeg[4] = {0, 90, 180, 270};
uint8_t bucketIndex = 0;

float readAngleDeg(){

  //th and 
  unsigned long th = pulseIn(PIN_SERVO_FEEDBACK, HIGH, 2500);
  unsigned long tl = pulseIn(PIN_SERVO_FEEDBACK, LOW, 2500);
  if (!th || !tl) return NAN;

  unsigned long tc = th + tl;

  if (tc < CYCLE_MIN_US || tc > CYCLE_MAX_US) return NAN;

  float duty = (float)th/(float)tc;
  float angle = (duty - DUTY_MIN) * (360.0f/(DUTY_MAX - DUTY_MIN));

  if (angle < 0){
    angle = 0;
  }
  if (angle > 359){
    angle = 359;
  }
  return angle;

}


inline float wrapError(float target, float current){
  float e = target - current;
  while(e>180) e -= 360;
  while (e<-180) e += 360;
  return e;
}

bool vendSteps(uint8_t steps){
  for (uint8_t i=0; i < steps; i++){
    if (!step90()) return false;
  }
  return true;
}

bool goToAngle(float targetDegree, uint16_t timeout_ms= 2000){

  uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms){
    float current = readAngleDeg();
    if (isnan(current)){
      continue;
    }
    float error = -wrapError( targetDegree, current);

    if (fabsf(error) <= 5.0f) {
      drum.writeMicroseconds(NEUTRAL_US);
      return true;
    }

    int out = (int)(Kp * error);
    if (out > 75) out = 75;
    if (out < -75) out = -75;

    out += (error > 0 ? +OFFSET : -OFFSET);

    int cmd = NEUTRAL_US + out;
    if (cmd > MAX_FWD_SPEED){
      cmd = MAX_FWD_SPEED;
    }
    if (cmd < MAX_REV_SPEED){
      cmd = MAX_REV_SPEED;
    }
    /*
    if (fabsf(error) < 15.0f) {
      cmd = NEUTRAL_US + (error > 0 ? +6 : -6); //creep control
    }
    */
    
    drum.writeMicroseconds(cmd);
    delay(10);
  }

  drum.writeMicroseconds(NEUTRAL_US);
  return false; //jam/timeout condition occurred
}

bool step90(){
  uint8_t next = (bucketIndex + 1) % 4;
  bool ok = goToAngle(bucketDeg[next]);
  if (ok) bucketIndex = next;
  return ok;
}



void defineBucketsFromHome(){
  float a=NAN;
  for(int i = 0; i < 5 && isnan(a); i++){
    a = readAngleDeg();
  }
  if (isnan(a)){
    a = 0;
  }
  auto wrap360=[](float x){ x = fmodf(x,360.0f); return x < 0 ? x + 360.0f : x;
  };

  bucketDeg[0] = wrap360(a);
  bucketDeg[1] = wrap360(a + 90.0f);
  bucketDeg[2] = wrap360(a + 180.0f);
  bucketDeg[3] = wrap360(a + 270.0f);
  bucketIndex = 0;

}

bool vendPressed(){
  static bool last=HIGH;
  static uint32_t t0 = 0;
  bool now = digitalRead(PIN_VEND_BTN);
  if (now!=last){
    t0 = millis();
    last = now;
  }
  return (millis()-t0 > 20 && now ==LOW);
}

void setup() {

  Serial.begin(115200);
  delay(200);



  //EEPROM SECTION-----------------------------------------------------
  EEPROM.get(EE_ADDR_POWER_CYCLES, powerCycles);

  // If EEPROM is "blank" (0xFFFFFFFF), treat as 0
  if (powerCycles == 0xFFFFFFFFUL) {
    powerCycles = 0;
  }

  powerCycles++;                 // increment for this boot
  EEPROM.put(EE_ADDR_POWER_CYCLES, powerCycles);

  Serial.print(F("Power cycles: "));
  Serial.println(powerCycles);
  //------------------------------------------------------------------

  // put your setup code here, to run once:
  pinMode(PIN_VEND_BTN, INPUT_PULLUP);
  pinMode(PIN_1_BTN, INPUT_PULLUP);
  pinMode(PIN_2_BTN, INPUT_PULLUP);
  pinMode(PIN_3_BTN, INPUT_PULLUP);


  pinMode(PIN_COIN_SENSOR , INPUT);

  attachInterrupt(digitalPinToInterrupt(PIN_COIN_SENSOR) , onCoinISR, CHANGE);

  statusLed.begin();
  statusLed.show();

  coin1Led.begin();
  coin1Led.show();

  coin2Led.begin();
  coin2Led.show();

  coin3Led.begin();
  coin3Led.show();

  showCoinLEDs(0);

  drum.attach(PIN_SERVO_CTRL);
  drum.writeMicroseconds(NEUTRAL_US);
  delay(200);
  defineBucketsFromHome(); //capture position on start up to set "home" bucket from whatever physical position
  const Tier &t = TIERS[0];
  setRGB(t.r,t.g,t.b);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (coinFlag){
    coinFlag = false;
    if (coinBank < 255) coinBank++;
  }
//reflect selection color
  showSelectionColor();
  //render coin LEDS  clamped?
  showCoinLEDs(min<uint8_t>(coinBank, MAX_BANK));
 //auto vend logic
  if (coinBank > MAX_BANK){
    if(vendSteps(4)) coinBank -= 4;
    else {
      //jam or failure condition true
      setRGB(128,0,200);
      delay(300);
    }
  }


// manual vend
  if (vendPressed()){
    while (vendPressed()) {}
    delay(25);

  uint8_t qtyIndex = selectedIndex();
  const Tier &sel = TIERS[qtyIndex];

  if (coinBank >= sel.cost){
    if (vendSteps(sel.steps)){
      coinBank -= sel.cost;       // successful vend: charge coins
    } else {
      setRGB(128,0,200);          // purple = jam/timeout
      delay(300);
    }
  } else {
    // not enough coins: flash red, then restore tier color
    blinkRGB(255,0,0,120);
    setRGB(sel.r, sel.g, sel.b);
  }
}

  delay(5);
}
