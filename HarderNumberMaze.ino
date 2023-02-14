/* Harder Number Maze Game - see http://www.technoblogy.com/show?48BN

   David Johnson-Davies - www.technoblogy.com - 14th February 2023
   AVR128DA32 or ATmega4808 @ 4 MHz (internal oscillator; BOD disabled)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/sleep.h>
#include <string.h>

#if defined(__AVR_ATmegax08__)
#define TCB_CLKSEL_DIV1_gc TCB_CLKSEL_CLKDIV1_gc
#endif

// Seven segment displays

// Cathodes, Arduino pin numbers
uint8_t cathode[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 20, 21, 22, 23 };

// Translation tables
char symbol[]     = { '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',
                      '=',  '^',  'v',  ' ',  'A',  'E',  'H',  'L',  'n',  'o',
                      'P',  'Y'  };
uint8_t segment[] = { 0x7e, 0x30, 0x6d, 0x79, 0x33, 0x5b, 0x5f, 0x70, 0x7f, 0x7b,
                      0x09, 0x62, 0x1c, 0x00, 0x77, 0x4f, 0x37, 0x0e, 0x15, 0x1d,
                      0x67, 0x3b };
uint8_t value[]   = { 0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
                      64+0, 64+1, 64-1, 0,    0,    0,    0,    0,    0,    0,
                      0,    0 };
                      
// Mazes
const int TotalMazes = 36;

char mazes[TotalMazes][17] = {
  // Uses 12
  "222122221222222H", // 6 6
  "1111222121H22222", // 6 7
  "111111112212122H", // 7 6
  "221121112222111H", // 8 9b
  // Uses 123
  "221222132222231H", // 7 6
  "1313213221H22332", // 10 8
  "3131221212H33133", // 10 9
  "223121212121112H", // 11 8
  // Uses 0123
  "201111232221212H", // 7 8
  "121213122102112H", // 7 9
  "222231131021112H", // 10 10
  "212331013313132H", // 11 8
  // Adds =
  "2121221111221=2H", // 5 7
  "121121=31=13222H", // 7 8 
  "2=3313=1=1=0002H", // 11 13
  "1=23331310=2==2H", // 12 14
  // Uses 012^
  "111^11111112112H", // 6 6
  "1111021111101^2H", // 7 9
  "2112^22^^1H^12^1", // 8 10
  "21^^^1^1222^112H", // 13 13
  // Uses ^v
  "^^^^^^^^v^Hvvv^v", // 7 8
  "^^vvvv^v^vv^^vvH", // 7 10
  "^vvv^v^^^^H^^^^v", // 12 11
  "^vv^^vvvvv^^vv^H", // 12 12
  // Uses ^v=
  "^^vv=======^==^H", // 6 7
  "^vv^=vvvvv=^v=^H", // 14 16
  "^vvv^v^v^vH^^===", // 15 16
  "^v=^=v==^^^^v=^H", // 15 17
  // Uses 3^v=
  "3^vvv^=^==^^v3=H", // 9 12
  "3^=vvv^^=^H^=^=v", // 12 10  
  "3==vv=vvv=^^^v=H", // 13 14  
  "3v=^v=vvv=v^v=^H", // 15 17
  // Uses all symbols
  "2v111^v1^2H^221^", // 12 14
  "^1vvvv^^3^H=^v==", // 15 17 
  "2vv=1=v1=^=22==H", // 15 18
  "3vv1^^=vv2=0133H", // 18 18
};

// Intro screen
char intro[17] = { "    PLAYno 1    " };

// Globals
uint8_t Maze = 0;
char *Buffer = intro;
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

void NextLevel (uint8_t press) {
  if (press-12 == 1) Maze = 1;                             // Left button resets to 1                  
  else Maze = (Maze + 1) % TotalMazes;
  Buffer = intro;  
  Buffer[11] = (Maze+1)%10 + '0';
  int tens = (Maze+1)/10;
  if (tens == 0) Buffer[10] = ' '; else Buffer[10] = tens + '0';
  Player = 16;                                             // Go to Intro screen
} 

int Jump (uint8_t jumpsize, int direction) {
  char cell = mazes[Maze][Player];
  int jump = value[strchr(symbol, cell) - symbol];
  if (jump >= 63) jump = jumpsize + (jump - 64)*direction;
  return jump;
}

void HandleButtons (uint8_t digit) {
  static uint8_t press = 0, last = 0, count = 0, move = 0, jumpsize = 0;
  static uint8_t undo[UndoBuffer];
  if (digit >= 12 && digitalRead(24) == 0) { 
    press = digit;
    count++;
  }
  if (digit != 15) return;
  if (count > 100) {                                       // Long press 1 second
    // Long press = undo
    if (move > 0) {
      move--;
      Player = undo[move];
      jumpsize = Jump(jumpsize, -1);
      Sound = Sound | 1<<UNDO;
    }
    count = 0;
  } else if (press != 0 && press != last) {                // Debounced keypress
    if (Player == 16) {
      // Start new maze
      Player = 0; Buffer = mazes[Maze]; 
      move = 0; jumpsize = 0;
    } else if (mazes[Maze][Player] == 'H') {               // Player on Home
      // Proceed after finishing a level
      NextLevel(press);
    } else {
      // Move player
      int dir = press-12; // Up = 0, Left = 1, Down = 2, Right = 3
      int jump = Jump(jumpsize, +1);
      int mex = Player & 0x03, mey = Player>>2;
      int nex = mex + (dir%2) * (dir-2) * jump;
      int ney = mey + (1-dir%2) * (dir-1) * jump;
      if (nex != mex && nex >= 0 && nex <= 3) {            // Move horizontally
        mex = nex; Sound = Sound | 1<<HORIZONTAL;
        undo[move] = Player; jumpsize = jump;
        if (move < UndoBuffer-1) move++;
      } else if (ney != mey && ney >= 0 && ney <= 3) {     // Move vertically
        mey = ney; Sound = Sound | 1<<VERTICAL;
        undo[move] = Player; jumpsize = jump;
        if (move < UndoBuffer-1) move++;
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
  
void DisplayNextDigit() {
  static uint8_t digit = 0;
  pinMode(cathode[digit], INPUT);
  digit = (digit + 1) & 0xF;
  uint8_t bits = segment[strchr(symbol, Buffer[digit]) - symbol];
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
    Sound = 0; 
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
