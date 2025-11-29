#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C screen(U8G2_R0, U8X8_PIN_NONE, SCL, SDA);

void screenDisplay(void *parameters){
  uint8_t i=0;
  screen.clear();
  for(;;){
    screen.setFont(u8g2_font_helvR10_te);
    screen.setCursor(0,10);
    screen.printf("Hello World!");
    screen.setDrawColor(0);
    screen.drawBox(0,55,10,10);
    screen.setDrawColor(1);
    screen.setCursor(0,55);
    screen.printf("%d", i);
    screen.sendBuffer();
    i++;
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
}

void setup(){
  Serial.begin(115200);
  delay(1000);

  Wire.begin();
  screen.begin();

  xTaskCreatePinnedToCore(screenDisplay,
    "Screen Test Code",
    4000,
    NULL,
    1,
    NULL,
    1
  );
}

void loop(){

}