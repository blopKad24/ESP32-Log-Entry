#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= FSM ================= IDLE conflicting tha
enum State {
  STATE_IDLE,
  ENTERING,
  GRANTED,
  DENIED,
  LOCKOUT,
  EMERGENCY,
  STARTUP,
  TIMEOUT_MSG
};

State currentState = STATE_IDLE;

// ================= KEYPAD =================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {13,12,14,27};
byte colPins[COLS] = {26,25,33,32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= OUTPUTS =================
const int GREEN_LED = 18;
const int BLUE_LED = 19;
const int RED_LED = 23;
const int BUZZER = 4;
const int EMERGENCY_BUTTON = 17;

// ================= PEEPS =================
struct Peep {
  String id;
  String role;
};

Peep peeps[] = {
  {"1234","ADMIN"},           // Normal SHIELD workers. GREEN led upon entry.
  {"1918","CAPTAIN ROGERS"},  // Captain Steve Rogers, Captain America. BLUE led upon entry.
  {"2007","AGENT ROMANOFF"},  // Agent Natasha Romanoff. RED led upon entry.
  {"4004","HYDRA"}            // SUSH! Secret agents of HYDRA. Keep the entry as sneaky as possible. Hail Hydra!
};

const int PEEP_COUNT = sizeof(peeps)/sizeof(peeps[0]);

// ================= VARIABLES =================
String enteredID = "";
String grantedRole = "";

int failedAttempts = 0;

unsigned long lastKeyTime = 0;
const unsigned long INPUT_TIMEOUT = 7000;

unsigned long stateStart = 0;
unsigned long lockoutStart = 0;

const unsigned long LOCKOUT_TIME = 15000;

unsigned long blinkTimer = 0;
bool blinkState = false;
bool emergencyTriggered = false;
bool currentButton = HIGH;
bool lastButtonState = HIGH;
// =================================================

void showPrompt();
void clearLEDs();
void updateMaskedInput();
int findPeep(String id);
void handleKeypad();
void handleTimeout();
void enterGrantedState(String role);
void enterDeniedState();
void handleLockoutBlink();
void emergencySequence();

// =================================================

void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(EMERGENCY_BUTTON, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Access System");
  lcd.setCursor(0,1);
  lcd.print("Starting...");

  stateStart = millis();
  currentState = STARTUP;
}

void loop() {

  switch(currentState) {

    case STATE_IDLE:
      handleKeypad();
      break;

    case ENTERING:
      handleKeypad();
      handleTimeout();
      break;

    case GRANTED:
      if(millis() - stateStart > 2000) {
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case DENIED:
      if(millis() - stateStart > 1000) {
       if(failedAttempts>=3) {
         currentState=LOCKOUT;
         lockoutStart=millis();

         lcd.clear();
         lcd.setCursor(0,0);
         lcd.print("SECURITY LOCK");
         lcd.setCursor(0,1);
         lcd.print("15 Seconds");
       }
       else{
         showPrompt();
         currentState = STATE_IDLE;
       }
      }
      break;

    case LOCKOUT:
     
     currentButton = digitalRead(EMERGENCY_BUTTON);

     if(lastButtonState==HIGH && currentButton==LOW)
     {
      currentState= EMERGENCY;
      stateStart=millis();
      emergencySequence();
     }
     lastButtonState=currentButton;
      if(millis() - lockoutStart >= LOCKOUT_TIME) { //sus
        failedAttempts = 0;
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case EMERGENCY:

      if(millis() - stateStart > 5000) {
        failedAttempts = 0;
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case STARTUP:
      if(millis() - stateStart >= 1500){
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case TIMEOUT_MSG:
      if(millis() - stateStart >= 1000){
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;
  }
}

// =================================================

void handleKeypad() {

  char key = keypad.getKey();

  if(!key) return;

  lastKeyTime = millis();

  if(currentState == STATE_IDLE)
    currentState = ENTERING;

  if(key >= '0' && key <= '9') {

    if(enteredID.length() < 4) {
      enteredID += key;
      updateMaskedInput();
    }
  }

  else if(key == '*') {

    if(enteredID.length() > 0) {
      enteredID.remove(enteredID.length()-1);
      updateMaskedInput();
    }
  }

  else if(key == '#') {

    int idx = findPeep(enteredID);

    enteredID = "";

    if(idx >= 0) {
      enterGrantedState(peeps[idx].role);
    }
    else {

      failedAttempts++;

      if(failedAttempts >= 3) {
        enterDeniedState();
      }
      else {
        enterDeniedState();
       }
      }
    }
  }
// }

// =================================================

void handleTimeout() {

  if(enteredID.length() == 0) return;

  if(millis() - lastKeyTime >= INPUT_TIMEOUT) {

    enteredID = "";

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Input Timeout");
    lcd.setCursor(0,1);
    lcd.print("Cleared");

    stateStart = millis();
    currentState = TIMEOUT_MSG;
  }
}

// =================================================

void enterGrantedState(String role) {

  grantedRole = role;

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Welcome");
  lcd.setCursor(0,1);
  lcd.print(role);

  clearLEDs();

  if(role == "ADMIN") {
    digitalWrite(GREEN_LED,HIGH);
    tone(BUZZER,1200,600);
  }
  else if(role == "CAPTAIN ROGERS") {
    digitalWrite(BLUE_LED,HIGH);
    tone(BUZZER,900,500);
  }
  else if(role == "AGENT ROMANOFF") {
    digitalWrite(RED_LED,HIGH);
    tone(BUZZER,900,350);
  }
  else {
    // digitalWrite(RED_LED,HIGH);
    // digitalWrite(BLUE_LED,HIGH);
    // digitalWrite(GREEN_LED,HIGH);
    // tone(BUZZER,700,150);
  }

  stateStart = millis();
  currentState = GRANTED;
}

// =================================================

void enterDeniedState() {

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Access Denied");
  lcd.setCursor(0,1);
  lcd.print("Attempt ");
  lcd.print(failedAttempts);

  tone(BUZZER,300,450);

  stateStart = millis();
  currentState = DENIED;
}

// =================================================

void handleLockoutBlink() {

  if(millis() - blinkTimer > 300) {

    blinkTimer = millis();
    blinkState = !blinkState;

    digitalWrite(GREEN_LED, blinkState);
    digitalWrite(BLUE_LED, blinkState);
    digitalWrite(RED_LED, blinkState);
  }
}

// =================================================

void emergencySequence() {

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Emergency Access");
  lcd.setCursor(0,1);
  lcd.print("Dir. Nick Fury");

  for(int i=0;i<8;i++) {

    digitalWrite(GREEN_LED,HIGH);
    digitalWrite(BLUE_LED,HIGH);
    digitalWrite(RED_LED,HIGH);

    tone(BUZZER,1500,150);

    digitalWrite(GREEN_LED,LOW);
    digitalWrite(BLUE_LED,LOW);
    digitalWrite(RED_LED,LOW);

    noTone(BUZZER);
  }
}

// =================================================

void showPrompt() {

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Enter 4-Digit ID");
  lcd.setCursor(0,1);
}

// =================================================

void updateMaskedInput() {

  lcd.setCursor(0,1);
  lcd.print("                ");
  lcd.setCursor(0,1);

  for(int i=0;i<enteredID.length();i++)
    lcd.print("*");
}

// =================================================

int findPeep(String id) {

  for(int i=0;i<PEEP_COUNT;i++) {

    if(peeps[i].id == id)
      return i;
  }

  return -1;
}

// =================================================

void clearLEDs() {

  digitalWrite(GREEN_LED,LOW);
  digitalWrite(BLUE_LED,LOW);
  digitalWrite(RED_LED,LOW);
}
