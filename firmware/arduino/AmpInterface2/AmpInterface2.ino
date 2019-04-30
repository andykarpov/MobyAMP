/*
   Moby AMP interface 2.0

   Arduino based front-end using 20x4 LCD, encoder, 5 push buttons, 2x MSGEQ7 ics
   and PT2322 audio processor

   @author Andrey Karpov <andy.karpov@gmail.com>
   @copyright 2013, 2019 Andrey Karpov
*/

#include <LiquidCrystal.h>
#include <NewEncoder.h>
#include <Button.h>
#include <Led.h>
#include <EEPROM.h>
#include <Wire.h>
#include <PT2322.h>
#include <jm_PCF8574.h>

const uint8_t ROWS = 4; // number of display rows
const uint8_t COLS = 20; // number of display columns

const uint8_t ENC_INCREMENT = 5; // encoder increment
#define ENC_PINA 2
#define ENC_PINB 3
#define ENC_TYPE FULL_PULSE

#define MSGEQ_STROBE 6 // В6
#define MSGEQ_RESET 5 // D5
#define MSGEQ_OUT_L 2 // A2
#define MSGEQ_OUT_R 3 // A3

#define PIN_RELAY1 0 // D0
#define PIN_RELAY2 1 // D1

const unsigned long BOOT_DELAY = 5000; // ms

#define PT2322_MIN_VOLUME -79 // dB
#define PT2322_MAX_VOLUME 0 // dB
#define PT2322_MIN_TONE -14 // db
#define PT2322_MAX_TONE 14 // db

#define NUM_MODES 8 // number of app modes

#define EEPROM_ADDRESS_OFFSET 400 // address offset to start reading/wring to the EEPROM 

const int DELAY_MODE = 400; // mode switch debounce delay
const unsigned long DELAY_EEPROM = 10000; // delay to store to the EEPROM
const unsigned long DELAY_EQ_MODE = 5000; // delay for autoswitch to eq mode

#define PCF_I2C_ADR1 0x20 // 0x20 is the default address for the PCF8574 with all three input pins tied to ground.
#define PCF_I2C_ADR2 0x70 // 0x70 is the default address for the PCF8574A with all three input pins tied to ground.

#define PCF_I2C_P7  ((uint8_t) 0b10000000)  // P7
#define PCF_I2C_P6  ((uint8_t) 0b01000000)  // P6
#define PCF_I2C_P5  ((uint8_t) 0b00100000)  // P5
#define PCF_I2C_P4  ((uint8_t) 0b00010000)  // P4
#define PCF_I2C_P3  ((uint8_t) 0b00001000)  // P3
#define PCF_I2C_P2  ((uint8_t) 0b00000100)  // P2
#define PCF_I2C_P1  ((uint8_t) 0b00000010)  // P1
#define PCF_I2C_P0  ((uint8_t) 0b00000001)  // P0

PT2322 audio; // PT2322 board connected to i2c bus (GND, A4, A5)
LiquidCrystal lcd(8, 9, 13, 12, 11, 10); // lcd connected to D8, D9, D13, D12, D11, D10 pins
NewEncoder enc(ENC_PINA, ENC_PINB, 0, 100, 0, ENC_TYPE, ENC_INCREMENT);

Button btn(4); // mode button
Led backlight(7); // LCD backlight connected to GND and D7

jm_PCF8574 pcf8574; // I2C port extender

int eq_L[7] = {0, 0, 0, 0, 0, 0, 0}; // 7-band equalizer values for left channel
int eq_R[7] = {0, 0, 0, 0, 0, 0, 0}; // 7-band equalizer values for right channel

// enum with application states
enum app_mode_e {
  mode_volume = 0,
  mode_balance,
  mode_bass,
  mode_middle,
  mode_treble,
  mode_channel,
  mode_passthru,
  mode_mono,
  mode_mute
};

// enum with keyboard buttons
enum app_buttons_e {
  btn_none = 0,
  btn_mute,
  btn_ch_prev,
  btn_ch_next,
  btn_menu_prev,
  btn_menu_next,
  btn_mono_selector,
  btn_eq_selector
};

int values[NUM_MODES];
int prev_values[NUM_MODES];

bool now_mute = false;
bool prev_mute = false;

int current_mode;
int prev_mode;
int prev_encoder_value;
bool power_done = false;
bool need_store = false;
bool eq_mode = false;
bool info_mode = false;
int char_loaded = 0;
bool current_mode_is_tones = false;
bool current_mode_is_switches = false;

unsigned long last_key_pressed = 0;
unsigned long last_btn_pressed = 0;
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

// custom LCD characters (channels)
byte c1[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111 };
byte c2[8] = { 0b00000, 0b00000, 0b00000, 0b01110, 0b01110, 0b01110, 0b00000, 0b11111 };

/**
   Load custom LCD characters for volume control
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
   Load custom LCD characters for graphical equalizer bars
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
   Load custom LCD characters for channel selector
*/
void loadChannelCharacters() {
  char_loaded = 3;
  lcd.clear();
  lcd.createChar(1, c1);
  lcd.createChar(2, c2);
  lcd.clear();
}

/**
   Arduino setup routine
*/
void setup() {

  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_RELAY2, OUTPUT);

  digitalWrite(PIN_RELAY1, LOW);
  digitalWrite(PIN_RELAY2, LOW);

  Wire.begin();

  // setup lcd
  lcd.begin(COLS, ROWS);
  lcd.clear();
  loadVolumeCharacters();
  backlight.on();

  // defaults
  for (int i = 0; i < NUM_MODES; i++) {
    values[i] = 0;
    prev_values[i] = 0;
  }

  current_mode = mode_volume;
  prev_mode = mode_volume;

  restoreValues();

  audio.init();

  // send volume and tones to the PT2322 board
  sendPT2322All();
  lcd.print("send");

  // try to init pcf8574 with 2 different addresses (8574 vs 8574A)
  if (!pcf8574.begin(PCF_I2C_ADR1)) {
    pcf8574.begin(PCF_I2C_ADR2);
  }

  // clear All port extender values
  pcf8574.pinMode( PCF_I2C_P0, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P1, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P2, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P3, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P4, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P5, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P6, OUTPUT );
  pcf8574.pinMode( PCF_I2C_P7, OUTPUT );

  pcf8574.digitalWrite( PCF_I2C_P0, LOW );
  pcf8574.digitalWrite( PCF_I2C_P1, LOW );
  pcf8574.digitalWrite( PCF_I2C_P2, LOW );
  pcf8574.digitalWrite( PCF_I2C_P3, HIGH ); // disable mux
  pcf8574.digitalWrite( PCF_I2C_P4, LOW ); // disable mono A
  pcf8574.digitalWrite( PCF_I2C_P5, LOW ); // disable mono B
  pcf8574.digitalWrite( PCF_I2C_P6, LOW ); // disable mono A+B
  pcf8574.digitalWrite( PCF_I2C_P7, LOW ); // disable EQ passthru

  sendPcf();

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

  enc.configure(ENC_PINA, ENC_PINB, 0, 100, values[current_mode], ENC_TYPE, ENC_INCREMENT); // set encoder bounds
  enc.begin();
  enc.setValue(values[current_mode]); // set encoder value

}

/**
   Main app loop
*/
void loop() {

  OnModeChanged();

  current_mode_is_tones = (current_mode == mode_volume || current_mode == mode_balance || current_mode == mode_bass || current_mode == mode_treble || current_mode == mode_middle) ? true : false;
  current_mode_is_switches = (current_mode == mode_channel || current_mode == mode_passthru || current_mode == mode_mono) ? true : false;
  unsigned long current = millis();

  // read encoder only for sound control modes and only 5s after keypress
  if (current_mode_is_tones) {
    //if ((current - last_key_pressed >= 2000)) {
      values[current_mode] = readEncoder();
    //} else {
    //  enc.configure(ENC_PINA, ENC_PINB, 0, 100, values[current_mode], ENC_TYPE, ENC_INCREMENT); // set encoder bounds 0..100, increment 5
    //  enc.begin();
    //  enc.setValue(values[current_mode]);      
    //}
  }

  // store settings in EEPROM with 10s delay to reduce number of write cycles
  if (need_store && current - last_changed >= 10000) {
    storeValues();
    need_store = false;
  }

  if ((current - last_changed >= 5000) && (current - last_key_pressed >= 5000)) {
    eq_mode = true;
    if (current_mode != mode_volume) {
      current_mode = mode_volume;
      prev_mode = mode_volume;
      enc.configure(ENC_PINA, ENC_PINB, 0, 100, values[current_mode], ENC_TYPE, ENC_INCREMENT); // set encoder bounds 0..100, increment 5
      enc.begin();
      enc.setValue(values[current_mode]);
    }
  } else {
    eq_mode = false;
  }

  // process current value change (except channel / passthru / mono)
  if (current_mode_is_tones && prev_values[current_mode] != values[current_mode]) {
    last_changed = current;
    need_store = true;
    prev_values[current_mode] = values[current_mode];
    sendPT2322Value(current_mode, values[current_mode]);
    eq_mode = false;
  }

  // process input switch / passthru / mono
  if (current_mode_is_switches && prev_values[current_mode] != values[current_mode]) {
    last_changed = current;
    need_store = true;
    prev_values[current_mode] = values[current_mode];
    sendPcf();
    eq_mode = false;
  }

  // process mute change
  if (prev_mute != now_mute) {
    last_changed = current;
    need_store = true;
    prev_mute = now_mute;
    //sendPT2322Value(mode_mute, now_mute);
    sendPT2322All();
    eq_mode = false;
  }

  if (eq_mode && char_loaded != 2) {
    loadEqualizerCharacters();
  }

  if (!eq_mode) {
    if (current_mode_is_switches && char_loaded != 3) {
      loadChannelCharacters();
    } 
    else if (current_mode_is_tones && char_loaded != 1) {
      loadVolumeCharacters();
    }
  }

  if (eq_mode) {
    if (info_mode) {
      AppInfo();
    } else {
      AppEq();
    }
  } else {
    switch (current_mode) {
      case mode_volume:
        AppVolume();
        break;
      case mode_channel:
        AppChannel();
        break;
      case mode_passthru:
        AppPassthru();
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
      case mode_mono:
        AppMono();
        break;
    }
  }
}

void AppVolume() {
  int i = map(values[mode_volume], 0, 100, PT2322_MIN_VOLUME, PT2322_MAX_VOLUME);
  printTitle("VOLUME", i);
  printStoreStatus();
  if (now_mute == true) {
    lcd.setCursor(0, 1);
    lcd.print((COLS == 20) ? F("------- MUTE -------") : F("----- MUTE -----"));
  } else {
    printBar(values[mode_volume]);
  }
}

void AppChannel() {
  int ch = constrain(values[mode_channel], 0, 7);
  printTitle("SOURCE", ch+1);
  printStoreStatus();
  printChannelBar(ch);
}

void AppPassthru() {
  int en = constrain(values[mode_passthru], 0, 1);
  
  lcd.setCursor(0, 0);
  lcd.print(F("EQ PASSTHRU"));

  printStoreStatus();

  lcd.setCursor(0, 1);
  lcd.write( (en == 1) ? 2 : 1);
  lcd.print(F(" enabled"));

  lcd.setCursor(0, 2);
  lcd.write( (en == 1) ? 1 : 2);
  lcd.print(F(" disabled"));

}

void AppMono() {
  int mono = constrain(values[mode_mono], 0, 3);
  
  lcd.setCursor(0, 0);
  lcd.print(F("MONO DOWNMIX"));

  printStoreStatus();

  lcd.setCursor(0, 1);
  lcd.write( (mono == 0) ? 2 : 1);
  lcd.print(F(" Normal Stereo "));

  lcd.setCursor(0, 2);
  lcd.write( (mono == 1) ? 2 : 1);
  lcd.print(F(" Mono A "));
  lcd.write( (mono == 2) ? 2 : 1);
  lcd.print(F(" Mono B "));
  
  lcd.setCursor(0, 3);
  lcd.write( (mono == 3) ? 2 : 1);
  lcd.print(F(" Mono A + B "));
}

void AppBass() {
  int i = map(values[mode_bass], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  printTitle("BASS", i);
  printStoreStatus();
  printBar(values[mode_bass]);
}

void AppMiddle() {
  int i = map(values[mode_middle], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  printTitle("MIDDLE", i);
  printStoreStatus();
  printBar(values[mode_middle]);
}

void AppTreble() {
  int i = map(values[mode_treble], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  printTitle("TREBLE", i);
  printStoreStatus();
  printBar(values[mode_treble]);
}

void AppBalance() {
  printTitle("BALANCE", values[mode_balance]);
  printStoreStatus();
  printBalance(values[mode_balance]);
}

void AppEq() {
  readMsgeq();

  lcd.setCursor(0, ROWS - 1);
  lcd.print(F("L"));
  lcd.setCursor(COLS - 1, ROWS - 1);
  lcd.print(F("R"));

  printStoreStatus();

  for (int i = 0; i < 7; i++) {
    int val_L = map(eq_L[i], 0, 1023, 0, 8 * ROWS);
    int val_R = map(eq_R[i], 0, 1023, 0, 8 * ROWS);
    for (int j = 0; j < ROWS; j++) {
      int mx = 8 * (j + 1);
      int v_l = (val_L >= mx) ? 8 : val_L - mx + 8;
      if (v_l < 0) v_l = 0;
      int v_r = (val_R >= mx) ? 8 : val_R - mx + 8;
      if (v_r < 0) v_r = 0;

      lcd.setCursor(i + ((COLS == 20) ? 2 : 1), ROWS - j - 1);
      if (v_l == 1 || v_l == 2 || v_l == 3) lcd.write(1);
      if (v_l == 4 || v_l == 5 || v_l == 6) lcd.write(2);
      if (v_l == 7 || v_l >= 8) lcd.write(3);
      if (v_l == 0) lcd.write(4);

      lcd.setCursor(i + ((COLS == 20) ? 11 : 8), ROWS - j - 1);
      if (v_r == 1 || v_r == 2 || v_r == 3) lcd.write(1);
      if (v_r == 4 || v_r == 5 || v_r == 6) lcd.write(2);
      if (v_r == 7 || v_r >= 8) lcd.write(3);
      if (v_r == 0) lcd.write(4);
    }
    //delay(2);
  }
}

void AppInfo() {
  int volume = map(values[mode_volume], 0, 100, PT2322_MIN_VOLUME, PT2322_MAX_VOLUME);
  int balance = values[mode_balance];
  int bass = map(values[mode_bass], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  int middle = map(values[mode_middle], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  int treble = map(values[mode_treble], 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE);
  int ch = constrain(values[mode_channel], 0, 7) + 1;
  int mono = constrain(values[mode_mono], 0, 3);
  int passthrough = constrain(values[mode_passthru], 0, 1);
  
  lcd.setCursor(0,0);
  lcd.print(F("VOL ")); 
  if (now_mute == true) {
    lcd.print(F("MUTE"));
  } else {
    lcd.print(volume);
    lcd.print(F("dB"));
  }

  lcd.setCursor(10,0);
  lcd.print(F("BAL "));
  lcd.print(balance);
  lcd.print(F(" %"));

  lcd.setCursor(0,1);
  lcd.print(F("BAS ")); 
  lcd.print(bass);
  lcd.print(F("dB"));

  lcd.setCursor(10,1);
  lcd.print(F("CH "));
  lcd.print(ch);

  lcd.setCursor(0,2);
  lcd.print(F("MID ")); 
  lcd.print(middle);
  lcd.print(F("dB"));

  lcd.setCursor(10,2);
  switch (mono) {
    case 0:
      lcd.print(F("STEREO"));
    break;
    case 1:
      lcd.print(F("MONO A"));
    break;
    case 2:
      lcd.print(F("MONO B"));
    break;
    case 3:
      lcd.print(F("MONO A+B"));
    break;
  }

  lcd.setCursor(0,3);
  lcd.print(F("TRE ")); 
  lcd.print(treble);
  lcd.print(F("dB"));

  lcd.setCursor(10,3);
  switch (passthrough) {
    case 0:
      lcd.print(F("EQ ON"));
    break;
    case 1:
      lcd.print(F("EQ OFF"));
    break;
  }
  
}

void powerUp() {

  audio.muteOn();

  lcd.clear();
  delay(50);

  lcd.setCursor(0, 0); lcd.print(F("    HI-FI STEREO    "));
  lcd.setCursor(0, 1); lcd.print(F("    Version 2.35    "));
  lcd.setCursor(0, 2); lcd.print(F("                    "));
  lcd.setCursor(0, 3); lcd.print(F("   Made in Ukraine  "));

  delay(500);

  lcd.setCursor(0, 1); lcd.print(F("                    "));
  lcd.setCursor(0, 2); lcd.print(F("      BOOTING       "));
  lcd.setCursor(0, 3); lcd.print(F("                    "));

  /*int steps = BOOT_DELAY / 50;
    for (int i=0; i<=steps; i++) {
    int j = map(steps, 0, steps, 0, 100);
    j = constrain(j, 0, 100);
    printBar(j);
    delay(50);
    }*/
  delay(BOOT_DELAY);

  audio.muteOff();

  digitalWrite(PIN_RELAY1, HIGH);
  digitalWrite(PIN_RELAY2, HIGH);

  lcd.clear();
  delay(50);

  lcd.setCursor(0, 0); lcd.print(F("                    "));
  lcd.setCursor(0, 1); lcd.print(F("                    "));
  lcd.setCursor(0, 2); lcd.print(F("      LOADING       "));
  lcd.setCursor(0, 3); lcd.print(F("                    "));

  int volume = values[mode_volume];
  for (int i = 0; i <= volume; i++) {
    values[mode_volume] = i;
    sendPT2322Value(mode_volume, i);
    printBar(i);
    delay(50);
  }

  lcd.clear();
}

void OnModeChanged() {

  unsigned long current = millis();

  if (btn.pressed() && current - last_btn_pressed > DELAY_MODE) {
    last_btn_pressed = current;
    lcd.clear();
    if (eq_mode) {
      info_mode = !info_mode;
    }
  }

  int kbd_btn = readKeyboard();
  if (kbd_btn > 0 && current - last_key_pressed > DELAY_MODE) {
    switch (kbd_btn) {
      case btn_mute:
        now_mute = !now_mute;
        last_key_pressed = current;
        break;
      case btn_ch_prev:
        last_key_pressed = current;
        if (current_mode != mode_channel) {
          current_mode = mode_channel;
        } else {
          values[mode_channel]--;
          if (values[mode_channel] < 0) values[mode_channel] = 7;
        }
      break;
      case btn_ch_next:
        last_key_pressed = current;
        if (current_mode != mode_channel) {
          current_mode = mode_channel;
        } else {
          values[mode_channel]++;
          if (values[mode_channel] > 7) values[mode_channel] = 0;
        }
      break;
      case btn_menu_prev:
        if (current_mode > mode_balance && current_mode <= mode_treble) {
          current_mode--;
        } else {
          current_mode = mode_treble;
        }
        last_key_pressed = current;
      break;
      case btn_menu_next:
        if (current_mode >= mode_balance && current_mode < mode_treble) {
          current_mode++;
        } else {
          current_mode = mode_balance;
        }
        last_key_pressed = current;
      break;
      case btn_mono_selector:
        last_key_pressed = current;
        if (current_mode != mode_mono) {      
          current_mode = mode_mono;
        } else {
          values[mode_mono]++;
          if (values[mode_mono] > 3) values[mode_mono] = 0;
        }
      break;
      case btn_eq_selector:
        last_key_pressed = current;
        if (current_mode != mode_passthru) {
          current_mode = mode_passthru;
        } else {
          values[mode_passthru]++;
          if (values[mode_passthru] > 1) values[mode_passthru] = 0;
        }
      break;
    }
  }

  if (prev_mode != current_mode) {
    lcd.clear();
    prev_mode = current_mode;

    if (current_mode == mode_channel) {
        enc.configure(ENC_PINA, ENC_PINB, 0, 7, values[current_mode], ENC_TYPE, 1); // set encoder bounds 0..7, increment 1
    } else if (current_mode == mode_passthru) {
        enc.configure(ENC_PINA, ENC_PINB, 0, 1, values[current_mode], ENC_TYPE, 1); // set encoder bounds 0..1, increment 1
    } else if (current_mode == mode_mono) {
        enc.configure(ENC_PINA, ENC_PINB, 0, 3, values[current_mode], ENC_TYPE, 1); // set encoder bounds 0..3, increment 1        
    } else {
        enc.configure(ENC_PINA, ENC_PINB, 0, 100, values[current_mode], ENC_TYPE, ENC_INCREMENT); // set encoder bounds 0..100, increment 5
    }
    enc.begin();
    enc.setValue(values[current_mode]);
  }
}

/**
   Restore values from EEPROM
*/
void restoreValues() {

  byte value;
  int addr;

  // volume / bass / treble / balance / channel
  for (int i = 0; i < NUM_MODES; i++) {
    addr = i + EEPROM_ADDRESS_OFFSET;
    value = EEPROM.read(addr);
    // defaults

    if (i == mode_channel && (value < 0 || value > 7)) {
      value = 0;
    } 
    if (i == mode_passthru && (value < 0 || value > 1)) {
      value = 0;
    }  
    if (i == mode_mono && (value < 0 || value > 3)) {
      value = 0;
    }    
    if (i != mode_channel && i != mode_passthru && i != mode_mono && (value < 0 || value > 100)) {
      value = (i == mode_balance) ? 50 : 0;
    }
    values[i] = value;
    prev_values[i] = value;
  }

  // mute switch
  addr = NUM_MODES + EEPROM_ADDRESS_OFFSET;
  byte _mute = EEPROM.read(addr);
  now_mute = (_mute > 0) ? true : false;
}

/**
   Store value into the EEPROM
*/
void storeValue(int mode) {
  int addr = mode + EEPROM_ADDRESS_OFFSET;
  EEPROM.write(addr, values[mode]);
}

/**
   Store values into the EEPROM
*/
void storeValues() {
  // volume / bass / treble / balance / channel / passtgru / mono
  for (int i = 0; i < NUM_MODES; i++) {
    storeValue(i);
  }
  int addr;
  // mute
  addr = NUM_MODES + EEPROM_ADDRESS_OFFSET;
  EEPROM.write(addr, ((now_mute == true) ? 1 : 0));
}

/**
   Read keyboard event
   @return int
*/
int readKeyboard() {

  int val = analogRead(A0);

  if ( val >= 5 && val <= 100) { // 14
    return btn_ch_prev;
  } else if (val >= 150 && val <= 250) { // 199
    return btn_ch_next;
  } else if (val >= 300 && val <= 380) { // 328
    return btn_menu_prev;
  } else if (val >= 400 && val <= 500) { // 431
    return btn_menu_next;
  } else if (val >= 520 && val <= 600) { // 544
    return btn_mono_selector;
  } else if (val >= 650 && val <= 800) { // 723
    return btn_eq_selector;
  }
  
  if (digitalRead(A1) == LOW) {
    return btn_mute;
  }

  return btn_none;
}

/**
   Read encoder value
   @return int
*/
int readEncoder() {
  int value = enc.getValue();
  return value;
}

/**
   Read equalizer data from the 2xMSGEQ7 chips into the eq_L and eq_R arrays
*/
void readMsgeq() {
  digitalWrite(MSGEQ_RESET, HIGH);
  digitalWrite(MSGEQ_RESET, LOW);
  for (int i = 0; i < 7; i++) {
    digitalWrite(MSGEQ_STROBE, LOW);
    delayMicroseconds(30);
    eq_L[i] = analogRead(MSGEQ_OUT_L);
    eq_R[i] = analogRead(MSGEQ_OUT_R);
    digitalWrite(MSGEQ_STROBE, HIGH);
  }
  digitalWrite(MSGEQ_RESET, LOW);
  digitalWrite(MSGEQ_STROBE, HIGH);
}

/**
   Send tone control values to the PT2322
*/
void sendPT2322All() {

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
  if (now_mute == true) {
    audio.muteOn(); // mute on
    digitalWrite(PIN_RELAY1, LOW);
    digitalWrite(PIN_RELAY2, LOW);
  } else {
    audio.muteOff(); // mute off
    digitalWrite(PIN_RELAY1, HIGH);
    digitalWrite(PIN_RELAY2, HIGH);
  }
}

void sendPT2322Value(int mode, int value) {
  switch (mode) {
    case mode_volume:
      audio.masterVolume(map(value, 0, 100, PT2322_MIN_VOLUME, PT2322_MAX_VOLUME));
      break;
    case mode_bass:
      audio.bass(map(value, 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
      break;
    case mode_middle:
      audio.middle(map(value, 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
      break;
    case mode_treble:
      audio.treble(map(value, 0, 100, PT2322_MIN_TONE, PT2322_MAX_TONE));
      break;
    case mode_balance:
      int volume       = values[mode_volume];
      int balance      = value - 50;
      int balance_diff = (balance * volume) / 100;
      int volume_left  = volume - ((balance_diff > 0) ? balance_diff : 0);
      int volume_right = volume + ((balance_diff < 0) ? balance_diff : 0);

      volume_left = map(volume_left, 0, 100, -15, 0);
      volume_right = map(volume_right, 0, 100, -15, 0);

      audio.leftVolume(volume_left);
      audio.rightVolume(volume_right);
      break;
    case mode_mute:
      if (now_mute > 0) {
        audio.muteOn();
        digitalWrite(PIN_RELAY1, LOW);
        digitalWrite(PIN_RELAY2, LOW);        
      } else {
        audio.muteOff();
        digitalWrite(PIN_RELAY1, HIGH);
        digitalWrite(PIN_RELAY2, HIGH);
      }
      break;
  }
}

void sendPcf() {
  int ch = constrain(values[mode_channel], 0, 7);
  if (values[mode_passthru] > 0) {
    bitSet(ch, 7);
  }
  if (values[mode_mono] == 1) {
    bitSet(ch, 4);
  }
  if (values[mode_mono] == 2) {
    bitSet(ch, 5);
  }
  if (values[mode_mono] == 3) {
    bitSet(ch, 6);
  }
  pcf8574.write(ch);
}

/**
   Conver string object into signed integer value

   @param String s
   @return int
*/
int stringToInt(String s) {
  char this_char[s.length() + 1];
  s.toCharArray(this_char, sizeof(this_char));
  int result = atoi(this_char);
  return result;
}

/**
   Print mode title
   @param char* title
   @param int value
*/
void printTitle(char* title, int value) {
  lcd.setCursor(0, 0);
  lcd.print(title);
  lcd.print(F(" "));
  lcd.print(value);
  if (current_mode == mode_volume || current_mode == mode_bass || current_mode == mode_treble || current_mode == mode_middle) {
    lcd.print(F(" dB"));
  }
  if (current_mode == mode_balance) {
    lcd.print(F(" %"));
  }
  lcd.print(F("   "));
}

/**
   Print store status
*/
void printStoreStatus() {
  lcd.setCursor(COLS - 1, 0);
  if (need_store) {
    lcd.print(F("*"));
  } else {
    lcd.print(F(" ")); // nothing
  }
}

/**
   Pring a progress bar on the current cursor position

   @param int percent
*/
void printBar(int percent) {

  lcd.setCursor(0, 1);

  double lenght = COLS + 0.0;
  double value = lenght / 100 * percent;
  int num_full = 0;
  double value_half = 0.0;
  int peace = 0;
  int spaces = 0;
  int pos = 0;

  // fill full parts of progress
  if (value >= 1) {
    for (int i = 1; i < value; i++) {
      lcd.write(3);
      pos++;
      num_full = i;
    }
    value_half = value - num_full;
  } else {
    value_half = value;
  }

  // fill partial part of progress
  peace = value_half * 5;

  if (peace > 0 && peace <= 5 && pos < COLS) {
    if (peace == 1 || peace == 2) lcd.write(1);
    if (peace == 3 || peace == 4) lcd.write(2);
    if (peace == 5) lcd.write(3);
    pos++;
  }

  // fill spaces
  spaces = COLS - pos;
  for (int i = 0; i < (spaces); i++) {
    if (pos < COLS) {
      lcd.write(6);
      pos++;
    }
  }
}

void printChannelBar(int ch) {

  lcd.setCursor(0, 1);

  for (int i=0; i<8; i++) {
    if (i == ch) {
      lcd.write(2); // selected
    } else {
      lcd.write(1); // empty
    }
  }
}

/**
   Pring a progress bar on the current cursor position

   @param int percent
*/
void printBalance(int percent) {

  byte a[COLS];
  double lenght = COLS + 0.0;
  double value = (lenght / 100 * (percent - 50));
  int num_full = 0;
  double value_half = 0.0;
  int peace = 0;

  // fill all zeroes
  for (int i = 0; i < COLS; i++) {
    a[i] = 6;
  }

  // positive balance
  if (value >= 0) {

    // fill full parts of progress
    if (value >= 1) {
      for (int i = 1; i < value; i++) {
        a[COLS / 2 + i - 1] = 3;
        num_full = i;
      }
      value_half = value - num_full;
    } else {
      value_half = value;
    }

    // fill partial part of progress
    peace = value_half * 5;

    if (peace > 0 && peace <= 5) {
      int i = COLS / 2 + num_full;
      if (peace == 1 || peace == 2) a[i] = 1;
      if (peace == 3 || peace == 4) a[i] = 2;
      if (peace == 5) a[i] = 3;
    }
  }

  // negative balance
  else {

    // fill full parts of progress
    if (value <= -1) {
      for (int i = -1; i > value; i--) {
        a[COLS / 2 + i] = 3;
        num_full = i;
      }
      value_half = value - num_full;
    } else {
      value_half = value;
    }

    // fill partial part of progress
    peace = value_half * 5;

    if (peace < 0 && peace >= -5) {
      int i = COLS / 2 + num_full - 1;
      if (peace == -1 || peace == -2) a[i] = 5;
      if (peace == -3 || peace == -4) a[i] = 4;
      if (peace == -5) a[i] = 3;
    }
  }

  if (percent == 50) {
    a[COLS / 2 - 1] = 7;
    a[COLS / 2] = 8;
  }

  lcd.setCursor(0, 1);
  for (int i = 0; i < COLS; i++) {
    lcd.write(a[i]);
  }

}
