#include "globals.h"
#include "CMPS14Application.h"

CMPS14Application app;

void setup() {

  Serial.begin(115200);
  delay(47);

  app.begin();
  app.status();
  if (!app.compassOk()) { while(true); }
}

void loop() {

  app.loop();

} 
