/*
 * TDA8425 interface
 * REF: http://labkit.ru/userfiles/file/documentation/Audioprocessor/tda8425.pdf
 *
 * @author Andrey Karpov <andy.karpov@gmail.com>
 * @copyright 2013 Andrey Karpov
 */

#include <LiquidCrystal.h>
#include <Encoder.h>
#include <Button.h>
#include <EEPROM.h>
#include <I2C.h>

#define ROWS 2 // number of display rows
#define COLS 16 // number of display columns

#define NUM_MODES 4 // number of app modes

#define DEBOUNCE 400

#define TDA8425_ADDR          0x82
#define TDA_VOL_LEFT          0x00
#define TDA_VOL_RIGHT         0x01
#define TDA_BASS              0x02
#define TDA_TREBLE            0x03
#define TDA8425_S1            0x08  /* switch functions */   
                                    /* values for those registers: */   
#define TDA8425_S1_OFF        0xEE  /* audio off (mute on) */   
#define TDA8425_S1_ON         0xCE  /* audio on (mute off) - "linear stereo" mode */

LiquidCrystal lcd(8, 9, 13, 12, 11, 10); // lcd connected to D8, D9, D13, D12, D11, D10 pins
Encoder enc(2, 3); // encoder pins A and B connected to D2 and D3 
Button btn(4, PULLUP); // mode button

enum app_mode_e {
  mode_volume = 0, 
  mode_bass, 
  mode_treble,
  mode_balance
};

enum app_effect_e {
  effect_stereo = 0,
  effect_enhanced_stereo,
  effect_mono,
  effect_enhanced_mono
};

enum app_buttons_e {
  btn_none = 0,
  btn_source,
  btn_mute,
  btn_volume,
  btn_bass,
  btn_treble
};

int values[NUM_MODES];
int prev_values[NUM_MODES];
int source = 0;
int prev_source = 0;
int mute = 0;
int prev_mute = 0;
int eeprom_address_offset = 500;

int current_mode;
int prev_mode;
bool power_done = false;

unsigned long last_pressed = 0;

 // custom LCD characters (bars)
 byte p1[8] = {
  0b10000,
  0b10000,
  0b10000,
  0b10100,
  0b10100,
  0b10000,
  0b10000,
  0b10000
};
 
byte p2[8] = {
  0b10100,
  0b10100,
  0b10100,
  0b10100,
  0b10100,
  0b10100,
  0b10100,
  0b10100
};
  
byte p3[8] = {
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101,
  0b10101
};

byte p4[8] = {
  0b00101,
  0b00101,
  0b00101,
  0b00101,
  0b00101,
  0b00101,
  0b00101,
  0b00101
};

byte p5[8] = {
  0b00001,
  0b00001,
  0b00001,
  0b00101,
  0b00101,
  0b00001,
  0b00001,
  0b00001
};

byte p6[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00100,
  0b00100,
  0b00000,
  0b00000,
  0b00000
};

byte p7[8] = {
  0b00000,
  0b00001,
  0b00011,
  0b00111,
  0b00111,
  0b00011,
  0b00001,
  0b00000
};

byte p8[8] = {
  0b00000,
  0b10000,
  0b11000,
  0b11100,
  0b11100,
  0b11000,
  0b10000,
  0b00000
};

void setup() {

  // defaults
  for (int i=0; i<NUM_MODES; i++) {
    values[i] = 0;
    prev_values[i] = 0;
  }
  
  current_mode = mode_volume;
  prev_mode = mode_volume;

  restoreValues();
  enc.write(values[current_mode]);

  lcd.begin(16, 2);
  lcd.clear();
  
  lcd.createChar(1, p1);
  lcd.createChar(2, p2);
  lcd.createChar(3, p3);
  lcd.createChar(4, p4);
  lcd.createChar(5, p5);
  lcd.createChar(6, p6);
  lcd.createChar(7, p7);
  lcd.createChar(8, p8);
  
  pinMode(A0, INPUT);
  digitalWrite(A0, HIGH);

  I2c.begin();
  I2c.pullup(true);
  sendTDA();
    
  if (!power_done) {
    powerUp();
    power_done = true;
  }
}

void loop() {
  
  OnModeChanged();
  
  values[current_mode] = readEncoder();
  if (prev_values[current_mode] != values[current_mode] || prev_source != source || prev_mute != mute) {
    storeValues();
    prev_values[current_mode] = values[current_mode];
    prev_source = source;
    prev_mute = mute;
    sendTDA();
  }
  
  switch (current_mode) {
    case mode_volume:
       AppVolume();
    break;
    case mode_bass:
       AppBass();
    break;
    case mode_treble:
       AppTreble();
    break;
    case mode_balance:
       AppBalance();
    break;
  }
}

void AppVolume() {
  printTitle("VOLUME", values[current_mode]);  
  printSource();
  if (mute == 1) {
    lcd.setCursor(0,1);
    lcd.print("----- MUTE -----");
  } else {
    printBar(values[current_mode]);
  }
}

void AppBass() {
  printTitle("BASS", values[current_mode]);
  printSource();
  printBar(values[current_mode]);
}

void AppTreble() {
  printTitle("TREBLE", values[current_mode]);  
  printSource();
  printBar(values[current_mode]);
}

void AppBalance() {
  printTitle("BALANCE", values[current_mode]);  
  printSource();
  printBalance(values[current_mode]);
}

void powerUp() {
  lcd.setCursor(0,0);
  lcd.print("  MOBY AMP 1.0  ");
  int volume = values[mode_volume];
  for (int i=0; i<volume; i++) {
    values[mode_volume] = i;
    sendTDA();
    printBar(i);
    delay(50);
  }
  lcd.clear();
}

void OnModeChanged() {
  
  unsigned long current = millis();
  
  if (prev_mode != current_mode) {
     lcd.clear();
     prev_mode = current_mode;
  }
  
  if (btn.isPressed() && current - last_pressed > DEBOUNCE) {
    if (current_mode == mode_balance) {
      current_mode = mode_volume;
    } else {
      current_mode++;
    }
    enc.write(values[current_mode]);
    last_pressed = current;
  }
  
  int kbd_btn = readKeyboard();
  if (kbd_btn > 0 && current - last_pressed > DEBOUNCE) {
    switch (kbd_btn) {
      case btn_source:
        source = (source == 1) ? 0 : 1;
      break;
      case btn_mute:
        mute = (mute == 1) ? 0 : 1;
      break;
      case btn_volume:
        current_mode = mode_volume;
        enc.write(values[current_mode]);
      break;
      case btn_bass:
        current_mode = mode_bass;
        enc.write(values[current_mode]);
      break;
      case btn_treble:
        current_mode = mode_treble;
        enc.write(values[current_mode]);
      break;
    }
    last_pressed = current;
  }  
}


void restoreValues() {
  
  byte value;
  int addr;
  
  // volume / bass / treble / balance
  for (int i=0; i<NUM_MODES; i++) {
    addr = i + eeprom_address_offset;
    value = EEPROM.read(addr);
    // defaults
    if (value < 0 || value > 100) {
      value = (i == mode_balance) ? 50 : 0;
    }
    values[i] = value;
    prev_values[i] = value;
  }
  
  // source switch
  addr = NUM_MODES + eeprom_address_offset;
  source = EEPROM.read(addr);
  if (source > 1) {
    source = 0;
  }
  prev_source = source;
  
  // mute switch
  addr = NUM_MODES + 1 + eeprom_address_offset;
  mute = EEPROM.read(addr);
  if (mute > 1) {
    mute = 0;
  }
  prev_mute = mute;

}

void storeValue(int mode) {
  int addr = mode + eeprom_address_offset;
  EEPROM.write(addr, values[mode]);  
}

void storeValues() {
  // volume / bass / treble / balance
  for (int i=0; i<NUM_MODES; i++) {
    storeValue(i);
  }
  int addr;
  // source
  addr = NUM_MODES + eeprom_address_offset;
  EEPROM.write(addr, source);
  // mute
  addr = NUM_MODES + 1 + eeprom_address_offset;
  EEPROM.write(addr, mute);
}

int readKeyboard() {
  int val = analogRead(A0);
  if (val <= 100) {
    return btn_source;
  }
  if (val <= 300) {
    return btn_mute;
  } 
  if (val <= 400) {
    return btn_volume;
  }
  if (val <= 500) {
    return btn_bass;
  }
  if (val <= 560) {
    return btn_treble;
  }
  return btn_none;
}

int readEncoder() {
  int value = enc.read();
  if (value > 100) {
    value = 100;
    enc.write(100);
  }
  if (value < 0) {
    value = 0;
    enc.write(0);
  }
  return value;
}

void sendTDA() {

  int volume       = values[mode_volume];
  int balance      = values[mode_balance] - 50;
  int balance_diff = (balance * volume) / 100;
  int volume_left  = volume - ((balance_diff > 0) ? balance_diff : 0);
  int volume_right = volume + ((balance_diff < 0) ? balance_diff : 0);

  volume_left = map(volume_left, 0, 100, 28, 63);
  volume_right = map(volume_right, 0, 100, 28, 63);

  int bass = map(values[mode_bass], 0, 100, 0, 15);
  int treble = map(values[mode_treble], 0, 100, 0, 15); 
  
  I2c.write(TDA8425_ADDR >> 1, TDA_VOL_LEFT, byte(volume_left) | 0xC0);
  I2c.write(TDA8425_ADDR >> 1, TDA_VOL_RIGHT, byte(volume_right) | 0xC0);
  I2c.write(TDA8425_ADDR >> 1, TDA_BASS, byte(bass) | 0xF0);
  I2c.write(TDA8425_ADDR >> 1, TDA_TREBLE, byte(treble) | 0xF0);
  source = (source == 0) ? 0 : 1;
  I2c.write(TDA8425_ADDR >> 1, TDA8425_S1, (mute == 1) ? TDA8425_S1_OFF + source : TDA8425_S1_ON + source);
  
/*
  debug
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(volume_left);
  lcd.print(" : ");
  lcd.print(volume_right);
*/
}
 
 /**
  * Conver string object into signed integer value
  *
  * @param String s
  * @return int
  */
 int stringToInt(String s) {
     char this_char[s.length() + 1];
     s.toCharArray(this_char, sizeof(this_char));
     int result = atoi(this_char);     
     return result;
 }

void printTitle(char* title, int value) {
  lcd.setCursor(0,0);
  lcd.print(title);
  lcd.print(" ");
  lcd.print(value);
  lcd.print("  ");
}

void printSource() {
  lcd.setCursor(13,0);
  lcd.print("[");
  lcd.print(source+1);
  lcd.print("]");
}

 /** 
  * Pring a progress bar on the current cursor position
  *
  * @param int percent
  */
 void printBar(int percent) {

   lcd.setCursor(0,1);
   
   double lenght = COLS + 0.0;
   double value = lenght/100*percent;
   int num_full = 0;
   double value_half = 0.0;
   int peace = 0;
   
   // fill full parts of progress
   if (value>=1) {
    for (int i=1;i<value;i++) {
      lcd.write(3); 
      num_full=i;
    }
    value_half = value-num_full;
  } else {
    value_half = value;
  }
  
  // fill partial part of progress
  peace=value_half*5;
  
  if (peace > 0 && peace <=5) {
    if (peace == 1 || peace == 2) lcd.write(1);
    if (peace == 3 || peace == 4) lcd.write(2);
    if (peace == 5) lcd.write(3);
  }
  
  // fill spaces
  for (int i =0;i<(lenght-num_full);i++) { 
    lcd.write(6);
  }  
 }

 /** 
  * Pring a progress bar on the current cursor position
  *
  * @param int percent
  */
 void printBalance(int percent) {

   byte a[COLS];
   double lenght = COLS + 0.0;
   double value = (lenght/100*(percent-50));
   int num_full = 0;
   double value_half = 0.0;
   int peace = 0;

   // fill all zeroes
   for (int i=0; i< COLS; i++) {
     a[i] = 6;
   }

  // positive balance
  if (value >= 0) {

   // fill full parts of progress
   if (value>=1) {
    for (int i=1;i<value;i++) {
      a[COLS/2 + i - 1] = 3;
      num_full=i;
    }
    value_half = value-num_full;
   } else {
    value_half = value;
   }
  
   // fill partial part of progress
   peace=value_half*5;
  
   if (peace > 0 && peace <=5) {
     int i = COLS/2 + num_full; 
     if (peace == 1 || peace == 2) a[i] = 1;
     if (peace == 3 || peace == 4) a[i] = 2;
     if (peace == 5) a[i] = 3;
   }
  } 

  // negative balance
  else {
  
  // fill full parts of progress
   if (value<=-1) {
    for (int i=-1;i>value;i--) {
      a[COLS/2 + i] = 3;
      num_full=i;
    }
    value_half = value-num_full;
   } else {
    value_half = value;
   }
  
   // fill partial part of progress
   peace=value_half*5;
  
   if (peace < 0 && peace >=-5) {
     int i = COLS/2 + num_full - 1;
     if (peace == -1 || peace == -2) a[i] = 5;
     if (peace == -3 || peace == -4) a[i] = 4;
     if (peace == -5) a[i] = 3;
   }
  }
  
 if (percent == 50) {
    a[COLS/2-1] = 7;
    a[COLS/2] = 8;
  }
 
  lcd.setCursor(0, 1);
  for (int i=0; i<COLS; i++) {
    lcd.write(a[i]);
  }
 
 }
