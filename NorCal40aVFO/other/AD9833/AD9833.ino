#include <LiquidCrystal.h>
#include <SPI.h>

// AD9833 set up
//  FSYNC (FNC) -> 10
//  SCLK (CLK) -> 13 (SCK)
//  SDATA (DAT) -> 11 (MOSI)
//  +Vin -> VCC
//  GND -> GND

#define FSYNC 10
//#define SCLK  13
//#define SDATA 11

// LCD set up
//  pin 4 = RS
//  pin 6 = EN
//  pins 11-14 = DB4-7
// Connect these to the Arduino pins as follows
#define LCD_RS 9
#define LCD_EN 8
#define LCD_DB4 4
#define LCD_DB5 5
#define LCD_DB6 6
#define LCD_DB7 7

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_DB4, LCD_DB5, LCD_DB6, LCD_DB7);

// Rotary encoder uses the library from https://github.com/0xPIT/encoder/tree/arduino.
// Also needs http://playground.arduino.cc/Code/Timer1
// Attach encoder pin A to 2, pin B to 3. Pin C (the middle one) goes to ground. No pullups needed.
// Attach encoder button to A0, with the other end of the button to ground.
#include <ClickEncoder.h>
#include <TimerOne.h>
#define ENCODER_A 2
#define ENCODER_B 3
#define ENCODER_BUTTON A0

// Observed step: value change with one step on the encoder.
#define ENCODER_STEP 4

#define CLOCK_FREQ 25000000UL

// Minimum and maximum frequencies.
#define MIN_FREQ 1
#define MAX_FREQ 12000000
#define INITIAL_FREQ 10000

// LCD layout
// Row 0:
// col 0: marker for freq mode
// col 1-7: freq
// col 8: space
// col 9: marker for wave form mode
// col 10-12: wave form
// col 13: space
// col 14: marker for auto mode
// col 15: auto update
#define FREQ_MARKER_COL 0
#define FREQ_COL 1
#define WAVE_MARKER_COL 9
#define WAVEFORM_COL 10
#define AUTO_MARKER_COL 14
#define AUTO_UPDATE_COL 15

// -----------------------------------------------------------------------------------------
// Find the number of decimal digits in a positive integer.
int DecimalDigits(long value) {
  if (value >= 1000000000) return 10;
  if (value >= 100000000) return 9;
  if (value >= 10000000) return 8;
  if (value >= 1000000) return 7;
  if (value >= 100000) return 6;
  if (value >= 10000) return 5;
  if (value >= 1000) return 4;
  if (value >= 100) return 3;
  if (value >= 10) return 2;
  if (value >= 1) return 1;
  return 0;  // Invalid value
}

// -----------------------------------------------------------------------------------------
// Value setting.
// The UI is this: at any time, we are modifying a particular digit of the value.
// The delta is added to this value.
// On a click, we move to the next digit, circling back to the first digit.
// To make this work, we need to decide on the range of value (number of digits).
class ValueEncoder {
  public:
    ValueEncoder(long max_value, long min_value, long initial_value);
  
    long Value() {
      return value_;
    }
    
    long Add(int delta);

    int Digit() {
      return digit_;
    }
    
    int NextDigit();
  
  private:
    long value_;
    long max_value_;
    long min_value_;
    long current_multiplier_;
    long maximum_multiplier_;
    int digit_;
    int digits_;

    void BoundValue();
};

ValueEncoder::ValueEncoder(long max_value, long min_value, long initial_value = 0)
  : max_value_(max_value), min_value_(min_value), value_(initial_value) {
    BoundValue();
    digits_ = DecimalDigits(max_value_);
    maximum_multiplier_ = 1;
    int i = digits_;
    while (--i > 0) {
      maximum_multiplier_ *= 10;
    }
    digit_ = 1;
    current_multiplier_ = 1;
}
  
long ValueEncoder::Add(int delta) {
  value_ += delta * current_multiplier_;
  BoundValue();    
  return value_; 
}

int ValueEncoder::NextDigit() {
  current_multiplier_ /= 10;
  digit_ -= 1;
  if (current_multiplier_ == 0 || current_multiplier_ < min_value_) {
    current_multiplier_ = maximum_multiplier_;
    digit_ = digits_;
  }
  return digit_;
}

void ValueEncoder::BoundValue() {
    if (value_ < min_value_) {
      value_ = min_value_;
    }
    if (value_ > max_value_) {
      value_ = max_value_;
    }
}

// -----------------------------------------------------------------------------------------
// Number setting formatter.
// Displays a number at a given position.
// The cursor is set to a specified digit. Digit 1 is the rightmost one.
// This is not a very general implementation. It only works for positive numbers and right aligns the number.
class NumberSetting {
  public:
    NumberSetting(LiquidCrystal* lcd, int x, int y, int width)
     : lcd_(lcd), x_(x), y_(y), width_(width), value_(0) {}
    
    // Set and show value.
    void Set(long value);
    
    // Get the last value that was set.
    long Value() { return value_; }

    // Show the cursor at the right place for the digit.
    void ShowCursor(int digit);
  
  private:
    LiquidCrystal* lcd_;
    int x_;
    int y_;
    int width_;
    long value_;
};

void NumberSetting::Set(long value) {
  value_ = value;
  int digits = DecimalDigits(value);
  lcd_->setCursor(x_, y_);
  for (int i = width_; i > digits; --i) {
    lcd_->print(" ");
  }
  lcd_->print(value);
}

void NumberSetting::ShowCursor(int digit) {
  lcd_->setCursor(x_ + width_ - digit, y_);
}

// -----------------------------------------------------------------------------------------
// Class for settings fields where we have several options.
// Manages changing and displaying the setting.
class EnumeratedSetting {
  public:
    // Initialize with an array of names. The setting values will be indices into this array.
    // Note that the array is NOT copied so it must exist as long as the EnumeratedSetting
    // object does.
    // Also takes the LCD object and the column and row where we display the values.
    // Initial value for the setting is set to zero.
    EnumeratedSetting(char **setting_names, int num_settings, LiquidCrystal* lcd, int x, int y)
     : setting_names_(setting_names), num_settings_(num_settings), lcd_(lcd), x_(x), y_(y),
       setting_(0) {
         ShowCurrent();
    }
    ~EnumeratedSetting() {}
    
    void ChangeSetting(bool next);
    void ShowCurrent();
    int Current() {
      return setting_;
    }
    void ChangeAndShowSetting(bool next) {
      ChangeSetting(next);
      ShowCurrent();
    }
    
  private:
    char** setting_names_;
    int num_settings_;
    LiquidCrystal* lcd_;
    int x_;
    int y_;
    int setting_;
};

void EnumeratedSetting::ChangeSetting(bool next) {
  if (next) {
    setting_ += 1;
  } else {
    setting_ -= 1;
  }
  if (setting_ >= num_settings_) {
    setting_ = 0;
  }
  if (setting_ < 0) {
    setting_ = num_settings_ - 1;
  }  
}

void EnumeratedSetting::ShowCurrent() {
  lcd_->setCursor(x_, y_);
  lcd_->print(setting_names_[setting_]);
}

// -----------------------------------------------------------------------------------------
// Mode for what we are setting.
class ModeHandler {
  public:
    enum Mode {
      Freq = 0,
      WaveForm = 1,
      Auto = 2,
      MaxMode = Auto
    };

    ModeHandler(LiquidCrystal* lcd, Mode initial_mode=Freq)
      : lcd_(lcd), mode_(initial_mode) {}
      
    Mode Current() {
      return mode_;
    }
    
    Mode Next();

    void Show();
  
    void HideMarker();

  private:
    LiquidCrystal* lcd_;
    Mode mode_;

  // Arrays indexed by mode value, and giving the associated column and row.
  static byte mode_cols[];
  static byte mode_rows[];
};

byte ModeHandler::mode_cols[] = {
  FREQ_MARKER_COL,
  WAVE_MARKER_COL,
  AUTO_MARKER_COL
};
byte ModeHandler::mode_rows[] = {
  0,
  0,
  0
};

ModeHandler::Mode ModeHandler::Next() {
  mode_ = (Mode)((int)mode_ + 1);
  if (mode_ > MaxMode) {
    mode_ = Freq;
  }
  return mode_;
}

void ModeHandler::Show() {
  for (byte i = 0; i <= MaxMode; ++i) {
    lcd_->setCursor(mode_cols[i], mode_rows[i]);
    lcd_->print(" ");
  }
  lcd_->setCursor(mode_cols[mode_], mode_rows[mode_]);
  lcd_->write('\x7e');
  if (mode_ == Freq) {
    lcd_->cursor();
  } else {
    lcd_->noCursor();
  }
}

void ModeHandler::HideMarker() {
  lcd_->setCursor(mode_cols[mode_], mode_rows[mode_]);
  lcd_->print(" ");
  lcd_->noCursor();
}

namespace {
// -----------------------------------------------------------------------------------------
enum WaveForm {
  Sine = 0,
  Triangle = 1,
  Square = 2
};
char *wave_form_names[] = {"Sin", "Tri", "Squ"};

// -----------------------------------------------------------------------------------------
// Low level AD9833 control.
// Generate value for control register, assuming that frequencies will be sent as 28 bits.
// Note this doesn't cover all the things that could be in the control register, for example
// it excludes SLEEP modes.
unsigned int ControlRegister(
  bool freq1,      // True for FREQ1, False for FREQ0
  bool phase1,     // True for PHASE1, False for PHASE0
  WaveForm mode) {
  unsigned int word = 0x2100;  // Control register, 28 bit frequencies, Reset
  if (freq1) word |= 0x0800;
  if (phase1) word |= 0x0400;
  switch (mode) {
    case Sine:
      break;
    case Triangle:
      word |= 0x0002;
      break;
    case Square:
      word |= 0x0020;
      break;
#if 0
    // We don't implement half frequency square wave in this version.
    case SquareHalfFreq:
      word |= 0x0028;
      break;
#endif
  }
  return word;
}

// Generate high and low words for frequency register.
void FrequencyRegisters(bool freq1, double freq, unsigned long clock_freq,
                        unsigned int *high, unsigned int* low) {
  *high = *low = (freq1) ? 0x8000 : 0x4000;
  
  // f = freqreq * fclk/2^28
  unsigned long freqreg = (unsigned long)(0.5 + (freq * 0x10000000) / clock_freq);
  *high |= (freqreg >> 14);
  *low |= (freqreg & 0x3fff);
}

// Generate phase register word.
unsigned int PhaseRegister(bool phase1, unsigned int phase_degrees) {
  unsigned int data = (phase1) ? 0xD000 : 0xC000;
  // phase shift in degrees = 360/4096 * phasereq
  unsigned int phasereg = (unsigned int)(phase_degrees * 4096UL / 360UL);
  data |= (phasereg & 0x3fff);
  return data;
}

// Write a 16 bit word.
void WriteWord(unsigned int data)
{
  digitalWrite(FSYNC, LOW);
  delay(10);
  SPI.transfer(highByte(data));
  SPI.transfer(lowByte(data));
  delay(10);
  digitalWrite(FSYNC, HIGH);
  Serial.println(data, HEX);
}

// -----------------------------------------------------------------------------------------
// High level AD9833 control.

// control1 = false sets freq and phase 0.
// control2 = false sets freq and phase 1.
void SetFrequencyAndPhase(double freq, unsigned int phase, WaveForm waveform, bool control1) {
  // Use safe values if we are given bad arguments.
  if (freq < 0) {
    freq = 100;
  }
  if (phase >= 360) {
    phase = 0;
  }

  unsigned int control = ControlRegister(control1, control1, waveform);
  WriteWord(control);
  unsigned int low, high;
  FrequencyRegisters(control1, freq, CLOCK_FREQ, &high, &low);
  WriteWord(low);
  WriteWord(high);
  WriteWord(PhaseRegister(control1, phase));
  WriteWord(control & 0xfeff);
}

// Class for managing the AD9833.
// Tracks the last settings and provides a function to change them.
class GeneratorController {
  public:
    GeneratorController()
      : freq_(0), phase_(0), waveform_(Sine), control1_(false) {}
    ~GeneratorController() {}
  
    void Update(double freq, unsigned int phase, WaveForm waveform, bool control1);
  
  private:
    // Last values supplied to Update.
    double freq_;
    unsigned int phase_;
    WaveForm waveform_;
    
    // TODO consider having two sets of the above params, for control1 and control2
    bool control1_;
};

void GeneratorController::Update(double freq, unsigned int phase, WaveForm waveform, bool control1) {
  if (freq != freq_ || phase != phase_ || waveform != waveform_ || control1 != control1_) {
    SetFrequencyAndPhase(freq, phase, waveform, control1);
    freq_ = freq;
    phase_ = phase;
    waveform_ = waveform;
    control1_ = control1;
  }
}
} // namespace

// -----------------------------------------------------------------------------------------
// Auto/manual setting.
// In auto mode, frequency changes take place immediately.
// In manual mode, you must click on the 'M' indicator.
#define AUTO_UPDATE 0
#define MANUAL_UPDATE 1
char *auto_update_names[] = {"A", "M"};

// -----------------------------------------------------------------------------------------
ValueEncoder value_encoder(MAX_FREQ, MIN_FREQ, INITIAL_FREQ);
NumberSetting freq_setting(&lcd, FREQ_COL, 0, DecimalDigits(MAX_FREQ));
ClickEncoder *click_encoder;
ModeHandler setting_mode(&lcd);
EnumeratedSetting wave_form(wave_form_names, sizeof(wave_form_names)/sizeof(char*), &lcd, WAVEFORM_COL, 0);
EnumeratedSetting auto_update(auto_update_names, sizeof(auto_update_names)/sizeof(char*), &lcd, AUTO_UPDATE_COL, 0);
GeneratorController controller;

void timerIsr() {
  click_encoder->service();
}

void UpdateController() {
  controller.Update(
    freq_setting.Value(),
    0 /* Phase */,
    (WaveForm)wave_form.Current(),
    false /* control 0 */);
}

void UpdateControllerIfAuto() {
  if (auto_update.Current() == AUTO_UPDATE) {
    UpdateController();
  }
}

void setup() {
  // SPI interface to AD9833 initialization
  pinMode(FSYNC, OUTPUT);
  digitalWrite(FSYNC, HIGH);
  SPI.setDataMode(SPI_MODE2); 
  SPI.begin();
  delay(100);
  
  // Encoder and timer initialization
  click_encoder = new ClickEncoder(ENCODER_B, ENCODER_A, ENCODER_BUTTON, ENCODER_STEP);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);

  lcd.begin(16, 2);
  freq_setting.Set(value_encoder.Value());
  wave_form.ShowCurrent();
  auto_update.ShowCurrent();
  setting_mode.Show();
  if (setting_mode.Current() == ModeHandler::Freq) {
    freq_setting.ShowCursor(value_encoder.Digit());
  }
}

void loop() {
  ClickEncoder::Button b = click_encoder->getButton();
  if (b == ClickEncoder::Held) {
    // In the middle of a long click, hide the mode marker.
    setting_mode.HideMarker();
    return;
  }
  
  long encoder_value = click_encoder->getValue();
  if (b == ClickEncoder::Released) {
    setting_mode.Next();
    setting_mode.Show();
    if (setting_mode.Current() == ModeHandler::Freq) {
      freq_setting.ShowCursor(value_encoder.Digit());
    }
    return;
  }

  switch (setting_mode.Current()) {
    case ModeHandler::Freq: {
      bool changed = false;
      if (encoder_value != 0) {
        value_encoder.Add(encoder_value);
        changed = true;
      }
      if (b == ClickEncoder::Clicked) {
        value_encoder.NextDigit();
        changed = true;
      }
  
      if (changed) {
        freq_setting.Set(value_encoder.Value());
        UpdateControllerIfAuto();
      }
      freq_setting.ShowCursor(value_encoder.Digit());
      return;
    }
    
    case ModeHandler::WaveForm:
      if (encoder_value != 0) {
        wave_form.ChangeAndShowSetting(encoder_value > 0);
        UpdateControllerIfAuto();
      }
      return;

    case ModeHandler::Auto:
      if (encoder_value != 0) {
        auto_update.ChangeAndShowSetting(encoder_value > 0);
        UpdateControllerIfAuto();
      }
      if (b == ClickEncoder::Clicked) {
        UpdateController();
      }
      return;
  }      
}

