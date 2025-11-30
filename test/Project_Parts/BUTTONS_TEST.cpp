#include <Arduino.h>

#define OLED_BUTTON 18

unsigned long lastTimeInterrupted = 0;
unsigned long debounceTime = 200;

SemaphoreHandle_t buttonSemaphore_handle = NULL;

void IRAM_ATTR buttonISR(){
  unsigned long currentTime = millis(); 
  if(currentTime - lastTimeInterrupted > debounceTime){
    BaseType_t higherPriorityWoken = pdFALSE;
    xSemaphoreGiveFromISR(buttonSemaphore_handle, &higherPriorityWoken);
    lastTimeInterrupted = currentTime;
    if(higherPriorityWoken){
      portYIELD_FROM_ISR();
    }
  }
}

void printSerial(void *parameters){
  for(;;){
    if(xSemaphoreTake(buttonSemaphore_handle, portMAX_DELAY)){
      Serial.println("INTERRUPTED SUCCESSFULLY");
    }
  }

}

void setup(){
  Serial.begin(115200);
  pinMode(OLED_BUTTON, INPUT_PULLUP); 
  buttonSemaphore_handle = xSemaphoreCreateBinary();
  
  if(buttonSemaphore_handle == NULL){
    Serial.println("FAILED TO CREATE SEMAPHORE");
    while(1){
    }
  }

  attachInterrupt(digitalPinToInterrupt(OLED_BUTTON), buttonISR, FALLING);

  xTaskCreatePinnedToCore(
    printSerial,
    "print to Serial Task", 
    4000,
    NULL,
    1,
    NULL,
    1
  );
}

void loop(){

}