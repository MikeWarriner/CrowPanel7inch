#include <PCA9557.h>
#include <lvgl.h>
#include <Crowbits_DHT20.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ui/ui.h"
#include "lgfx/lgfx.h"

// Setup the panel.
void setup()
{
  // Three second delay to wait for serial monitor to be running.  Not sure this is necessary going forward
  delay(1000);
  Serial.begin(115200);
  delay(2000);

  Serial.println("Running setup...");

  // Setup the panel
  lcd.setup();

  // Initialize the Square Line UI
  ui_init();

  // Run the LVGL timer handler once to get things started
  lv_timer_handler();
}

int clickCount = 0;

// Handle Click event
void clickedClickMe(lv_event_t *e)
{
  clickCount++;
  char ClickBuffer[20];
  snprintf(ClickBuffer, sizeof(ClickBuffer), "%d", clickCount);
  lv_label_set_text(ui_LabelCount, ClickBuffer);
}

// Run Ardunio event loop
void loop()
{
  lv_timer_handler(); /* let the GUI do its work */
  delay(10);
}
