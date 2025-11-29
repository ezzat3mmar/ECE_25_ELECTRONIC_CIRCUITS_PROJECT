/*
 * U8g2 "Hello World" Example for SSD1306 OLED Display (I2C)
 *
 * This code uses the U8g2 library to initialize and display text on a 
 * 128x64 pixel OLED display module. It assumes a standard I2C connection.
 * * IMPORTANT: You must install the "U8g2" library in your Arduino IDE 
 * (Sketch -> Include Library -> Manage Libraries... search for "u8g2").
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h> // Required for I2C communication

// --- U8G2 OBJECT INITIALIZATION ---
// You must select the constructor that matches your display module.
// This example uses a common 128x64 SSD1306 display via hardware I2C.
//
// Constructor format: U8G2_<display_type>_<resolution>_<buffer_mode>_<interface>
//
// F: Full buffer mode (uses ~1KB RAM, best performance)
// U8X8: Minimal RAM usage (less than 100 bytes, slower)
//
// The 'NONAME' part is often used for common breakout boards.
// The '0x3C' address is the default for many 128x64 I2C OLEDs.
// Change 0x3C to 0x3D if your board uses the alternative I2C address.

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA);   // Hardware I2C

// --- SETUP ---
void setup() {
  // 1. Initialize U8g2 library
  u8g2.begin();

  // 2. Clear the buffer
  u8g2.clearBuffer();          

  // 3. Set the font (u8g2 comes with many fonts)
  // u8g2_font_helvB14_tf is a nice, readable font
  u8g2.setFont(u8g2_font_helvB14_tf); 

  // 4. Set the cursor position (x, y coordinates)
  // x=0 is the left edge. The y-coordinate is the base line of the text.
  // 32 is roughly the vertical center of the 64-pixel display.
  u8g2.setCursor(0, 32); 

  // 5. Print the text to the internal buffer
  u8g2.print("Hello");
  
  // Set cursor for the second line
  u8g2.setCursor(0, 55); // Move down a bit
  u8g2.print("World!");
  
  // 6. Transfer the content of the buffer to the display
  u8g2.sendBuffer();          
}

// --- LOOP ---
void loop() {
  // The display content is static, so the loop can be empty.
  // In a real application, you would put sensor reading or animation updates here.
}