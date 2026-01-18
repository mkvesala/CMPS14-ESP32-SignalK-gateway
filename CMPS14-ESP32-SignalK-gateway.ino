#include <Arduino.h> 
#include "CMPS14Application.h"

// === M A I N  P R O G R A M ===
//
// - Owns the CMPS14Application instance
// - Initiates Serial
// - Initiates the app
// - Stops before loop() if the compass of the app is not initialized
// - Executes app.loop() within main loop()

CMPS14Application app;

void setup() {

  Serial.begin(115200);
  delay(47);

  app.begin();

  if (!app.compassOk()) { 
    Serial.println("CMPS14 INIT FAILED! CHECK SYSTEM!");
    while(1) delay(1999); // Stop here if no compass
  }
  
}

void loop() {

  app.loop();

} 
