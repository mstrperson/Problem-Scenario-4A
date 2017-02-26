#include <Thread.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include<LiquidCrystal.h>
#include <SPI.h>
#include <Wire.h>
#include "Adafruit_MAX31855.h"

#define ON LOW
#define OFF HIGH

#define EVAP_FULL_PIN 48
#define COOLER_FULL_PIN 49

// Display is connected to the extra pins on the Mega.
LiquidCrystal lcd(40, 41, 37, 35, 33, 31);
// Keep track of which stats to show.
// 0 - Mash, 1 - Evaporator, 2 - Feed
int displayMode = 0;

Thread displayThread = Thread();

// OneWire sensor bus
#define TEMP_PROBES_BUS 6

// These are the physical addresses of the three one-wire Dallas Sensor temperature probes.
byte mashTemperatureAddress[]         = { 0x28, 0x91, 0xDC, 0x20, 0x07, 0x00, 0x00, 0xED }, 
     evaporatorTemperatureAddress[]   = { 0x28, 0xBD, 0xC5, 0x1D, 0x07, 0x00, 0x00, 0x7C }, 
     coolingTankTemperatureAddress[]  = { 0x28, 0x46, 0x8E, 0x1D, 0x07, 0x00, 0x00, 0x6F };

// Initialize the OneWire bus.
OneWire oneWire(TEMP_PROBES_BUS);
DallasTemperature sensors(&oneWire);

// Digital pins hooked up to the various relays
#define FEED_PUMP_RELAY   7
#define COOLANT_RELAY     8

#define MASH_HEATER_RELAY     20
#define COOLANT_COOLER_RELAY  21

// control variables.
double mashTemperature        = 0;
double evaporatorTemperature  = 0;
double coolingTemperature     = 0;

// Keep track of which things are tuned on.
bool mashHeaterOn             = false;
bool coolerOn                 = false;
bool coolantPumpOn            = false;
bool feederOn                 = false;

// target temperatures for the different vats.
#define MASH_TARGET     27.5
#define EVAP_TARGET     80.0
#define FEED_TEMP_MAX   30.0

// Differential Temperature (radius of temperature range)
#define DT 1.5

Thread temperatureReadThread  = Thread();
Thread relayControlThread     = Thread();

// Read the temperatures from the different sensors.
// update the hotplate target temperature as needed.
void readTemperatures()
{
  sensors.requestTemperatures();
  delay(10);  // Wait for sensors to report.
  
  mashTemperature = sensors.getTempC(mashTemperatureAddress);
  evaporatorTemperature = sensors.getTempC(evaporatorTemperatureAddress);
  coolingTemperature = sensors.getTempC(coolingTankTemperatureAddress);
}

// control te relays based on the current state of the machine.
void relayControl()
{
  // if the evaporator is full and the cooler tank is not full.
  bool letItRest = (digitalRead(EVAP_FULL_PIN) == HIGH && digitalRead(COOLER_FULL_PIN) == LOW);
  // if both the evaporator and the cooler tank are full
  bool reactorLow = (digitalRead(EVAP_FULL_PIN) == HIGH && digitalRead(COOLER_FULL_PIN) == HIGH);
  // if the coolant tank is full, but the evaporator is not.
  bool coolantOverflow = (digitalRead(EVAP_FULL_PIN) == LOW && digitalRead(COOLER_FULL_PIN) == HIGH);

  if(letItRest)
  {
    Serial.println("Let it Rest");
  }
  if(reactorLow)
  {
    Serial.println("Reactor Low");
  }
  if(coolantOverflow)
  {
    Serial.println("Coolant Overflow");
  }
  
  if(letItRest || reactorLow)
  {
    digitalWrite(FEED_PUMP_RELAY, OFF);
    feederOn = false;
  }
  else if(coolantOverflow && (coolingTemperature < FEED_TEMP_MAX || mashTemperature < MASH_TARGET))
  {
    digitalWrite(FEED_PUMP_RELAY, ON);
    feederOn = true;
  }
  
  if(!mashHeaterOn && mashTemperature < MASH_TARGET - DT)
  {
    digitalWrite(MASH_HEATER_RELAY, ON);
    mashHeaterOn = true;
    Serial.println("Turned on the Mash Heater.");
  }
  else if(mashHeaterOn && mashTemperature > MASH_TARGET + DT)
  {
    digitalWrite(MASH_HEATER_RELAY, OFF);
    mashHeaterOn = false;
    Serial.println("Turned off the Mash Heater.");
  }

  if(!coolantPumpOn && evaporatorTemperature >= EVAP_TARGET)
  {
    digitalWrite(COOLANT_RELAY, ON);
    coolantPumpOn = true;
    Serial.println("Turned on the coolant pump.");
  }
  else if(coolantPumpOn && evaporatorTemperature < EVAP_TARGET - DT)
  {
    digitalWrite(COOLANT_RELAY, OFF);
    coolantPumpOn = false;
    Serial.println("Turend off the coolant pump.");
  }

  if(feederOn && ((!coolantOverflow && coolingTemperature > FEED_TEMP_MAX) || (coolantOverflow && mashTemperature > MASH_TARGET - DT)))
  {
    digitalWrite(FEED_PUMP_RELAY, OFF);
    feederOn = false;
    Serial.println("Feed is too hot!");
  }
  else if(!feederOn && !letItRest && (coolingTemperature < FEED_TEMP_MAX - DT || mashTemperature < MASH_TARGET - DT))
  {
    digitalWrite(FEED_PUMP_RELAY, ON);
    feederOn = true;
    Serial.println("Feeder Running.");
  }

}

// Update the LCD screen.
void displayCallback()
{
  lcd.clear();
  switch(displayMode)
  {
    case 0: 
      lcd.print("Mash: ");
      lcd.print(mashTemperature, 2);
      lcd.print(" C");
      lcd.setCursor(0,1);
      lcd.print("Target: ");
      lcd.print(MASH_TARGET);
      lcd.print(" C");
      break;
    case 1:
      lcd.print("Evap: ");
      lcd.print(evaporatorTemperature, 2);
      lcd.print(" C");
      lcd.setCursor(0,1);
      lcd.print("Target: ");
      lcd.print(EVAP_TARGET);
      lcd.print(" C");
      break;
    case 2:
      lcd.print("Feed: ");
      lcd.print(coolingTemperature, 2);
      lcd.print(" C");
      lcd.setCursor(0,1);
      lcd.print("Max: ");
      lcd.print(FEED_TEMP_MAX);
      lcd.print(" C");
      break;
    case 3:
      lcd.print(feederOn ? "Feed On" : "Feed Off");
      lcd.setCursor(0,1);
      lcd.print(coolantPumpOn ? "Coolant On" : "Coolant Off");
    default:
      break;
  }

  displayMode = (++displayMode) % 4;
}

// Initialize the machine
void setup() {

  pinMode(EVAP_FULL_PIN, INPUT_PULLUP);
  digitalWrite(EVAP_FULL_PIN, HIGH);
  pinMode(COOLER_FULL_PIN, INPUT_PULLUP);
  digitalWrite(COOLER_FULL_PIN, HIGH);

  lcd.begin(16,2);
  lcd.print("Setting things up");

  temperatureReadThread.onRun(readTemperatures);
  temperatureReadThread.setInterval(1999);

  pinMode(FEED_PUMP_RELAY, OUTPUT);
  digitalWrite(FEED_PUMP_RELAY, OFF);
  pinMode(COOLANT_RELAY, OUTPUT);
  digitalWrite(COOLANT_RELAY, OFF);
  pinMode(MASH_HEATER_RELAY, OUTPUT);
  digitalWrite(MASH_HEATER_RELAY, OFF);
  pinMode(COOLANT_COOLER_RELAY, OUTPUT);
  digitalWrite(COOLANT_COOLER_RELAY, OFF);  

  relayControlThread.onRun(relayControl);
  relayControlThread.setInterval(3000);
  sensors.begin();

  displayThread.onRun(displayCallback);
  displayThread.setInterval(6000);

  Serial.begin(115200);

  delay(500);
  Serial.println("Ready!");
}

void loop() {

  if(temperatureReadThread.shouldRun())
    temperatureReadThread.run();

  if(relayControlThread.shouldRun())
    relayControlThread.run();

  if(displayThread.shouldRun())
    displayThread.run();
}
