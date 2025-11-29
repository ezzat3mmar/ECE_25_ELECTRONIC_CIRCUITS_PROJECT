#include <Arduino.h>

#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <DHT.h>

#define DHTPIN 13
#define DHTTYPE DHT11

#define PULSE_PIN 33

DHT_Unified dht(DHTPIN, DHTTYPE);

TaskHandle_t readDHT_handle;

void readDHT(void* parameters){
  for(;;){
    vTaskDelay(1000 / portTICK_RATE_MS);

    sensors_event_t event;
    dht.temperature().getEvent(&event);

    if(isnan(event.temperature)){
      Serial.println("DHT11 FAILED TO READ TEMP");
    }else{
      Serial.print("DHT11 Temp = ");
      Serial.print(event.temperature);
      Serial.println("°C");
    }

    dht.humidity().getEvent(&event);

    if(isnan(event.relative_humidity)){
      Serial.println("DHT11 FAILED TO READ RELATIVE HUMIDITY"); 
    }else{
      Serial.print("DHT11 REL_HUMIDITY = ");
      Serial.print(event.relative_humidity);
      Serial.println("%");  
    }

    Serial.print("Free Stack DHT: ");
    Serial.println(uxTaskGetStackHighWaterMark(readDHT_handle));
  }
}

void readPulseSensor(void *parameters){
  uint32_t lastPeakTime = 0;
  uint16_t threshold = 2650;
  uint32_t timeDelay = 600;
  bool pulseOcurred = false;

  uint8_t READING_NUM = 10;
  uint8_t CURRENT_INDEX = 0;
  uint16_t lastPulses[READING_NUM] = {0,0,0,0,0,0,0,0,0,0};

  uint16_t signal;
  uint16_t BPM = 0;
  uint16_t tempBPM;

  for(;;){
    signal = analogRead(PULSE_PIN);

    if(signal > threshold && !pulseOcurred){
      
      uint32_t currentTime = millis();

      if(currentTime - lastPeakTime > timeDelay){

        tempBPM = 60000 / (currentTime - lastPeakTime);
        
        if(tempBPM>40 && tempBPM<120){
          lastPulses[CURRENT_INDEX] = tempBPM;
          CURRENT_INDEX = (CURRENT_INDEX+1)%10;
        }

        lastPeakTime = currentTime;
        pulseOcurred = !pulseOcurred;

      }

      uint16_t sumBPM = 0;
      uint8_t validReading = 0;

      for(int i=0;i<=READING_NUM;i++){
        if(lastPulses[i]>0){
            sumBPM += lastPulses[i];
            validReading++;
        }
      }
      
      if(validReading>0){
        BPM = sumBPM/validReading;
      }

      Serial.print("Current BPM = ");
      Serial.println(BPM);

    }


    if(signal < (threshold-100) && pulseOcurred){
      pulseOcurred = !pulseOcurred;
    }

    vTaskDelay(10/portTICK_PERIOD_MS);

  }
}

void setup(){
  Serial.begin(115200);

  dht.begin();

  xTaskCreatePinnedToCore(
    readDHT,
    "DHT SENSOR READING TASK",
    2048,
    NULL,
    1,
    &readDHT_handle,
    1    
  );

  xTaskCreatePinnedToCore(
    readPulseSensor,
    "",
    3000,
    NULL,
    1,
    NULL,
    1
  );
}

void loop(){

}