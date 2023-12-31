#ifndef USER_USB_RAM
#warning "This example needs to be compiled with a USER USB setting"
#endif

#include <TouchKey.h>
#include <SoftI2C.h>
#include <WS2812.h>

const int BUTTON=31, LED=30, SCL=35, SDA=34, TOUCH1=15, TOUCH2=16, TOUCH3=17, WS2812_DATA=33,
  AMS5600_ADDR=0x36, FACTOR=32;

#define NUM_LEDS 16
#define COLOR_PER_LEDS 3
#define NUM_BYTES NUM_LEDS*COLOR_PER_LEDS

#if NUM_BYTES > 255
#error "NUM_BYTES can not be larger than 255."
#endif

#ifdef USER_USB_RAM
  #include "USBHIDMediaKeyboard.h"

  #undef USBSerial_print
  #undef USBSerial_println
  #define USBSerial_print(x) ;
  #define USBSerial_println(x) ;
#else                         // for debugging
  #define USBInit() ;
  #define Consumer_press(x) USBSerial_println(x)
  #define Consumer_release(x) ;
  #define Consumer_releaseAll() ;
  #define MEDIA_NEXT 1
  #define MEDIA_PREV -1
  #define MEDIA_PLAY_PAUSE 0
  #define MEDIA_VOL_DOWN -10
  #define MEDIA_VOL_UP 10
#endif

//macros for AS5600 register read/write
#define REGBR(name,addr) uint8_t read##name() { return readReg(addr,false); }
#define REGBW(name,addr) void write##name(uint8_t data) { writeReg(addr,data,false); }
#define REGWR(name,addr) uint16_t read##name() { return readReg(addr,true); }
#define REGWW(name,addr) void write##name(uint16_t data) { writeReg(addr,data,true); }
#define REGW(name,addr) REGWR(name,addr) REGWW(name,addr)

uint8_t r, g, b, touchResult,i;
uint16_t startAngle=0, last=0, lastKey=0,
   timeout, sum, angle, curr;
uint64_t tLastKeyPress=0, tLastLedEffect=0;
__xdata uint8_t ledData[NUM_BYTES];

uint16_t readReg(uint8_t reg, bool readWord) {
  uint8_t ack=0, data1=0, data2;

  I2CStart();
  ack = I2CSend(AMS5600_ADDR << 1 | 0); //last bit is r(1)/w(0).
  if (ack) {
    USBSerial_println("Err 1");
    return 0xFFFFU;
  }
  ack = I2CSend(reg);
  if (ack) {
    USBSerial_println("Err 2");
    return 0xFFFFU;
  }
  I2CRestart();
  ack = I2CSend(AMS5600_ADDR << 1 | 1);
  if (ack) {
    USBSerial_println("Err 3");
    return 0xFFFFU;
  }
  if (readWord) {
    data1=I2CRead();
    I2CAck();
  }
  data2=I2CRead();
  I2CNak();
  I2CStop();

  return (data1<<8)+data2;
}

void writeReg(uint8_t reg, uint16_t data, bool writeWord) {
  uint8_t ack=0;

  I2CStart();
  ack = I2CSend(AMS5600_ADDR << 1 | 0); //last bit is r(1)/w(0).
  if (ack) {
    USBSerial_println("Err 1");
    return;
  }
  ack = I2CSend(reg);
  if (ack) {
    USBSerial_println("Err 2");
    return;
  }
  if (writeWord) {
    I2CSend(data>>8);
    //I2CAck();
  }
  I2CSend((uint8_t)data);
  //I2CNak();
  I2CStop();
}

uint8_t readRegByte(uint8_t reg) {
  return readReg(reg, false);
}

uint16_t readRegWord(uint16_t reg) {
  return readReg(reg, true);
}

void writeRegByte(uint8_t reg, uint8_t data) {
  writeReg(reg,data,false);
}

void writeRegWord(uint8_t reg, uint16_t data) {
  writeReg(reg,data,true);
}

REGBR(Zmco,0x00)
REGW(ZPos,0x01)
REGW(MPos,0x03)
REGW(Mang,0x05)
REGW(Conf,0x07)
REGWR(RawAngle,0x0C)
REGWR(Angle,0x0E)
REGBR(Status,0x0B)
REGBR(AGC,0x1A)
REGWR(Magnitude,0x1B)
REGBW(Burn,0xFF)

void setup() {
  USBInit();

  TouchKey_begin(1<<3 | 1<<4 | 1<<5);  //enable touch on P1.5, P1.6, P1.7
  TouchKey_SetTouchThreshold(2000);
  TouchKey_SetReleaseThreshold(1900);

  pinMode(WS2812_DATA, OUTPUT);

  scl_pin = SCL;
  sda_pin = SDA;
  I2CInit();

  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
    delay(100);
  }

  startAngle=readAngle()-FACTOR/2;
  writeConf(3<<2); //hysteresis (?)

  for (int j=0;j<3;j++) {
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      set_pixel_for_GRB_LED(ledData, i, j==0?16:0, j==1?16:0, j==2?16:0);
    neopixel_show_P3_3(ledData, NUM_BYTES);
    delay(200);
  }
}

void pressKey(int key) {
  lastKey=key;
  Consumer_press(key);
  tLastKeyPress=millis();
}


void loop() {
  uint64_t t=millis();

  if (tLastLedEffect!=0 && t -tLastLedEffect>2000) {
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      set_pixel_for_GRB_LED(ledData, i, 0, 0, 16);
    neopixel_show_P3_3(ledData, NUM_BYTES);
  }

  if (tLastKeyPress==0 || t-tLastKeyPress>250) {
    TouchKey_Process();
    touchResult=TouchKey_Get();
    digitalWrite(LED, touchResult ? HIGH : LOW);
    if (touchResult) {
      r=g=b=0;
      if (touchResult&(1<<3)) {
        pressKey(MEDIA_NEXT);
        r=b=16;
      }
      if (touchResult&(1<<4)) {
        pressKey(MEDIA_PLAY_PAUSE);
        r=g=b=16;
      }
      if (touchResult&(1<<5)) {
        pressKey(MEDIA_PREV);
        r=b=16;
      }
      for (i = 0; i < NUM_LEDS; i++)
        set_pixel_for_GRB_LED(ledData, i, r, g, b);
      neopixel_show_P3_3(ledData, NUM_BYTES);
      tLastLedEffect=t;
      do  //wait for release
        TouchKey_Process();
      while ((touchResult & TouchKey_Get())!=0);
    }
  }

  if (tLastKeyPress!=0) {
    timeout=250;
    if (lastKey==MEDIA_VOL_UP || lastKey==MEDIA_VOL_DOWN) timeout=50;
    if (t-tLastKeyPress>timeout) {
      tLastKeyPress=0;
      Consumer_releaseAll();
    }
  }

  sum=0;
  for (i=0;i<10;i++)
    sum+=readAngle();
  angle=(sum/10-startAngle+2048)%4096;
  if (angle==0xFFFFU) {
    while (digitalRead(SDA)==LOW || digitalRead(SCL)==LOW)
      ;
    I2CInit();
    return;
  }

  curr=angle/FACTOR;
  if (curr>last) {
    pressKey(MEDIA_VOL_UP);
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      set_pixel_for_GRB_LED(ledData, i, 32, 0, 0);
    neopixel_show_P3_3(ledData, NUM_BYTES);
    tLastLedEffect=t;
  }
  else if (curr<last) {
    pressKey(MEDIA_VOL_DOWN);
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      set_pixel_for_GRB_LED(ledData, i, 0, 16, 16);
    neopixel_show_P3_3(ledData, NUM_BYTES);
    tLastLedEffect=t;
  }
  last=curr;
}
