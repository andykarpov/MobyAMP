/*
 * Moby AMP interface
 *
 * Arduino based front-end using 16x2 LCD, encoder, 5 push buttons, 2x MSGEQ7 ics
 * and PT2322 audio processor
 *
 * @author Andrey Karpov <andy.karpov@gmail.com>
 * @copyright 2013 Andrey Karpov
 */

#include <LiquidCrystal.h>
#include <Encoder.h>
#include <Button.h>
#include <Led.h>
#include <EEPROM.h>
#include <Wire.h>
#include <PT2322.h>

#define ROWS 4 // number of display rows
#define COLS 20 // number of display columns

#define ENC_SCALE_FACTOR 4 // encoder scale

#define MSGEQ_STROBE 6 // В6
#define MSGEQ_RESET 5 // D5
#define MSGEQ_OUT_L 2 // A2
#define MSGEQ_OUT_R 3 // A3

#define PT2322_MIN_VOLUME -79 // dB
#define PT2322_MAX_VOLUME 0 // dB
#define PT2322_MIN_TONE -14 // db
#define PT2322_MAX_TONE 14 // db

#define NUM_MODES 5 // number of app modes

#define EEPROM_ADDRESS_OFFSET 400 // address offset to start reading/wring to the EEPROM 

#define DELAY_MODE 400 // mode switch debounce delay
#define DELAY_EEPROM 10000 // delay to store to the EEPROM
#define DELAY_EQ_MODE 5000 // delay for autoswitch to eq mode

PT2322 audio; // PT2322 board connected to i2c bus (GND, A4, A5)
LiquidCrystal lcd(8, 9, 13, 12, 11, 10); // lcd connected to D8, D9, D13, D12, D11, D10 pins
Encoder enc(2, 3); // encoder pins A and B connected to D2 and D3 
Button btn(4); // mode button
Led backlight(7); // LCD backlight connected to GND and D7

int eq_L[7] = {0,0,0,0,0,0,0}; // 7-band equalizer values for left channel
int eq_R[7] = {0,0,0,0,0,0,0}; // 7-band equalizer values for right channel

// enum with application states
enum app_mode_e {
  mode_volume = 0, 
  mode_bass, 
  mode_middle,
  mode_treble,
  mode_balance
};

// enum with keyboard buttons
enum app_buttons_e {
  btn_none = 0,
  btn_mute,
  btn_volume,
  btn_bass,
  btn_middle,
  btn_treble
};

int values[NUM_MODES];
int prev_values[NUM_MODES];
int mute = 0;
int prev_mute = 0;

int current_mode;
int prev_mode;
bool power_done = false;
bool need_store = false;
bool eq_mode = false;
int char_loaded = 0;

unsigned long last_pressed = 0;
unsigned long last_changed = 0;

// custom LCD characters (volume bars)
byte p1[8] = { 0b10000, 0b10000, 0b10000, 0b10100, 0b10100, 0b10000, 0b10000, 0b10000 };
byte p2[8] = { 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100, 0b10100 };  
byte p3[8] = { 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101, 0b10101 };
byte p4[8] = { 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101, 0b00101 };
byte p5[8] = { 0b00001, 0b00001, 0b00001, 0b00101, 0b00101, 0b00001, 0b00001, 0b00001 };
byte p6[8] = { 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b00000, 0b00000, 0b00000 };
byte p7[8] = { 0b00000, 0b00001, 0b00011, 0b00111, 0b00111, 0b00011, 0b00001, 0b00000 };
byte p8[8] = { 0b00000, 0b10000, 0b11000, 0b11100, 0b11100, 0b11000, 0b10000, 0b00000 };

// custom LCD characters (eq bars)
byte e1[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111 };
byte e2[8] = { 0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b00000, 0b11111, 0b11111 };  
byte e3[8] = { 0b11111, 0b11111, 0b00000, 0b11111, 0b11111, 0b00000, 0b11111, 0b11111 };
byte e4[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000 };

/**
 * Load custom LCD characters for volume control
 */ 
void loadVolumeCharacters() {
  char_loaded = 1;
  lcd.clear();
  lcd.createChar(1, p1);
  lcd.createChar(2, p2);
  lcd.createChar(3, p3);
  lcd.createChar(4, p4);
  lcd.createChar(5, p5);
  lcd.createChar(6, p6);
  lcd.createChar(7, p7);
  lcd.createChar(8, p8);
  lcd.clear();
}

/**
 * Load custom LCD characters for graphical equalizer bars
 */
void loadEqualizerCharacters() {  
  char_loaded = 2;
  lcd.clear();
  lcd.createChar(1, e1);
  lcd.createChar(2, e2);
  lcd.createChar(3, e3);
  lcd.createChar(4, e4);
  lcd.clear();
}

/**
 * Arduino setup routine
 */
void setup() {

  Wire.begin();

  // setup lcd
  lcd.begin(COLS, ROWS);
  lcd.clear();
  loadVolumeCharacters();
  backlight.on();
  //lcd.print(F("test"));
  //delay(1000);

  // defaults
  for (int i=0; i<NUM_MODES; i++) {
    values[i] = 0;
    prev_values[i] = 0;
  }
  
  current_mode = mode_volume;
  prev_mode = mode_volume;

  restoreValues();
  enc.write(values[current_mode]*ENC_SCALE_FACTOR);
  
  audio.init();

  // send volume and tones to the PT2322 board
  sendPT2322();
  lcd.print("send");
  
  // setup MSGEQ board
  pinMode(MSGEQ_STROBE, OUTPUT);
  pinMode(MSGEQ_RESET, OUTPUT);
  analogReference(DEFAULT);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT);
  pinMode(4, INPUT_PULLUP);
    
  pinMode(A0, INPUT_PULLUP);
  digitalWrite(A0, HIGH);

  pinMode(A1, INPUT_PULLUP);
  digitalWrite(A1, HIGH);

  if (!power_done) {
    powerUp();
    power_done = true;
  }
}

/**
 * Main app loop
 */
void loop() {
  
  OnModeChanged();
  
  values[current_mode] = readEncoder();
  
  unsigned long current = millis();
  
  if (prev_values[current_mode] != values[current_mode] || prev_mute != mute) {
    last_changed = current;
    need_store = true;
    prev_values[current_mode] = values[current_mode];
    prev_mute = mute;
    sendPT2322();
    eq_mode = false;
  }
  
  // store settings in EEPROM with 10s delay to reduce number of write cycles
  if (need_store && current - last_changed >= 10000) {
      storeValues();
      need_store = false;
  }

  if ((current - last_changed >= 5000) && (current - last_pressed >= 5000)) {
    eq_mode = true;
    current_mode = mode_volume;
    prev_mode = mode_volume;
  } else {
    eq_mode = false;
  }
  
  if (eq_mode && char_loaded != 2) {
    loadEqualizerCharacters();
  }
  
  if (!eq_mode && char_loaded != 1) {
    loadVolumeCharacters();
  }
  
  if (eq_mode) {
    AppEq();
  } else {
    switch (current_mode) {
      case mode_volume:
         AppVolume();
      break;
      case mode_bass:
         AppBass();
      break;
      case mode_middle:
         AppMiddle();
      break;
      case mode_treble:
         AppTreble();
      break;
      case mode_balance:
         AppBalance();
      break;
    }
  }
}

/**
 * Application mode to control volume
 */
void AppVolume() {
  printTitle("VOLUME", values[current_mode]);  
  printStoreStatus();
  if (mute == 1) {
    lcd.setCursor(0,1);
    lcd.print((COLS == 20) ? F("------- MUTE -------") : F("----- MUTE -----"));
  } else {
    printBar(values[current_mode]);
  }
}

/**
 * Application mode to control bass tone
 */
void AppBass() {
  printTitle("BASS", values[current_mode]);
  printStoreStatus();
  printBar(values[current_mode]);
}

/**
 * Application mode to control mid tone
 */
void AppMiddle() {
  printTitle("MIDDLE", values[current_mode]);  
  printStoreStatus();
  printBar(values[current_mode]);
}

/**
 * Application mode to control treble tone
 */
void AppTreble() {
  printTitle("TREBLE", values[current_mode]);  
  printStoreStatus();
  printBar(values[current_mode]);
}

/**
 * Application mode to control balance
 */
void AppBalance() {
  printTitle("BALANCE", values[current_mode]);  
  printStoreStatus();
  printBalance(values[current_mode]);
}

/**
 * Application mode to display a graphical equalizer (2x 7-band)
 */
void AppEq() {
  readMsgeq();

  lcd.setCursor(0, ROWS-1);
  lcd.print(F("L"));
  lcd.setCursor(COLS-1, ROWS-1);
  lcd.print(F("R"));
  
  printStoreStatus();
  
  for (int i=0; i<7; i++) {
    int val_L = map(eq_L[i], 0, 1023, 0, 8 * ROWS);
    int val_R = map(eq_R[i], 0, 1023, 0, 8 * ROWS);
    for (int j=0; j<ROWS; j++) {
      int mx = 8*(j+1);
      int v_l = (val_L >= mx) ? 8 : val_L - mx + 8;
      if (v_l < 0) v_l = 0;
      int v_r = (val_R >= mx) ? 8 : val_R - mx + 8;
      if (v_r < 0) v_r = 0;
      
      lcd.setCursor(i + ((COLS == 20) ? 2 : 1), ROWS-j-1);
      if (v_l == 1 || v_l == 2 || v_l == 3) lcd.write(1);
      if (v_l == 4 || v_l == 5 || v_l == 6) lcd.write(2);
      if (v_l == 7 || v_l >= 8) lcd.write(3);
      if (v_l == 0) lcd.write(4);

      lcd.setCursor(i + ((COLS == 20) ? 11 : 8), ROWS-j-1);
      if (v_r == 1 || v_r == 2 || v_r == 3) lcd.write(1);
      if (v_r == 4 || v_r == 5 || v_r == 6) lcd.write(2);
      if (v_r == 7 || v_r >= 8) lcd.write(3);
      if (v_r == 0) lcd.write(4);
    }
    //delay(2);
  }  
}

/**
 * Power up routine to smooth volume on start-up from 0 to stored value
 */
void powerUp() {
  lcd.setCursor(0,0);
  lcd.print(F("  MOBY AMP 1.1  "));
  int volume = values[mode_volume];
  for (int i=0; i<volume; i++) {
    values[mode_volume] = i;
    sendPT2322();
    printBar(i);
    delay(50);
  }
  lcd.clear();
}

/**
 * Mode change handler
 */
void OnModeChanged() {
  
  unsigned long current = millis();
  
  if (prev_mode != current_mode) {
     lcd.clear();
     prev_mode = current_mode;
  }
    
  if (btn.pressed() && current - last_pressed > DELAY_MODE) {
    if (current_mode == mode_balance) {
      current_mode = mode_volume;
    } else {
      current_mode++;
    }
    enc.write(values[current_mode]*ENC_SCALE_FACTOR);
    last_pressed = current;
  }
  
  int kbd_btn = readKeyboard();
  if (kbd_btn > 0 && current - last_pressed > DELAY_MODE) {
    switch (kbd_btn) {
      case btn_mute:
        mute = (mute == 1) ? 0 : 1;
      break;
      case btn_volume:
        current_mode = mode_volume;
        enc.write(values[current_mode]*ENC_SCALE_FACTOR);
      break;
      case btn_bass:
        current_mode = mode_bass;
        enc.write(values[current_mode]*ENC_SCALE_FACTOR);
      break;
      case btn_middle:
        current_mode = mode_treble;
        enc.write(values[current_mode]*ENC_SCALE_FACTOR);
      break;
      case btn_treble:
        current_mode = mode_treble;
        enc.write(values[current_mode]*ENC_SCALE_FACTOR);
      break;
    }
    last_pressed = current;
  }  
}

/**
 * Restore values from EEPROM
 */
void restoreValues() {
  
  byte value;
  int addr;
  
  // volume / bass / treble / balance
  for (int i=0; i<NUM_MODES; i++) {
    addr = i + EEPROM_ADDRESS_OFFSET;
    value = EEPROM.read(addr);
    // defaults
    if (value < 0 || value > 100) {
      value = (i == mode_balance) ? 50 : 0;
    }
    values[i] = value;
    prev_values[i] = value;
  }
    
  // mute switch
  addr = NUM_MODES + EEPROM_ADDRESS_OFFSET;
  mute = EEPROM.read(addr);
  if (mute > 1) {
    mute = 0;
  }
  prev_mute = mute;

}

/**
 * Store value into the EEPROM
 */
void storeValue(int mode) {
  int addr = mode + EEPROM_ADDRESS_OFFSET;
  EEPROM.write(addr, values[mode]);  
}

/**
 * Store values into the EEPROM
 */
void storeValues() {
  // volume / bass / treble / balance
  for (int i=0; i<NUM_MODES; i++) {
    storeValue(i);
  }
  int addr;
  // mute
  addr = NUM_MODES + EEPROM_ADDRESS_OFFSET;
  EEPROM.write(addr, mute);
}

/**
 * Read keyboard event
 * @return int
 */
int readKeyboard() {
  
  int val = analogRead(A0);
  if (val <= 100) {
    return btn_mute;
  }
  if (val <= 300) {
    return btn_volume;
  } 
  if (val <= 400) {
    return btn_bass;
  }
  if (val <= 500) {
    return btn_middle;
  }
  if (val <= 560) {
    return btn_treble;
  }
  if (digitalRead(A1) == LOW) {
    return btn_mute;
  }
  
  return btn_none;
}

/**
 * Read encoder value
 * @return int
 */
int readEncoder() {
  int value = enc.read()/ENC_SCALE_FACTOR;
  if (value > 100) {
    value = 100;
    enc.write(100*ENC_SCALE_FACTOR);
  }
  if (value < 0) {
    value = 0;
    enc.write(0);
  }
  return value;
}

/**
 * Read equalizer data from the 2xMSGEQ7 chips into the eq_L and eq_R arrays
 */
void readMsgeq() {
  digitalWrite(MSGEQ_RESET, HIGH);
  digitalWrite(MSGEQ_RESET, LOW);
  for (int i = 0; i < 7; i++) {
    digitalWrite(MSGEQ_STROBE, LOW);
    delayMicroseconds(30);
    eq_L[i] = analogRead(MSGEQ_OUT_L);
    eq_R[i] = analogRead(MSGEQ_OUT_R);
    digitalWrite(MSGEQ_STROBE,HIGH);
  }
  digitalWrite(MSGEQ_RESET, LOW);
  digitalWrite(MSGEQ_STROBE, HIGH);
}

/**
 * Send tone control values to the PT2322
 */
void sendPT2322() {
  
  int volume       = values[mode_volume];
  int balance      = values[mode_balance] - 50;
  int balance_diff = (balance * volume) / 100;
  int volume_left  = volume - ((balance_diff > 0) ? balance_diff : 0);
  int volume_right = volume + ((balance_diff < 0) ? balance_diff : 0);

  volume_left = map(volume_left, 0, 100, -15, 0);
  volume_right = map(volume_right, 0, 100, -15, 0);
  
  audio.masterVolume(map(values[mode_volume], 0, 100, PT2322_MIN_VOLUME, PT2322_MAX_VOLUME));
  audio.leftVolume(volume_left);
  audio.rightVolume(volume_right);
  audio.centerVolume(-15); // off
  audio.rearLeftVolume(-15); // off
  audio.rearRightVolume(-15); // off
  audio.subwooferVolume(-15); // off
  audio.bass(map(values[mode_bass], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio.middle(map(values[mode_middle], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio.treble(map(values[mode_treble], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
  audio._3DOff(); // 3d
  audio.toneOn(); // tone Defeat
  if (mute) {
    audio.muteOn(); // mute off
  } else {
    audio.muteOff(); // mute off
  }
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

/**
 * Print mode title
 * @param char* title
 * @param int value
 */
void printTitle(char* title, int value) {
  lcd.setCursor(0,0);
  lcd.print(title);
  lcd.print(F(" "));
  lcd.print(value);
  lcd.print(F("  "));
}

/**
 * Print store status
 */
void printStoreStatus() {
  lcd.setCursor(COLS-1,0);
  lcd.print((need_store) ? F("*") : F(" ")); 
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
   int pos = 0;

   // fill full parts of progress
   if (value>=1) {
    for (int i=1;i<value;i++) {
      lcd.write(3); 
      pos++;
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
    pos++;
  }
  
  // fill spaces
  for (int i =0;i<(lenght-pos);i++) { 
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
