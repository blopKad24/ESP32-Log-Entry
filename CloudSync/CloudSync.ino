// NAKUL KADAM - CLOUDSYNC

#include <Wire.h>               //Handles the I2C communication
#include <LiquidCrystal_I2C.h>  //Needed to operate LCD (lcd.print(), lcd.setCursor(), lcd.clear() etc.)
#include <Keypad.h>             //Handles reading which key is pressed

#include <WiFi.h>               //To use WiFi.begin(), WiFi.status() 
#include <HTTPClient.h>         //To use http.POST(body) to send data to MockAPI
#include <ArduinoJson.h>        //Allows JSON formatted text (doc[role]=...)
#include <NTPClient.h>          //To get current time (timeClient.getFormattedTime())
#include <WiFiUdp.h>            //NTPClient uses this library internally

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2); //object lcd represents our LCD display here

// ================= FSM =================
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
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27}; //hardware connections for ROWS
byte colPins[COLS] = {26, 25, 33, 32}; //harware connections for COLS
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); //formats the array and gives it to the Keypad library

// ================= OUTPUTS PINS DECLARATION =================
const int GREEN_LED = 18;
const int BLUE_LED = 19;
const int RED_LED = 23;
const int BUZZER = 4;
const int EMERGENCY_BUTTON = 17;

// ================= PEEP (Our database for the valid users) =================
struct Peep {                 //Each valid user will be recognized by it's id & role
  String id;
  String role;
};

Peep peeps[] = {
  {"1234", "ADMIN"},          //Normal SHIELD workers. GREEN led upon entry
  {"1918", "CAPTAIN ROGERS"}, //Captain Steve Rogers. BLUE led upon entry
  {"2007", "AGENT ROMANOFF"}, //Agent Natasha Romanoff. RED led upon entry
  {"4004", "HYDRA"}           //Secret agents of HYDRA. Keep their entry sneaky
};

const int PEEP_COUNT = sizeof(peeps) / sizeof(peeps[0]); //Count is total/size_of_one

// ================= VARIABLES =================
String enteredID = "";   //Store the entered id. Resets to "" by default
String grantedRole = ""; //Store the role for the current enteredID. Resets to "" by default

int failedAttempts = 0;  //failed attempts counter

unsigned long lastKeyTime = 0;             //timer since last key pressed (needed to detect timeout)
const unsigned long INPUT_TIMEOUT = 7000;  //timeout set at 7s

unsigned long stateStart = 0;              //timer at start of every state
unsigned long lockoutStart = 0;            //timer during lockout
const unsigned long LOCKOUT_TIME = 15000;  //lockout for 15s

unsigned long blinkTimer = 0;              //timer for counting the LED on/off during lockout
bool blinkState = false;                   //the LED input which we flicker during lockout
bool emergencyTriggered = false;
bool currentButton = HIGH;                 //Emergency button kept HIGH as default. EMERGENCY detected on negedge
bool lastButtonState = HIGH;               //Store the prev state of the button

//WiFi/NTP
bool manualWiFiOff = false;             //bit that changes as per WiFi being on or off (pressing C or D)
const char* WIFI_SSID  = "Wokwi-GUEST"; //Network name
const char* WIFI_PASS  = "";            //Password (no password here)
const char* SERVER_URL = "https://6a398bb564a2d82692241c71.mockapi.io/logs"; //Put the URL of our base.

WiFiUDP ntpUDP; //Allows communication for NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); //Creates NTPClient. Communation channel is ntpUDP, timezone is +5:30 = 19800s, reconnect to server every 60s

//Offline queue structure
struct LogEntry {
  String role;
  String timestamp;
  bool granted;     //true for access granted, false for acces denied (but we arent logging the denied ones, so false can be ignored)
};

const int MAX_QUEUE = 10;         //max 10 offline queues possible
LogEntry offlineQueue[MAX_QUEUE]; //keep the array ready for 10 entries
int queueCount = 0;               //queue counter

//Reconnect Timer
unsigned long lastReconnectAttempt = 0;         //time counter for when was the last reconnect attempt
const unsigned long RECONNECT_INTERVAL = 10000; //wait 10 seconds between attempts


// ================= FUNCTION PROTOTYPING =================

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

void flushQueue();
void connectWiFi();
bool postLog(LogEntry & entry);
void queueLog(LogEntry & entry);


// =================================================

void setup() {
  Serial.begin(115200); //115200 baud is standard for ESP32

  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(EMERGENCY_BUTTON, INPUT_PULLUP); //PULLUP needed here because the value remains HIGH by default

  lcd.init();      //Initialize the LCD to display text whenever received
  lcd.backlight(); //Turns on the LED backlight behind the display

  connectWiFi();      //Connect to WiFi at every start
  timeClient.begin(); //Initialize the UDP socket and ger the first time reading from the NTP server

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access System");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  stateStart = millis();
  currentState = STARTUP;
}

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    if ((!manualWiFiOff) && (millis() - lastReconnectAttempt > RECONNECT_INTERVAL)) { //if WiFi has not been connected, reconnect
      lastReconnectAttempt = millis();
      Serial.println("CONNECTING...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    else {
      timeClient.update();  //Update the time if 60s is passed; if yes, NTP server resyncs the time
      flushQueue();
    }
  }

  switch (currentState) {

    case STATE_IDLE:
      handleKeypad();
      break;

    case ENTERING:
      handleKeypad();
      handleTimeout();
      break;

    case GRANTED:
      if (millis() - stateStart > 2000) { //wait of 2s so that the LCD text is read
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case DENIED:
      if (millis() - stateStart > 2000) { //wait of 2s so that the LCD text is read
        if (failedAttempts >= 3) {
          currentState = LOCKOUT;
          lockoutStart = millis();

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("SECURITY LOCK");
          lcd.setCursor(0, 1);
          lcd.print("15 Seconds");
        }
        else {
          showPrompt();
          currentState = STATE_IDLE;
        }
      }
      break;

    case LOCKOUT: 
      handleLockoutBlink(); //run the lockout blinking
      currentButton = digitalRead(EMERGENCY_BUTTON);

      if (lastButtonState == HIGH && currentButton == LOW) //detect negedge
      {
        currentState = EMERGENCY;
        stateStart = millis();
        emergencySequence();
      }
      lastButtonState = currentButton;
      if (millis() - lockoutStart >= LOCKOUT_TIME) {  //After 15s go back to normal
        failedAttempts = 0;
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case EMERGENCY:

      if (millis() - stateStart > 5000) { //wait of 5s so that the LCD text is read
        failedAttempts = 0;
        clearLEDs();
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case STARTUP:
      if (millis() - stateStart >= 1500) {
        showPrompt();
        currentState = STATE_IDLE;
      }
      break;

    case TIMEOUT_MSG:
      if (millis() - stateStart >= 1000) { //wait of 1s so that the LCD text is read
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
  
  if(key == 'C') {
    if(!manualWiFiOff) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Already");
        lcd.setCursor(0, 1);
        lcd.print("Connected!");
      }
    else {
        manualWiFiOff=false;
        connectWiFi();
        lcd.setCursor(0, 0);
        lcd.print("Connecting...");
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
    enteredID="";
    stateStart=millis();
    currentState = TIMEOUT_MSG;

    return; 
  }

  if(key=='D') {
    if (manualWiFiOff) { 
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Already");
        lcd.setCursor(0, 1);
        lcd.print("Disconnected!");
      return;
    }
    
    manualWiFiOff = true;
    Serial.println("DISCONNECTING : manualWiFiOff set to TRUE");

    WiFi.disconnect();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Disabled");
    lcd.setCursor(0, 1);
    lcd.print("Manually");
    enteredID = "";
    stateStart = millis();
    currentState = TIMEOUT_MSG;
    return; 
  }

  lastKeyTime = millis();

  if(currentState == STATE_IDLE)
    currentState = ENTERING;

  if(key >= '0' && key <= '9') {

    if (enteredID.length() < 4) {
      enteredID += key;
      updateMaskedInput();
    }
  }

  else if (key == '*') {

    if (enteredID.length() > 0) {
      enteredID.remove(enteredID.length() - 1); //careful with index. Length goes from 1 to 4, but String is stored from 0 to 3 
      updateMaskedInput();
    }
  }

  else if (key == '#') {

    int idx = findPeep(enteredID);

    enteredID = "";

    if (idx >= 0) {
      enterGrantedState(peeps[idx].role);
    }
    else {
      failedAttempts++;
      enterDeniedState();
    }
  }
}


// =================================================

void handleTimeout() {

  if (enteredID.length() == 0) 
    return;   //Nothing entered.Stay in STATE_IDLE

  if (millis() - lastKeyTime >= INPUT_TIMEOUT) { //timeout after 7s

    enteredID = "";

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Input Timeout");
    lcd.setCursor(0, 1);
    lcd.print("Cleared");

    stateStart = millis();
    currentState = TIMEOUT_MSG;
  }
}

// =================================================

void enterGrantedState(String role) {

  grantedRole = role;

  LogEntry entry;         //Create a blank log entry which contains the three info sent to API
  entry.role    = role;   //Enter the role
  entry.granted = true;   //true because access granted
  entry.timestamp = timeClient.getFormattedTime(); //NTP stores the time of entry

  if (!manualWiFiOff  &&  WiFi.status() == WL_CONNECTED) { //if entry made when WiFi connected
    if (!postLog(entry)) {                                 //WiFi is connected, but it may happen that API returns an error. So offline queue it
      queueLog(entry); 
    }
  }
  else {
    queueLog(entry); //WiFi is disconnected, so offline entry needed
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Welcome");
  lcd.setCursor(0, 1);
  lcd.print(role);

  clearLEDs();

  if (role == "ADMIN") {
    digitalWrite(GREEN_LED, HIGH);
    tone(BUZZER, 1200, 600);
  }
  else if (role == "CAPTAIN ROGERS") {
    digitalWrite(BLUE_LED, HIGH);
    tone(BUZZER, 900, 500);
  }
  else if (role == "AGENT ROMANOFF") { 
    digitalWrite(RED_LED, HIGH);
    tone(BUZZER, 900, 350);
  }
  else {
    // Empty because no buzzer/LED needed for HYDRA
  }

  stateStart = millis();
  currentState = GRANTED;
}

// =================================================

void enterDeniedState() {

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Access Denied");
  lcd.setCursor(0, 1);
  lcd.print("Attempt ");
  lcd.print(failedAttempts);

  tone(BUZZER, 300, 450);

  stateStart = millis();
  currentState = DENIED;
}

// =================================================

void handleLockoutBlink() {

  if (millis() - blinkTimer > 300) {

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
  lcd.setCursor(0, 0);
  lcd.print("Emergency Access");
  lcd.setCursor(0, 1);
  lcd.print("Dir. Nick Fury");
  }


// =================================================

void showPrompt() {

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter 4-Digit ID");
  lcd.setCursor(0, 1);
}

// =================================================

void updateMaskedInput() {

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);

  for (int i = 0; i < enteredID.length(); i++)
    lcd.print("*");
}

// =================================================

int findPeep(String id) {

  for (int i = 0; i < PEEP_COUNT; i++) {

    if (peeps[i].id == id)
      return i;
  }

  return -1;
}

// =================================================

void clearLEDs() {

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(RED_LED, LOW);
}

// =================================================

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);  //use .begin() of WiFi which connects to Wokwi-GUEST network by taking password ""
}

// =================================================

bool postLog(LogEntry &entry) {                         //here, instead of sending entire copy of entry which has many String objects inside, postLog directly references the entry

  Serial.println("postLog called. manualWiFiOff=" + String(manualWiFiOff) );   //Safety print. This line showed the WiFi status during every entry

  if (manualWiFiOff  ||  WiFi.status() != WL_CONNECTED) //If WiFi not connected...
    return false;                                       //Why even post it on API?

  HTTPClient http;                                    //http is our variable to access HTTP communcation
  http.begin(SERVER_URL);                             //takes the MockAPI URL and begins communication
  http.addHeader("Content-Type", "application/json"); // tell server it's JSON

  StaticJsonDocument<256> doc;                        //create a JSON container(256 bytes max)
  doc["role"]      = entry.role;
  doc["granted"]   = entry.granted;
  doc["timestamp"] = entry.timestamp;

  String body;
  serializeJson(doc, body);   //formats the JSON object to text (which is seen on the website)

  int code = http.POST(body); //.POST() sends the body and the response from API is stored in code (whether logged successfully or not)
  http.end();                 //End communication

  return (code==200 || code==201); //true=success, false=failed
}

// =================================================

void queueLog(LogEntry &entry) {        //here, instead of sending entire copy of entry which has many String objects inside, queueLog directly references the entry
  if (queueCount < MAX_QUEUE) {
    offlineQueue[queueCount++] = entry; //log it in next empty slot
    Serial.println("Queued offline. Count : " + String(queueCount) );
  }
}

// =================================================

void flushQueue() {
  if (manualWiFiOff  ||  WiFi.status()!=WL_CONNECTED || queueCount==0) //do nothing if either WiFi off or nothing to upload in API
    return; 

  Serial.println("Flushing " + String(queueCount) + " queued logs...");

  int sent = 0;
  for (int i = 0; i<queueCount; i++) {
    if (postLog(offlineQueue[i])) 
      sent++;
    else 
      break; // stop if mid-flush the connection drops again
  }
  
  if (sent==queueCount) { //All entries were made. Go back to default settings. If all entries weren't made, shift the remaining back to top of the queue
    queueCount = 0;
    Serial.println("Queue cleared.");
  } else {
    // shift unset entries to front
    for (int i = 0; i< queueCount-sent; i++)
      offlineQueue[i] = offlineQueue[i + sent];

    queueCount -= sent;
  }
}

// =================================================
