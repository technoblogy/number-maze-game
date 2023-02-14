/* Number Maze Game - see http://www.technoblogy.com/show?45JT

   David Johnson-Davies - www.technoblogy.com - 21st December 2022
   AVR128DA32 or ATmega4808 @ 4 MHz (internal oscillator; BOD disabled)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/sleep.h>

#if defined(__AVR_ATmegax08__)
#define TCB_CLKSEL_DIV1_gc TCB_CLKSEL_CLKDIV1_gc
#endif

// Seven segment displays

// Cathodes, Arduino pin numbers
uint8_t cathode[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 20, 21, 22, 23 };

// Translation tables
uint8_t symbol[]  = { '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  
                      ' ',  'H',  'P', 'L',  'A',  'Y',   'n',  'o',  0xff };
uint8_t segment[] = { 0x7e, 0x30, 0x6d, 0x79, 0x33, 0x5b, 0x5f, 0x70, 0x7f, 0x7b,
                      0x00, 0x37, 0x67, 0x0e, 0x77, 0x3b, 0x15, 0x1d, 0xff };

// Mazes
const int TotalMazes = 12;

uint8_t mazes[TotalMazes][17] = {
  "201111232221212H", // 7 8+5
  "3212122112H21213", // 7 7+4
  "121213122132213H", // 8 9+3
  "221121112222111H", // 8 9b+3
  "131212131131232H", // 9 9+2
  "112213123122112H", // 9 10+4
  "1313213221H22332", // 10 8+3
  "231111333113332H", // 10 9+0
  "3131221212H33133", // 10 9+2
  "222231131321112H", // 10 10+1
  "223121212121112H", // 11 8+3
  "212331013310132H"  // 11 8b+0
};

// Number of steps in each maze
uint8_t steps[TotalMazes] = { 7, 7, 8, 8, 9, 9, 10, 10, 10, 10, 11, 11 }; 

// Intro screen
uint8_t intro[17] = { "    PLAYno 1    " };

// Globals
int Maze = 0;
uint8_t *Buffer = intro;
volatile uint8_t Sound = 0;
enum sounds { NONE, HORIZONTAL, VERTICAL, UNDO, SOLVED };
volatile uint8_t Player = 16;

// Buttons **********************************************

void SetupButtons () {
  PORTF.PIN4CTRL = PORT_PULLUPEN_bm;                       // PF4 input pullup
}

ISR(PORTF_PORT_vect) {
  PORTF.INTFLAGS = PIN4_bm;                                // Clear the interrupt flag
}

// Display multiplexer **********************************************

const int UndoBuffer = 100;
const unsigned long Timeout = 60000;                       // 1 minute
volatile unsigned long Start;

void SetupDisplay () {
  // Cathodes input low
  PORTA.DIRCLR = 0xff; PORTA.OUTCLR = 0xff;                // PA0 to PA7
  PORTC.DIRCLR = 0x0f; PORTC.OUTCLR = 0x0f;                // PC0 to PC3
  PORTF.DIRCLR = 0x0f; PORTF.OUTCLR = 0x0f;                // PF0 to PF3
  // Anodes output low
  PORTD.DIRSET = 0xff; PORTD.OUTCLR = 0xff;                // PD0 to PD7
  // Set up Timer/Counter TCB to multiplex the display
  TCB0.CCMP = 2499;                                        // 4MHz/2500 = 1600Hz
  TCB0.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_ENABLE_bm;         // Divide timer by 1
  TCB0.CTRLB = 0;                                          // Periodic Interrupt
  TCB0.INTCTRL = TCB_CAPT_bm;                              // Enable interrupt
}

// Timer/Counter TCB interrupt - multiplexes the display
ISR(TCB0_INT_vect) {
  TCB0.INTFLAGS = TCB_CAPT_bm;                             // Clear the interrupt flag
  DisplayNextDigit();
}

uint8_t Segments (uint8_t c) {
  int p = 0;
  while (symbol[p] != 0xff) {
    if (symbol[p] == c) return segment[p];
    p++;
  }
  return 0;
}

void NextLevel (uint8_t press) {
  if (press - 12 == 1) Maze = 0;                           // Left button resets to 1
  else Maze = (Maze + 1) % TotalMazes;                     // Wrap around
  Buffer = intro;
  Buffer[11] = (Maze+1)%10 + '0';
  int tens = (Maze+1)/10;
  if (tens == 0) Buffer[10] = ' '; else Buffer[10] = tens + '0';
  Player = 16;                                             // Go to Intro screen
}

void HandleButtons (uint8_t digit) {
  static uint8_t press = 0, last = 0, count = 0, move = 0;
  static uint8_t undo[UndoBuffer];
  if (digit >= 12 && digitalRead(24) == 0) { 
    press = digit;
    count++;
  }
  if (digit == 15) {
    if (count > 100) {                                     // Long press 1 second
      // Long press = undo
      if (move > 0) {
        move--;
        Player = undo[move];
        Sound = Sound | 1<<UNDO;
      }
      count = 0;
    } else if (press != 0 && press != last) {
      if (Player == 16) {                                  // Intro screen
        // Start new maze
        Player = 0; Buffer = mazes[Maze]; move = 0;
      } else if (mazes[Maze][Player] == 'H') {             // Player on Home
        // Proceed after finishing a level
        NextLevel(press);
      } else {
        // Move player
        int dir = press-12; // Up = 0, Left = 1, Down = 2, Right = 3
        int cell = mazes[Maze][Player];
        int mex = Player & 0x03, mey = Player>>2;
        int nex = mex + (dir%2) * (dir-2) * (cell-'0');
        int ney = mey + (1-dir%2) * (dir-1) * (cell-'0');
        if (nex != mex && nex >= 0 && nex <= 3) {          // Move horizontally
          mex = nex; Sound = Sound | 1<<HORIZONTAL;
          undo[move] = Player; if (move < UndoBuffer-1) move++;
        } else if (ney != mey && ney >= 0 && ney <= 3) {   // Move vertically
          mey = ney; Sound = Sound | 1<<VERTICAL;
          undo[move] = Player; if (move < UndoBuffer-1) move++;
        }
        Player = mey<<2 | mex;
        // Moved onto Home
        if (mazes[Maze][Player] == 'H') Sound = Sound | 1<<SOLVED;
        count = 0;
      }
    }
    last = press;
    press = 0;
  }
}
  
void DisplayNextDigit() {
  static uint8_t digit = 0;
  pinMode(cathode[digit], INPUT);
  digit = (digit + 1) & 0xF;
  uint8_t bits = Segments(Buffer[digit]);
  if (digit == Player) bits = bits + 0x80;                 // Decimal point
  PORTD.OUT = bits;
  pinMode(cathode[digit], OUTPUT);
  HandleButtons(digit);
}

// Play notes **********************************************

uint8_t scale[] = {239,226,213,201,190,179,169,160,151,142,134,127};

// Note on WO5
void SetupNote () {
  PORTF.DIRSET = PIN5_bm;                                  // PF5 as output
  PORTMUX.TCAROUTEA = PORTMUX_TCA0_PORTF_gc;               // PWM on port F
  TCA0.SINGLE.CTRLD = TCA_SINGLE_SPLITM_bm;
  TCA0.SPLIT.CTRLA = TCA_SPLIT_ENABLE_bm | 5<<TCA_SPLIT_CLKSEL_gp; // DIV64
  TCA0.SPLIT.CTRLB = TCA_SPLIT_HCMP2EN_bm;                 // Note on WO5 = PF5
  TCA0.SPLIT.HCMP2 = 0;
}

void PlayNote (int note, int millis) {
  TCA0.SPLIT.HPER = scale[note];
  TCA0.SPLIT.HCMP2 = scale[note]>>1;
  delay(millis);
  TCA0.SPLIT.HCMP2 = 0;
}

boolean PlaySound (int sound) {
  if (Sound & 1<<sound) {
    Sound = Sound & ~(1<<sound);
    Start = millis();
    return true;
  } else return false;
}

// Setup **********************************************

void setup () {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  SetupDisplay();
  SetupButtons();
  SetupNote();
  ADC0.CTRLA = 0; // Disable ADC to save power
  Start = millis();
}

// Play sounds
void loop () {
  if (PlaySound(HORIZONTAL)) PlayNote(9,200);
  if (PlaySound(VERTICAL)) PlayNote(11, 200);
  if (PlaySound(UNDO)) PlayNote(7, 200);
  if (PlaySound(SOLVED)) {
    delay(250); PlayNote(4, 250); PlayNote(5, 250);
    PlayNote(7, 250); PlayNote(9, 250); PlayNote(11, 750); 
  }
  // Go to sleep?
  if ((unsigned long)(millis() - Start) > Timeout) {
    TCB0.CTRLA = 0;                                        // Turn off display multiplexing
    PORTA.DIRSET = 0xff;                                   // Display cathodes to outputs
    PORTC.DIRSET = 0x0f;
    PORTF.DIRSET = 0x0f;
    PORTD.OUTCLR = 0xff;                                   // Display anodes output low
    PORTF.PIN4CTRL = PORT_PULLUPEN_bm | PORT_ISC_LEVEL_gc; // PF4 input pullup with interrupt
    sleep_enable();
    sleep_cpu();
    PORTF.PIN4CTRL = PORT_PULLUPEN_bm;                     // Turn off PF4 interrupt
    while (digitalRead(24) == 0);                          // Wait for key released
    PORTA.DIRCLR = 0xff;                                   // Display cathodes back to inputs
    PORTC.DIRCLR = 0x0f;
    PORTF.DIRCLR = 0x0f;
    TCB0.CTRLA = TCB_CLKSEL_DIV1_gc | TCB_ENABLE_bm;       // Turn on display multiplexing
    Start = millis();
  }
}
