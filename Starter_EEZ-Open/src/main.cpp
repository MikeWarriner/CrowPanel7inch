#include <PCA9557.h>
#include <lvgl.h>
#include <Crowbits_DHT20.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ui/ui.h"
#include "ui/vars.h"
#include "ui/actions.h" 
#include "lgfx/lgfx.h"

// Setup the panel.
void setup()
{
  // Three second delay to wait for serial monitor to be running.  Not sure this is necessary going forward
  delay(1000);
  Serial.begin(115200);
  delay(2000);

  Serial.println("Running setup...");
  set_var_label_count_value("-");

  // Setup the panel
  lcd.setup();

  // Initialize the UI
  ui_init();

  // Run the LVGL timer handler once to get things started
  lv_timer_handler();
}

int clickCount = 0;

char labelValue[512];
extern const char *get_var_label_count_value()
{
  return labelValue;
}
extern void set_var_label_count_value(const char *value)
{
  Serial.println("set_var_label_count_value");
  Serial.println(value);
  strlcpy(labelValue, value, sizeof(labelValue));
}

// Handle Click event
void action_button_click_action(lv_event_t *e)
{
  Serial.println("action_button_click_action");
  clickCount++;
  char ClickBuffer[20];
  snprintf(ClickBuffer, sizeof(ClickBuffer), "%d", clickCount);
  set_var_label_count_value(ClickBuffer);
}

// Run Ardunio event loop
void loop()
{
  ui_tick();
  lv_timer_handler(); /* let the GUI do its work */
  delay(10);
}
