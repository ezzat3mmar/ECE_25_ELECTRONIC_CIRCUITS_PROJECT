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
/*  MPU6050 lib  */
#include <MPU6050.h>
/*  Parsing and getting JSON APIs  */
#include <HTTPClient.h>
#include <Arduino_JSON.h>
/*  Handling credentials   */
#include <credentials.h>


/*  Buttons Pins & debounce Time  */
#define SCREEN_CHANGE_BUTTON 18
#define TIME_EDIT_ENABLE_BUTTON 32
#define TIME_INCREMENT_BUTTON 25
#define TIME_DECREMENT_BUTTON 26
#define DEBOUNCE_TIME 250

/* Buttons last Time Pressed */
unsigned long lastScreenChangeTime = 0;
unsigned long lastTimeEditEnablePressed = 0;
unsigned long lastTimeIncremennt = 0;
unsigned long lastTimeDecrement = 0;
unsigned long lastStepCountReset = 0;

/* MPU6050 super-params  */
#define THRESHOLD 1.0 
#define BUFFER_LENGTH 15 
#define DEBOUNCE_DELAY 300 

#define DHTPIN 13
#define DHTTYPE DHT11

#define PULSE_PIN 33

DHT_Unified dht(DHTPIN, DHTTYPE);

ESP32Time rtc(0);

MPU6050 mpu;

#define gmOffset 7200     // (GMT+2) in seconds
#define dayLightSaving 0 
#define ntpServer1 "pool.ntp.org"
#define ntpServer2 "time.nist.gov" 

String city = "Tanta"; 
String countryCode = "EG";
String openWeatherUrl = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + APIKey;

typedef struct{
  String date;
  String time;
  String AmPm;
}timeStrings;

typedef struct{
  uint8_t minOffset;
  uint8_t hrOffset;
  uint8_t dayOffset;
  uint8_t monthOffset;
  uint8_t yearOffset;
}timeOffsets;

typedef struct{
  int hour;
  int min;
  int sec;
  int year; 
  int month;
  int day;
}timeInt;

typedef struct {
  int stepCount;
  float avgMagnitude;
  bool stepDetected;
} StepData;

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

typedef struct{
  volatile uint8_t screenCurrentIndex;         //  the current screen number showed on the OLED (0-3)
  volatile uint8_t currentBlinkingTimeField;   //  the current binking part of Time and date (0-5)
  volatile uint8_t timeChange;                 //  the new change added to the offsets
}ScreenStatus;

openWeatherJSONParsed weatherInfo;

WiFiClient client;
HTTPClient httpClient;

U8G2_SH1106_128X64_NONAME_F_HW_I2C screen(U8G2_R0, U8X8_PIN_NONE, SCL, SDA);

ScreenStatus screenStatusCfx = {
  .screenCurrentIndex = 0,
  .currentBlinkingTimeField = 0,
  .timeChange = 0
};


timeOffsets timeOffsetsCfx = {
  .minOffset = 0,
  .hrOffset = 0,
  .dayOffset = 0, 
  .monthOffset = 0,
  .yearOffset = 0,
};


TaskHandle_t readDHT_handle;
TaskHandle_t readPulseSensor_handle;
TaskHandle_t readRTC_handle;
TaskHandle_t openWeatherTask_handle;
TaskHandle_t screenDisplay_handle;
TaskHandle_t readMPU_handle;        
TaskHandle_t stepDetection_handle;  
TaskHandle_t displayUpdate_handle;

QueueHandle_t screenRTCQueue_handle;

QueueHandle_t screenDHTQueue_handle;
#define SCREEN_DHT_QUEUE_SIZE 5

QueueHandle_t screenPulseQueue_handle;
#define SCREEN_PULSE_QUEUE_SIZE 10

QueueHandle_t screenOpenWeather_handle;
#define SCREEN_WEATHER_API_QUEUE_SIZE 1

QueueHandle_t mpuDataQueue_handle;  
QueueHandle_t stepDataQueue_handle; 
QueueHandle_t displayQueue_handle; 

SemaphoreHandle_t screenDisplaySemaphore_handle;
SemaphoreHandle_t timeIncerementSemaphore_handle;
SemaphoreHandle_t timeDecrementSemaphore_handle;
SemaphoreHandle_t resetSemaphore_handle;

volatile int globalStepCount = 0;

void IRAM_ATTR screenChangeButtonISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastScreenChangeTime > DEBOUNCE_TIME){
    screenStatusCfx.screenCurrentIndex = (screenStatusCfx.screenCurrentIndex + 1)%5;
    lastScreenChangeTime = currentTime;
  }
}

void IRAM_ATTR screenTimeDateEditEnableButtonISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastTimeEditEnablePressed > DEBOUNCE_TIME){
    lastTimeEditEnablePressed = currentTime;
    screenStatusCfx.currentBlinkingTimeField = (screenStatusCfx.currentBlinkingTimeField+1)%6;
  }
}


void IRAM_ATTR screenTimeIncrementButtonISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastTimeIncremennt > DEBOUNCE_TIME){
    
    lastTimeIncremennt = currentTime;
    BaseType_t higherPriorityTaskAwaken = pdFALSE;
    xSemaphoreGiveFromISR(timeIncerementSemaphore_handle, &higherPriorityTaskAwaken);
    portYIELD_FROM_ISR(higherPriorityTaskAwaken);
  }
  
}


void IRAM_ATTR screenTimeDecremntButtonISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastTimeDecrement > DEBOUNCE_TIME){
    
    lastTimeDecrement = currentTime;
    BaseType_t higherPriorityTaskAwaken = pdFALSE;
    xSemaphoreGiveFromISR(timeDecrementSemaphore_handle, &higherPriorityTaskAwaken);
    portYIELD_FROM_ISR(higherPriorityTaskAwaken);
  }
  
}

void IRAM_ATTR resetStepCountsISR(){
  unsigned long currentTime = millis();
  if(currentTime - lastStepCountReset > DEBOUNCE_TIME){
    lastStepCountReset = currentTime;
    BaseType_t higherPriorityTaskAwaken = pdFALSE;
    xSemaphoreGiveFromISR(resetSemaphore_handle, &higherPriorityTaskAwaken);
    portYIELD_FROM_ISR(higherPriorityTaskAwaken);
  }
}


void readMPU(void* parameters) {
  float accelerationData[3];
  
  for(;;) {
   // read data from MPU 
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    
    accelerationData[0] = ax / 2048.0;
    accelerationData[1] = ay / 2048.0;
    accelerationData[2] = az / 2048.0;
    
    // send data to Queue 
    xQueueSend(mpuDataQueue_handle, &accelerationData, portMAX_DELAY);
    
    Serial.print("Free MPU Stack: ");
    Serial.println(uxTaskGetStackHighWaterMark(readMPU_handle));
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void stepDetection(void* parameters) {
  float buffer[BUFFER_LENGTH] = {0};
  int bufferIndex = 0;
  float accelerationData[3];
  unsigned long lastStepTime = 0;
  StepData stepData = {0, 0, false};
  
  for(;;) {
    // recive accelerationData
    if(xQueueReceive(mpuDataQueue_handle, &accelerationData, pdMS_TO_TICKS(50))) {
    // calculate the magnitude       
      float accelerationMagnitude = sqrt(
        accelerationData[0] * accelerationData[0] +
        accelerationData[1] * accelerationData[1] +
        accelerationData[2] * accelerationData[2]
      );
      
      buffer[bufferIndex] = accelerationMagnitude;
      bufferIndex = (bufferIndex + 1) % BUFFER_LENGTH;
      // calc average 
      float avgMagnitude = 0;
      for (int i = 0; i < BUFFER_LENGTH; i++) {
        avgMagnitude += buffer[i];
      }
      avgMagnitude /= BUFFER_LENGTH;
      
      stepData.avgMagnitude = avgMagnitude;
      
      unsigned long currentMillis = millis();
      
      if (accelerationMagnitude > (avgMagnitude + THRESHOLD)) {
        if (!stepData.stepDetected && (currentMillis - lastStepTime) > DEBOUNCE_DELAY) {
          globalStepCount++;
          stepData.stepCount = globalStepCount;
          stepData.stepDetected = true;
          lastStepTime = currentMillis;
          
          Serial.print("👟 Step detected! Total: ");
          Serial.println(globalStepCount);
          
 
          xQueueOverwrite(stepDataQueue_handle, &stepData);
        }
      } else {
        stepData.stepDetected = false;
      }
      
      if(xSemaphoreTake(resetSemaphore_handle, 0)) {
        globalStepCount = 0;
        stepData.stepCount = 0;
        Serial.println("🔄 Step counter reset!");
        xQueueOverwrite(stepDataQueue_handle, &stepData);
      }
    }
    
    Serial.print("Free StepDetection Stack: ");
    Serial.println(uxTaskGetStackHighWaterMark(stepDetection_handle));
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

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
  uint16_t threshold = 2450;
  uint32_t timeDelay = 450;
  static uint32_t lastPrintTime = 0;
  bool pulseOcurred = false;

  uint8_t READING_NUM = 10;
  uint8_t CURRENT_INDEX = 0;
  uint16_t lastPulses[READING_NUM] = {0};

  uint16_t signal;
  uint16_t BPM = 0;
  uint16_t tempBPM;

  for(;;){
        // condetion to make sensor read at his screen
    if(screenStatusCfx.screenCurrentIndex != 2){
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
  
    signal = analogRead(PULSE_PIN);  // read sensor signal
    Serial.print("[Pulse] Raw Signal = ");
    Serial.println(signal);
    Serial.print("[Pulse] BPM = ");
    Serial.println(BPM);
       // soft delay to read from serial_monitor
    if(millis() - lastPrintTime > 500){   
      Serial.print("[Pulse] Raw = ");
      Serial.print(signal);
      Serial.print(" | BPM = ");
      Serial.println(BPM);
      lastPrintTime = millis();
}
        
    if(signal > threshold && !pulseOcurred){
      uint32_t currentTime = millis();

      if(currentTime - lastPeakTime > timeDelay){
        tempBPM = 60000 / (currentTime - lastPeakTime);

        if(tempBPM > 40 && tempBPM < 120){
          lastPulses[CURRENT_INDEX] = tempBPM;
          CURRENT_INDEX = (CURRENT_INDEX + 1) % READING_NUM;
        }

        lastPeakTime = currentTime;
        pulseOcurred = true;
      }

      uint16_t sumBPM = 0;
      uint8_t validReading = 0;

      for(int i = 0; i < READING_NUM; i++){
        if(lastPulses[i] > 0){
          sumBPM += lastPulses[i];
          validReading++;
        }
      }

      if(validReading > 0){
        BPM = sumBPM / validReading;
      }
      if(screenStatusCfx.screenCurrentIndex != 2){
        Serial.println("[Pulse] Waiting for BPM screen...");
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }


      xQueueSend(screenPulseQueue_handle, &BPM, portMAX_DELAY);
    }

    if(signal < (threshold - 100) && pulseOcurred){
      pulseOcurred = false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void readRTC(void *parameters){
  tm timeInfo;
  timeStrings strTime;

  for(;;){
    if(getLocalTime(&timeInfo)){

      if(xSemaphoreTake(timeIncerementSemaphore_handle, pdMS_TO_TICKS(0))){
        switch (screenStatusCfx.currentBlinkingTimeField)
        {
        case 0:
          break;
        case 1:
          timeInfo.tm_min += 1;
          break;
        case 2:
          timeInfo.tm_hour += 1;
          break;
        case 3:
          timeInfo.tm_mday += 1;
          timeInfo.tm_wday += 1;
          timeInfo.tm_yday += 1;
          break;
        case 4:
          timeInfo.tm_mon += 1;
          break;
        case 5:
          timeInfo.tm_year += 1;
          break;
        default:
          break;
        }
        rtc.setTimeStruct(timeInfo);
      }

      if(xSemaphoreTake(timeDecrementSemaphore_handle, pdMS_TO_TICKS(0))){
        switch (screenStatusCfx.currentBlinkingTimeField)
        {
        case 0:
          break;
        case 1:
          timeInfo.tm_min -= 1;
          break;
        case 2:
          timeInfo.tm_hour -= 1;
          break;
        case 3:
          timeInfo.tm_mday -= 1;
          timeInfo.tm_wday -= 1;
          timeInfo.tm_yday -= 1;
          break;
        case 4:
          timeInfo.tm_mon -= 1;
          break;
        case 5:
          timeInfo.tm_year -= 1;
          break;
        default:
          break;
        }
        rtc.setTimeStruct(timeInfo);
      }

      strTime.date = rtc.getDate(false);
      strTime.time = rtc.getTime();
      strTime.AmPm = rtc.getAmPm(true);

      xQueueOverwrite(screenRTCQueue_handle, &strTime);

    }else{
      Serial.println("FAILED TO READTIME");
    }

    Serial.print("Free RTC Stack: ");
    Serial.println(uxTaskGetStackHighWaterMark(readRTC_handle));

    vTaskDelay(1000/portTICK_PERIOD_MS);

  }
}

void createTimeDateFrames(String arr[], String time, String date, int size = 7, bool mode=true){
  if(mode){
    arr[0] = time.substring(0,5);
  }else{
    arr[0] = time;
  }
  arr[1] = time.substring(0,3) + "--";
  arr[2] = "--" + time.substring(2,5);
  arr[3] = date; 
  arr[4] = date.substring(0,9) + "--" + date.substring(11,date.length());
  arr[5] = date.substring(0,5) + "---" + date.substring(8,date.length());
  arr[6] = date.substring(0,12) + "----";
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
      Serial.println(weatherInfoBuffer.description.substring(1,weatherInfoBuffer.description.length()-1));
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

    Serial.print("Free openWeatherAPI Stack: ");
    Serial.println(uxTaskGetStackHighWaterMark(openWeatherTask_handle));

    vTaskDelay(5000/portTICK_PERIOD_MS);
  }
}

void screenDisplay(void *parameters){
  uint8_t currentScreenIndex = 0;
  timeStrings tmInfoBuffer;
  DHT_sensor_data TempRHvaluesBuffer;
  uint16_t pulseReadingBuffer;
  openWeatherJSONParsed weatherInfoBuffer;
  String timeDateFrames[7];

  bool currentBlinkingState = false;
  StepData stepData = {0, 0, false};

  for(;;){

    vTaskDelay(pdMS_TO_TICKS(400));

    if(screenStatusCfx.screenCurrentIndex == 0){
      xQueueReceive(screenRTCQueue_handle, &tmInfoBuffer, pdMS_TO_TICKS(10));
      createTimeDateFrames(timeDateFrames, tmInfoBuffer.time, tmInfoBuffer.date);
      currentBlinkingState = !currentBlinkingState;

      if((screenStatusCfx.currentBlinkingTimeField==1 || screenStatusCfx.currentBlinkingTimeField==2) && currentBlinkingState==true){

        screen.clearBuffer();
        screen.setFont(u8g2_font_helvB12_te);
        screen.drawStr(40,25, timeDateFrames[screenStatusCfx.currentBlinkingTimeField].c_str());
        //Serial.printf("THIS SHOULD BE PRINTED -> %s \n", tmInfoBuffer.time.substring(x,y));
        screen.setFont(u8g2_font_helvR08_te);
        screen.drawStr(20,50, timeDateFrames[3].c_str());
        screen.sendBuffer();
      }else if (screenStatusCfx.currentBlinkingTimeField>=3 && currentBlinkingState==true){
        screen.clearBuffer();
        screen.setFont(u8g2_font_helvB12_te);
        screen.drawStr(40,25, timeDateFrames[0].c_str());
        screen.setFont(u8g2_font_helvR08_te);
        screen.drawStr(20,50, timeDateFrames[screenStatusCfx.currentBlinkingTimeField+1].c_str());
        screen.sendBuffer();
      }else{
        screen.clearBuffer();
        screen.setFont(u8g2_font_helvB12_te);
        screen.drawStr(40,25, timeDateFrames[0].c_str());
        screen.setFont(u8g2_font_helvR08_te);
        screen.drawStr(20,50, timeDateFrames[3].c_str());
        screen.sendBuffer();
      }
  
    }else if(screenStatusCfx.screenCurrentIndex == 1){
      xQueueReceive(screenDHTQueue_handle, &TempRHvaluesBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB10_te);
      screen.setCursor(20,25);
      screen.printf("Temp = %.2f ", TempRHvaluesBuffer.temp);
      screen.setCursor(30,50);
      screen.printf("RH = %.2f", TempRHvaluesBuffer.rh);
      screen.sendBuffer();
    }else if(screenStatusCfx.screenCurrentIndex == 2){
      xQueueReceive(screenPulseQueue_handle, &pulseReadingBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB12_te);
      screen.drawStr(40, 25, "BPM");
      screen.setCursor(50,50);
      screen.print(pulseReadingBuffer);
      screen.sendBuffer();
    }else if(screenStatusCfx.screenCurrentIndex == 3){
      xQueueReceive(screenOpenWeather_handle, &weatherInfoBuffer, pdMS_TO_TICKS(10));
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB08_tr);
      screen.drawStr(24,17, weatherInfoBuffer.description.substring(1,weatherInfoBuffer.description.length()-1).c_str());
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
    }else{
      
      xQueueReceive(stepDataQueue_handle, &stepData, pdMS_TO_TICKS(100));
      
      screen.clearBuffer();
      screen.drawFrame(0, 0, 128, 64);
      
      screen.setFont(u8g2_font_ncenB08_tr);
      screen.drawStr(25, 12, "Step Counter");
      
      screen.drawLine(5, 15, 123, 15);
      
      screen.setFont(u8g2_font_logisoso20_tn);
      String steps = String(stepData.stepCount);
      int textWidth = steps.length() * 12;
      screen.setCursor((128 - textWidth) / 2, 40);
      screen.print(stepData.stepCount);

      screen.setFont(u8g2_font_6x10_tr);
      screen.drawStr(45, 52, "Steps");
    
      screen.setFont(u8g2_font_5x7_tr);
      String ip = WiFi.localIP().toString();
      screen.setCursor(2, 62);
      screen.print("IP: " + ip);
      screen.sendBuffer();
    }

    Serial.print("Free sceeenDisplay Stack: ");
    Serial.println(uxTaskGetStackHighWaterMark(screenDisplay_handle));

  }

}

void setup(){
  Serial.begin(115200);

  delay(1000);
  
  dht.begin();

  pinMode(SCREEN_CHANGE_BUTTON, INPUT_PULLUP);
  pinMode(TIME_EDIT_ENABLE_BUTTON, INPUT_PULLUP);
  pinMode(TIME_INCREMENT_BUTTON, INPUT_PULLUP);
  pinMode(TIME_DECREMENT_BUTTON, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(SCREEN_CHANGE_BUTTON),  screenChangeButtonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(TIME_EDIT_ENABLE_BUTTON), screenTimeDateEditEnableButtonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(TIME_INCREMENT_BUTTON), screenTimeIncrementButtonISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(TIME_DECREMENT_BUTTON), screenTimeDecremntButtonISR, FALLING); 
  
  screenDisplaySemaphore_handle = xSemaphoreCreateBinary();
  timeIncerementSemaphore_handle = xSemaphoreCreateBinary();
  timeDecrementSemaphore_handle = xSemaphoreCreateBinary();
  resetSemaphore_handle = xSemaphoreCreateBinary();

  screenRTCQueue_handle = xQueueCreate(1, sizeof(timeStrings));
  screenDHTQueue_handle = xQueueCreate(SCREEN_DHT_QUEUE_SIZE, sizeof(DHT_sensor_data));
  screenPulseQueue_handle = xQueueCreate(SCREEN_PULSE_QUEUE_SIZE, sizeof(uint16_t));
  screenOpenWeather_handle = xQueueCreate(SCREEN_WEATHER_API_QUEUE_SIZE, sizeof(openWeatherJSONParsed));
  mpuDataQueue_handle = xQueueCreate(10, sizeof(float) * 3);
  stepDataQueue_handle = xQueueCreate(1, sizeof(StepData));
  displayQueue_handle = xQueueCreate(1, sizeof(StepData));

  Wire.begin();
  screen.begin();

  Serial.println("Initializing MPU-6050...");
  mpu.initialize();
  
  if (!mpu.testConnection()) {
    Serial.println("MPU-6050 connection failed!");
    screen.clearBuffer();
    screen.drawStr(5, 20, "MPU-6050 Error!");
    screen.setFont(u8g2_font_5x7_tr);
    screen.drawStr(5, 35, "Check wiring:");
    screen.drawStr(5, 50, "VCC->3.3V GND->GND");
    screen.drawStr(5, 58, "SCL->22 SDA->21");
    screen.sendBuffer();
    while (1) delay(1000);
  }
  
  Serial.println(" MPU-6050 connected!");

  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);

  WiFi.mode(WIFI_STA); // Set to station mode
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to %s", WIFI_SSID);

  String StartScreenFrames[] = {"Connecting", "Connecting.", "Connecting..","Connecting..."};
  
  while(WiFi.status() != WL_CONNECTED){
    for(int i=0; i<4; i++){
      screen.clearBuffer();
      screen.setFont(u8g2_font_helvB12_te);
      screen.drawStr(20,40, StartScreenFrames[i].c_str());
      screen.sendBuffer();
    }
    Serial.print(".");
    delay(100);
  }

  configTime(gmOffset, dayLightSaving, ntpServer1, ntpServer2);

  xTaskCreatePinnedToCore(
    readDHT,
    "DHT SENSOR READING TASK",
    2000,
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
    &openWeatherTask_handle,
    1
  );


  xTaskCreatePinnedToCore(
    readMPU,
    "MPU6050 Reading Task",
    3000,
    NULL,
    2,
    &readMPU_handle,
    1
  );
  
  xTaskCreatePinnedToCore(
    stepDetection,
    "Step Detection Task",
    4000,
    NULL,
    2,
    &stepDetection_handle,
    1
  );

  xTaskCreatePinnedToCore(
    screenDisplay,
    "OLED DISPLAY TASK",
    5000,
    NULL,
    1,
    &screenDisplay_handle,
    1
  );

}

void loop(){

}