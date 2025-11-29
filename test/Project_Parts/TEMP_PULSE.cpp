/*
  ESP32 Accurate BPM with Moving Average
*/

#include <Arduino.h>

const int PulseSensorPin = 13; 
// ============================================
const int THRESHOLD = 2650; // حط رقمك هنا
// ============================================

int signalValue = 0;
unsigned long lastBeatTime = 0; 

// متغيرات الـ Averaging (عشان الدقة)
#define RATE_SIZE 10        // هنحسب متوسط آخر 10 دقات
int rates[RATE_SIZE];       // مصفوفة لتخزين القراءات
int rateSpot = 0;           // مؤشر للكتابة في المصفوفة
//long lastBeat = 0;          // وقت آخر دقة

float beatsPerMinute = 0;
int beatAvg = 0;            // ده الرقم الدقيق النهائي

bool beatDetected = false;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12); 
  
  // تصفير المصفوفة في البداية
  for(int i=0; i<RATE_SIZE; i++) rates[i] = 0;
  
  Serial.println("System Ready.");
}

void loop() {
  signalValue = analogRead(PulseSensorPin);

  // --- لحظة اكتشاف الدقة ---
  if (signalValue > THRESHOLD && beatDetected == false) {
    
    unsigned long currentTime = millis(); // الوقت الحالي بالمللي ثانية
    unsigned long timeDifference = currentTime - lastBeatTime; // الفرق بين الدقتين
    
    // فلتر الزمن (تجاهل الشوشرة السريعة)
    if (timeDifference > 600) { 
      
      lastBeatTime = currentTime; 
      beatDetected = true;        
      
      // 1. حساب الـ BPM اللحظي
      beatsPerMinute = 60000.0 / timeDifference; 

      // 2. فلتر منطقي (عشان لو طلع رقم خرافي زي 200 نتجاهله)
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        
        // تخزين القراءة في المصفوفة
        rates[rateSpot++] = (int)beatsPerMinute; 
        rateSpot %= RATE_SIZE; // عشان اللفة ترجع للأول لما نوصل لـ 10
        
        // حساب المتوسط
        beatAvg = 0;
        for (int x = 0 ; x < RATE_SIZE ; x++) {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE; // القسمة على عددهم
      }

      // طباعة الرقم الدقيق (المتوسط)
      Serial.print("♥ Stable BPM: ");
      Serial.print(beatAvg);
      
      // طباعة اللحظي للمقارنة (اختياري)
      Serial.print("\t (Instant: ");
      Serial.print((int)beatsPerMinute);
      Serial.println(")");
      
    }
  }

  // إعادة التفعيل لما الإشارة تنزل
  if (signalValue < THRESHOLD - 100) {
    beatDetected = false;
  }

  delay(20); 
}