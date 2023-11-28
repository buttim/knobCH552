#ifndef USER_USB_RAM
#warning "This example needs to be compiled with a USER USB setting"
#endif

#include <TouchKey.h>
#include <SoftI2C.h>
#include <WS2812.h>

const int BUTTON=31, LED=30, SCL=10, SDA=11, TOUCH1=15, TOUCH2=16, TOUCH3=17, DIRECTION=34, WS2812_DATA=33, 
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
  #define Consumer_press(x) ;
  #define Consumer_release(x) ;
#endif

#define REGBR(name,addr) uint8_t read##name() { return readReg(addr,false); }
#define REGBW(name,addr) void write##name(uint8_t data) { writeReg(addr,data,false); }
#define REGWR(name,addr) uint16_t read##name() { return readReg(addr,true); }
#define REGWW(name,addr) void write##name(uint16_t data) { writeReg(addr,data,true); }
#define REGW(name,addr) REGWR(name,addr) REGWW(name,addr)

uint16_t startAngle=0, last=0, tLastKeyPress=0;
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
  TouchKey_SetTouchThreshold(1500);
  TouchKey_SetReleaseThreshold(1450);

  delay(5);

  pinMode(WS2812_DATA, OUTPUT);

  scl_pin = SCL;
  sda_pin = SDA;
  I2CInit();

  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  digitalWrite(DIRECTION,0);            //clockwise increment

  for (int i = 0; i < 3; i++) {
    digitalWrite(LED, HIGH);
    delay(100);
    digitalWrite(LED, LOW);
    delay(100);
  }

  startAngle=readAngle()-FACTOR/2;
  writeConf(3<<2); //hysteresis (?)

  /*for (uint8_t i = 0; i < NUM_LEDS; i++) {
    set_pixel_for_RGB_LED(ledData, NUM_LEDS-i-1, 0, 0, 4);
    neopixel_show_P3_3(ledData, NUM_BYTES);
    delay(50);
  }*/
}

void pressKey(int key) {
    Consumer_press(key);
    tLastKeyPress=millis();
}

void loop() {
  //TODO: aggiungere effetti LED
  uint16_t t=millis();

  if (tLastKeyPress!=0 && t-tLastKeyPress>50) {
    tLastKeyPress=0;
    Consumer_releaseAll();
  }
  TouchKey_Process();
  uint8_t touchResult=TouchKey_Get();
  digitalWrite(LED, touchResult ? HIGH : LOW);
  if (touchResult) {
    uint8_t r=0, g=0, b=0;
    if (touchResult&(1<<3)) {
      pressKey(MEDIA_NEXT);
      r=16;
    }
    if (touchResult&(1<<4)) {
      pressKey(MEDIA_PLAY_PAUSE);
      g=16;
    }
    if (touchResult&(1<<5)) {
      pressKey(MEDIA_PREV);
      b=16;
    }
    for (uint8_t i = 0; i < NUM_LEDS; i++)
      set_pixel_for_BGR_LED(ledData, i, r, g, b);
    neopixel_show_P3_3(ledData, NUM_BYTES);
  }
    
  uint16_t sum=0;

  for (int i=0;i<10;i++)
    sum+=readAngle();
  uint16_t angle=(sum/10-startAngle+2048)%4096;
  if (angle==0xFFFFU)
    while (true);

  uint16_t curr=angle/FACTOR;
  if (curr>last)
    pressKey(MEDIA_VOL_UP);
  else if (curr<last)
    pressKey(MEDIA_VOL_DOWN);
  last=curr;
}
