#include <Arduino.h>
/*  DHT11 Sensor required dependecies  */
#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <DHT.h>

#include <WiFi.h>
/*  Internal RTC lib  */
#include <ESP32Time.h>

#include <Wire.h>
/*  The standard lib for SH106 drivers  */
#include <U8g2lib.h>
/*  Parsing and getting JSON APIs  */
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#define SCREEN_BUTTON 18
#define DEBOUNCE_TIME 250

unsigned long lastScreenChangeTime = 0;

#define DHTPIN 13
#define DHTTYPE DHT11

#define PULSE_PIN 33

DHT_Unified dht(DHTPIN, DHTTYPE);

#define SSID "Ahmed"
#define PASSWORD "*asdf1234#"

ESP32Time rtc(0);

#define gmOffset 7200     // (GMT+2) in seconds
#define dayLightSaving 0 
#define ntpServer1 "pool.ntp.org"
#define ntpServer2 "time.nist.gov" 

String city = "Tanta"; 
String countryCode = "EG";
String APIKey = "58e56f237bc3057843fa4f3a8052a738";
String openWeatherUrl = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + APIKey;

typedef struct{
  String date;
  String time;
  String AmPm;
}timeStrings;

typedef struct{
  float temp;       // stands for Temperature obviusly!!
  float rh;         // stands for relative humidiy
}DHT_sensor_data;

typedef struct{
  String description;
  float tempFeelLike;
  float humidity;
  float windSpeed;
}openWeatherJSONParsed;

openWeatherJSONParsed weatherInfo;

WiFiClient client;
HTTPClient httpClient;

U8G2_SH1106_128X64_NONAME_F_HW_I2C screen(U8G2_R0, U8X8_PIN_NONE, SCL, SDA);

TaskHandle_t readDHT_handle;
TaskHandle_t readPulseSensor_handle;
TaskHandle_t readRTC_handle;
TaskHandle_t screenDisplay_handle;

QueueHandle_t screenRTCQueue_handle;

QueueHandle_t screenDHTQueue_handle;
#define SCREEN_DHT_QUEUE_SIZE 5

QueueHandle_t screenPulseQueue_handle;
#define SCREEN_PULSE_QUEUE_SIZE 10

QueueHandle_t screenOpenWeather_handle;
#define SCREEN_WEATHER_API_QUEUE_SIZE 1

SemaphoreHandle_t screenDisplaySemaphore_handle;

void IRAM_ATTR screenButtonISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastScreenChangeTime > DEBOUNCE_TIME){
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(screenDisplaySemaphore_handle, &higherPriorityTaskWoken);
    Serial.println("SEMAPHORE DONEEEEEEEEEEEEEEEEEEEEEEE EEEE");
    lastScreenChangeTime = currentTime;
    if(higherPriorityTaskWoken){
      portYIELD_FROM_ISR();
    }
  }
}

/*
void testInterrupt(void *parameters){
  for(;;){
    if(xSemaphoreTake(screenDisplaySemaphore_handle, portMAX_DELAY)){
      Serial.println("INTERRUPTTED SUCCESSFULLY");
    }
  }
}
*/

void readDHT(void* parameters){
  DHT_sensor_data TempRHvalues;

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

      TempRHvalues.temp = event.temperature;
    }

    dht.humidity().getEvent(&event);

    if(isnan(event.relative_humidity)){
      Serial.println("DHT11 FAILED TO READ RELATIVE HUMIDITY"); 
    }else{
      Serial.print("DHT11 REL_HUMIDITY = ");
      Serial.print(event.relative_humidity);
      Serial.println("%");  

      TempRHvalues.rh = event.relative_humidity;
    }

    xQueueSend(screenDHTQueue_handle, &TempRHvalues, portMAX_DELAY);

    Serial.print("Free Stack DHT: ");
    Serial.println(uxTaskGetStackHighWaterMark(readDHT_handle));
  }
}

void readPulseSensor(void *parameters){
  uint32_t lastPeakTime = 0;
  uint16_t threshold = 2650;
  uint32_t timeDelay = 400;
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

      xQueueSend(screenPulseQueue_handle, &BPM, portMAX_DELAY);

    }


    if(signal < (threshold-100) && pulseOcurred){
      pulseOcurred = !pulseOcurred;
    }

    vTaskDelay(10/portTICK_PERIOD_MS);

  }
}

void readRTC(void *parameters){
  tm timeInfo;
  timeStrings strTime;

  for(;;){
    if(getLocalTime(&timeInfo)){
      strTime.date = rtc.getDate(false);
      strTime.time = rtc.getTime();
      strTime.AmPm = rtc.getAmPm(true);

      Serial.print(strTime.date);
      Serial.printf("  %s  %s \n", strTime.time, strTime.AmPm);

      //xQueueSend(screenRTCQueue_handle, &strTime, 750/portTICK_PERIOD_MS);
      xQueueOverwrite(screenRTCQueue_handle, &strTime);

    }else{
      Serial.println("FAILED TO READTIME");
    }
    vTaskDelay(1000/portTICK_PERIOD_MS);

  }
}

void openWeatherGet(void* parameters){
  openWeatherJSONParsed weatherInfoBuffer;
  String tempJSON;
  JSONVar tempJSONVar;
  String weatherDescriptionBuffer;
  float numBuffer;

  for(;;){
    httpClient.begin(openWeatherUrl);
    int httpCode = httpClient.GET();
    if(httpCode>0){
      Serial.println("---- HOLA! CONNECTED HTTP ----");
      tempJSON = httpClient.getString();
      tempJSONVar = JSON.parse(tempJSON);
      
      weatherInfoBuffer.description = tempJSONVar.stringify(tempJSONVar["weather"][0]["description"]);
      weatherInfoBuffer.tempFeelLike = atof(tempJSONVar.stringify(tempJSONVar["main"]["feels_like"]).c_str());
      weatherInfoBuffer.humidity = atof(tempJSONVar.stringify(tempJSONVar["main"]["humidity"]).c_str());
      weatherInfoBuffer.windSpeed = atof(tempJSONVar.stringify(tempJSONVar["wind"]["speed"]).c_str());

      xQueueSend(screenOpenWeather_handle, &weatherInfoBuffer, portMAX_DELAY);

      Serial.print("Description = ");
      Serial.println(weatherInfoBuffer.description);
      Serial.print("API TEMP = ");
      Serial.println(weatherInfoBuffer.tempFeelLike);
      Serial.print("API RH = ");
      Serial.println(weatherInfoBuffer.humidity);
      Serial.print("Wind Speed = ");
      Serial.println(weatherInfoBuffer.windSpeed);

      httpClient.end();
      vTaskDelay(5000/portTICK_PERIOD_MS);

    }else{
      Serial.println("HELL NAH");
    }
    vTaskDelay(5000/portTICK_PERIOD_MS);
  }
}

void screenDisplay(void *parameters){
  uint8_t currentScreenIndex = 0;
  timeStrings tmInfoBuffer;
  DHT_sensor_data TempRHvaluesBuffer;
  uint16_t pulseReadingBuffer;
  openWeatherJSONParsed weatherInfoBuffer;

  for(;;){
    if(xSemaphoreTake(screenDisplaySemaphore_handle, pdMS_TO_TICKS(150))){
      currentScreenIndex = (currentScreenIndex+1)%4;
    }

    if(currentScreenIndex == 0){
      xQueueReceive(screenRTCQueue_handle, &tmInfoBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB12_te);
      screen.drawStr(30,25, tmInfoBuffer.time.c_str());
      screen.setFont(u8g2_font_helvR08_te);
      screen.drawStr(20,50, tmInfoBuffer.date.c_str());
      screen.sendBuffer();
    }else if(currentScreenIndex == 1){
      xQueueReceive(screenDHTQueue_handle, &TempRHvaluesBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB10_te);
      screen.setCursor(20,25);
      screen.printf("Temp = %.2f ", TempRHvaluesBuffer.temp);
      screen.setCursor(30,50);
      screen.printf("RH = %.2f", TempRHvaluesBuffer.rh);
      screen.sendBuffer();
    }else if(currentScreenIndex == 2){
      xQueueReceive(screenPulseQueue_handle, &pulseReadingBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB12_te);
      screen.drawStr(40, 25, "BPM");
      screen.setCursor(50,50);
      screen.print(pulseReadingBuffer);
      screen.sendBuffer();
    }else{
      xQueueReceive(screenOpenWeather_handle, &weatherInfoBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB08_tr);
      screen.drawStr(24,17, weatherInfoBuffer.description.c_str());
      screen.setCursor(14,34);
      screen.print("Temp: ");
      screen.print(weatherInfoBuffer.tempFeelLike - 273.25);
      screen.setCursor(14,64);
      screen.print("RH(%): ");
      screen.print(weatherInfoBuffer.humidity);
      screen.setCursor(14,47);
      screen.print("wind speed: ");
      screen.print(weatherInfoBuffer.windSpeed);
      screen.sendBuffer();
    }

  }
}

void setup(){
  Serial.begin(115200);

  delay(1000);
  
  dht.begin();

  pinMode(SCREEN_BUTTON, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(SCREEN_BUTTON), (void (*)())screenButtonISR, FALLING);

  screenDisplaySemaphore_handle = xSemaphoreCreateBinary();

  screenRTCQueue_handle = xQueueCreate(1, sizeof(timeStrings));
  screenDHTQueue_handle = xQueueCreate(SCREEN_DHT_QUEUE_SIZE, sizeof(DHT_sensor_data));
  screenPulseQueue_handle = xQueueCreate(SCREEN_PULSE_QUEUE_SIZE, sizeof(uint16_t));
  screenOpenWeather_handle = xQueueCreate(SCREEN_WEATHER_API_QUEUE_SIZE, sizeof(openWeatherJSONParsed));

  WiFi.mode(WIFI_STA); // Set to station mode
  WiFi.begin(SSID, PASSWORD);
  Serial.printf("Connecting to %s", SSID);

  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(250);
  }

  configTime(gmOffset, dayLightSaving, ntpServer1, ntpServer2);


  Wire.begin();
  screen.begin();


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
    "READ PULSE SENSOR TASK",
    3000,
    NULL,
    1,
    &readPulseSensor_handle,
    1
  );

  xTaskCreatePinnedToCore(
    readRTC,
    "READ INTERNAL RTC TASK",
    3000,
    NULL,
    1,
    &readRTC_handle,
    1
  );

  xTaskCreatePinnedToCore(
    openWeatherGet, 
    "OpenWeatherAPI Task",
    4000,
    NULL,
    1,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    screenDisplay,
    "OLED DISPLAY TASK",
    4000,
    NULL,
    1,
    NULL,
    1
  );

  /*xTaskCreatePinnedToCore(
    testInterrupt,
    "INTERRUPT TESTING TASK",
    1024,
    NULL,
    1,
    NULL,
    1
  );*/
  
}

void loop(){

}