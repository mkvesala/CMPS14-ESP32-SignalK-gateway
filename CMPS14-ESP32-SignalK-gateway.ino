#include <Arduino.h> 
#include "CMPS14Application.h"

CMPS14Application app;

void setup() {

  Serial.begin(115200);
  delay(47);

  app.begin();

  if (!app.compassOk()) { 
    Serial.println("CMPS14 INIT FAILED! CHECK SYSTEM!");
    while(true); 
  }
  
}

void loop() {

  app.loop();

} 
