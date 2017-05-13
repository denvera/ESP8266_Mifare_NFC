#ifdef USE_SSD1306
#include <brzo_i2c.h>
#include "SSD1306Brzo.h"

SSD1306Brzo  display(0x3c, D2, D1);

unsigned long lastActive = 0;

void initScreen() {
  display.init();
  display.clear();
  display.flipScreenVertically();  
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 20, "Initializing...");
  display.display();
  lastActive = millis();
}

void readTagScreen(String msg, int percent) {   
  lastActive = millis();
  display.clear();
  // draw the percentage as String
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, msg);
  display.drawString(64, 30, String(percent) + "%");
  display.drawProgressBar(0, 50, 120, 10, percent);
  display.display();
}

void idleScreen() {
  display.clear();
  if ((millis() - lastActive < 30000) && (lastActive > 0)) {      
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 20, "Waiting for tag..");    
  }
  display.display();
}


#else
// Just fill with empty stubs if screen not used
void initScreen() {}
void idleScreen() {}
void readTagScreen(String msg, int percent) {}
#endif
