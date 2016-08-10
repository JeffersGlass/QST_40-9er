/*
  The heart of the code that manages the AD9850 was written by
  Richard Visokey AD7C - www.ad7c.com

 Modifications were made by Jeff Glass, Jack Purdum and Dennis Kidder:
   Rev 4.00:  Feb.  2, 2015
   Rev 5.00:  July  8, 2015, Jack Purdum
   Rev.6.00:  Aug.  1, 2015, Jack Purdum
	 Rev.6.10:  Feb. 27, 2016, Jack Purdum, decreased the number of elements in the increment array
   Rev 7.1:   August 10, 2016, Jeff Glass
*/

#include <Rotary.h>   // From Brian Low: https://github.com/brianlow/Rotary
#include <EEPROM.h>   // Shipped with IDE
#include <Wire.h>     //        

const int morseElementLength = 100;
String numbers[10];

#define MYTUNINGCONSTANT     34.35910479585483    // Replace with your calculated TUNING CONSTANT. See article

#define SPACES      "                "
#define HERTZ       "Hz"
#define KILOHERTZ   "kHz"
#define MEGAHERTZ   "mHz"

#define GENERAL     2
#define TECH        1
#define EXTRA       0

#define ELEMENTCOUNT(x) (sizeof(x) / sizeof(x[0]))  // A macro that calculates the number
// of elements in an array.
#define pulseHigh(pin) {digitalWrite(pin, HIGH); digitalWrite(pin, LOW); } // Write macro

//Setup some VFO band edges
#define VFOUPPERFREQUENCYLIMIT  7300000L            // Upper band edge
#define VFOLOWERFREQUENCYLIMIT  7000000L            // Lower band edge
#define VFOLOWALLBUTEXTRA       7025000L            // Frequency of Extra licensees only
#define VFOHIGHTECH             7125000L            // Hi Tech cutoff
#define VFOLOWTECH              7100000L            // Low Tech cutoff
#define VFOGENERALLOWGAP        7175000L            // General class edge

#define W_CLK             8               // Pin  8 - connect to AD9850 module word load clock pin (CLK)
#define FQ_UD             9               // Pin  9 - connect to freq update pin (FQ)
#define DATA             10               // Pin 10 - connect to serial data load pin (DATA)
#define RESET            11               // Pin 11 - connect to reset pin (RST) 

#define LCDCOLS          16               // LCD stuff
#define LCDROWS           2
#define SPLASHDELAY    4000               // Hold splash screen for 4 seconds

#define FREQPIN           4               //Used by button for frequency readout
#define ROTARYSWITCHPIN   7               // Used by switch for rotary encoder
#define RITPIN            8               // Used by push button switch to toggle RIT
#define RXTXPIN          12               // When HIGH, the xcvr is in TX mode
#define RITOFFSETSTART  700L              // Offset for RIT

#define FREQINBAND        0               // Used with range checking
#define FREQOUTOFBAND     1

// ===================================== EEPROM Offsets and data ==============================================
#define READEEPROMFREQ        0       // The EEPROM record address of the last written frequency
#define READEEPROMINCRE       1       // The EEPROM record address of last frequency increment

#define DELTAFREQOFFSET      25       // Must move frequency more than this to call it a frequency change
#define DELTATIMEOFFSET   60000       // If not change in frequency within 1 minute, update the frequency

unsigned long markFrequency;          // The frequency just written to EEPROM
long eepromStartTime;                 // Set when powered up and while tuning
long eepromCurrentTime;               // The current time reading

// ============================ ISR variables: ======================================
volatile int_fast32_t currentFrequency;     // Starting frequency of VFO
volatile long currentFrequencyIncrement;
volatile long ritOffset;

// ============================ General Global Variables ============================
bool ritState;                    // Receiver incremental tuning state HIGH, LOW
bool oldRitState;

bool freqState;
bool oldFreqState;

char temp[17];

int ritDisplaySwitch;             // variable to index into increment arrays (see below)
int incrementIndex = 0;

long oldRitOffset;
int_fast32_t oldFrequency = 1;    // variable to hold the updated frequency

static char *bandWarnings[]     = {"Extra  ", "Tech   ", "General"};
static int whichLicense;
static char *incrementStrings[] = {"1000", "100", "10"};     // These two allign
static  long incrementTable[]   = { 1000,   100,   10};
static  long memory[]           = {VFOLOWERFREQUENCYLIMIT, VFOUPPERFREQUENCYLIMIT};

Rotary r = Rotary(2, 3);       // Create encoder object and set the pins the rotary encoder uses.  Must be interrupt pins.
void setup() {

  // ===================== Set up from EEPROM memory ======================================

  currentFrequency = readEEPROMRecord(READEEPROMFREQ);            // Last frequency read while tuning
  if (currentFrequency < 7000000L || currentFrequency > 7300000L) // New EEPROM usually initialized with 0xFF
    currentFrequency = 7030000L;                                  // Default QRP freq if no EEPROM recorded yet
  markFrequency = currentFrequency;                               // Save EEPROM freq.


  incrementIndex = (int) readEEPROMRecord(READEEPROMINCRE);       // Saved increment as written to EEPROM
  if (incrementIndex < 0 || incrementIndex > 9)                   // If none stored in EEPROM yet...
    incrementIndex = 0;                                           // ...set to 10Hz
  currentFrequencyIncrement = incrementTable[incrementIndex];     // Store working freq variables
  markFrequency = currentFrequency;
  eepromStartTime = millis();               // Need to keep track of EEPROM update time

  pinMode(13, OUTPUT);

  pinMode(FREQPIN, INPUT);
  digitalWrite(FREQPIN, INPUT_PULLUP);
  pinMode(ROTARYSWITCHPIN, INPUT);
  digitalWrite(ROTARYSWITCHPIN, INPUT_PULLUP);
  pinMode(RITPIN, INPUT);
  digitalWrite(RITPIN, INPUT_PULLUP);
  pinMode(RXTXPIN, LOW);              // Start in RX mode

  oldRitState = ritState = LOW;       // Receiver incremental tuning state HIGH, LOW
  ritOffset = RITOFFSETSTART;         // Default RIT offset
  ritDisplaySwitch = 0;

  PCICR |= (1 << PCIE2);              // Interrupt service code
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();
  pinMode(FQ_UD, OUTPUT);             // Tied to AD9850 board
  pinMode(W_CLK, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(RESET, OUTPUT);

  pulseHigh(RESET);
  pulseHigh(W_CLK);
  pulseHigh(FQ_UD);  // this pulse enables serial mode on the AD9850 - Datasheet page 12.`

  //Serial.begin(9600);

  f
  sendFrequency(currentFrequency);

  numbers[0] = "-----";
  numbers[1] = ".----";
  numbers[2] = "..---";
  numbers[3] = "...--";
  numbers[4] = "....-";
  numbers[5] = ".....";
  numbers[6] = "-....";
  numbers[7] = "--...";
  numbers[8] = "---..";
  numbers[9] = "----.";
}

void loop() {
  static int state = 1;      // 1 because of pull-ups on encoder switch
  static int oldState = 1;

  int flag;

  state = digitalRead(ROTARYSWITCHPIN);    // See if they pressed encoder switch
  ritState = digitalRead(RITPIN);          // Also check RIT button
  freqState = digitalRead(FREQPIN);

  if (freqState != oldFreqState && freqState == LOW){
    CWFrequency();
  }
  oldFreqState = freqState;

  if (state != oldState) {                 // Only if it's been changed...
    if (state == 1) {                      // Only adjust increment when HIGH
      if (incrementIndex < ELEMENTCOUNT(incrementTable) - 1) {    // Adjust the increment size
        incrementIndex++;
      } else {
        incrementIndex = 0;                // Wrap around to zero
      }
      currentFrequencyIncrement = incrementTable[incrementIndex];
      for (int i = ELEMENTCOUNT(incrementTable); i > incrementIndex; i--){
        digitalWrite(13, HIGH);
        delay(50);
        digitalWrite(13, LOW);
        delay(70);
      }
      //Serial.println(incrementTable[incrementIndex]);
    }
    oldState = state;
  }

  if (currentFrequency != oldFrequency) { // Are we still looking at the same frequency?
  


    sendFrequency(currentFrequency);      // Send frequency to chip
    oldFrequency = currentFrequency;
  }

  eepromCurrentTime = millis();
  // Only update EEPROM if necessary...both time and currently stored freq.
  if (eepromCurrentTime - eepromStartTime > DELTATIMEOFFSET && markFrequency != currentFrequency) {
    writeEEPROMRecord(currentFrequency, READEEPROMFREQ);                  // Update freq
    writeEEPROMRecord((unsigned long) incrementIndex, READEEPROMINCRE);   // Update increment
    eepromStartTime = millis();
    markFrequency = currentFrequency;                                     // Update EEPROM freq.
  }

  if (ritState == HIGH) {      // Change RIT?
    //DoRitDisplay();
    ritDisplaySwitch = 1;
  }
  if (oldRitState != ritState) {
    oldRitState = ritState;
    ritDisplaySwitch = 0;
  }
}


// Original interrupt service routine, as modified by Jack
ISR(PCINT2_vect) {
  unsigned char result = r.process();

  //Serial.println(currentFrequency);

  switch (result) {
    case 0:                                          // Nothing done...
      return;

    case DIR_CW:                                     // Turning Clockwise, going to higher frequencies
      if (ritState == LOW) {
        currentFrequency += currentFrequencyIncrement;
      } else {
        ritOffset += RITOFFSETSTART;
      }
      break;

    case DIR_CCW:                                    // Turning Counter-Clockwise, going to lower frequencies
      if (ritState == LOW) {
        currentFrequency -= currentFrequencyIncrement;
      } else {
        ritOffset -= RITOFFSETSTART;
      }
      break;

    default:                                          // Should never be here
      break;
  }
  if (currentFrequency >= VFOUPPERFREQUENCYLIMIT) {   // Upper band edge?
    currentFrequency = oldFrequency;
  }
  if (currentFrequency <= VFOLOWERFREQUENCYLIMIT) {   // Lower band edge?
    currentFrequency = oldFrequency;
  }
}


void sendFrequency(int32_t frequency) {
  /*
  Formula: int32_t adjustedFreq = frequency * 4294967295/125000000;

  Note the 125 MHz clock on 9850.  You can make 'slight' tuning
  variations here by adjusting the clock frequency. The constants
  factor to 34.359
  */
  int32_t freq = (int32_t) (((float) frequency * MYTUNINGCONSTANT));  // Redefine your constant if needed

  for (int b = 0; b < 4; b++, freq >>= 8) {
    tfr_byte(freq & 0xFF);
  }
  tfr_byte(0x000);   // Final control byte, all 0 for 9850 chip
  pulseHigh(FQ_UD);  // Done!  Should see output
}

// transfers a byte, a bit at a time, LSB first to the 9850 via serial DATA line
void tfr_byte(byte data)
{
  for (int i = 0; i < 8; i++, data >>= 1) {
    digitalWrite(DATA, data & 0x01);
    pulseHigh(W_CLK);   //after each bit sent, CLK is pulsed high
  }
}

/*****
  This method is used to read a record from EEPROM. Each record is 4 bytes (sizeof(unsigned long)) and
  is used to calculate where to read from EEPROM.

  Argument list:
    int record                the record to be read. While tuning, it is record 0

  Return value:
    unsigned long            the value of the record,

  CAUTION:  Record 0 is the current frequency while tuning, etc. Record 1 is the number of stored
            frequencies the user has set. Therefore, the stored frequencies list starts with record 23.
*****/
unsigned long readEEPROMRecord(int record)
{
  int offset;
  union {
    byte array[4];
    unsigned long val;
  } myUnion;

  offset = record * sizeof(unsigned long);

  myUnion.array[0] = EEPROM.read(offset);
  myUnion.array[1] = EEPROM.read(offset + 1);
  myUnion.array[2] = EEPROM.read(offset + 2);
  myUnion.array[3] = EEPROM.read(offset + 3);

  return myUnion.val;
}

/*****
  This method is used to test and perhaps write the latest frequency to EEPROM. This routine is called
  every DELTATIMEOFFSET (default = 10 seconds). The method tests to see if the current frequency is the
  same as the last frequency (markFrequency). If they are the same, +/- DELTAFREQOFFSET (drift?), no
  write to EEPROM takes place. If the change was larger than DELTAFREQOFFSET, the new frequency is
  written to EEPROM. This is done because EEPROM can only be written/erased 100K times before it gets
  flaky.

  Argument list:
    unsigned long freq        the current frequency of VFO
    int record                the record to be written. While tuning, it is record 0

  Return value:
    void
*****/
void writeEEPROMRecord(unsigned long freq, int record)
{
  int offset;
  union {
    byte array[4];
    unsigned long val;
  } myUnion;

  if (abs(markFrequency - freq) < DELTAFREQOFFSET) {  // Is the new frequency more or less the same as the one last written?
    return;                                           // the same as the one last written? If so, go home.
  }
  myUnion.val = freq;
  offset = record * sizeof(unsigned long);

  EEPROM.write(offset, myUnion.array[0]);
  EEPROM.write(offset + 1, myUnion.array[1]);
  EEPROM.write(offset + 2, myUnion.array[2]);
  EEPROM.write(offset + 3, myUnion.array[3]);
  markFrequency = freq;                               // Save the value just written
}

/*****
  This method is used to see if the current frequency displayed on line 1 is within a ham band.
  The code does not allow you to use the band edge.

  Argument list:
    void

  Return value:
    int            0 (FREQINBAND) if within a ham band, 1 (FREQOUTOFBAND) if not
*****/


/*****
  This method is used to change the number of hertz associated with one click of the rotary encoder. Ten step
  levels are provided and increasing or decreasing beyonds the array limits rolls around to the start/end
  of the array.

  Argument list:
    void

  Return value:
    void
*****/

void CWFrequency(){
  long workingFrequency = currentFrequency - 7000000;
  //Serial.println(workingFrequency);
  for (int i = 5; i >=3; i--){
   long toShow = workingFrequency/pow(10, i);
   int toShowDigit = int(toShow + 0.5);
   toShowDigit = toShowDigit % 10;
   showMorseWord(numbers[toShowDigit]);
   showIntracharacter();
  }
}

void showMorseWord(String singleWord){
  for (int i = 0; i < singleWord.length(); i++){
    if (singleWord[i] == '-') showDash();
    else if (singleWord[i] == '.') showDot();
    else showIntracharacter();
  }
}

void displayOn(){
  digitalWrite(13, HIGH);
}

void displayOff(){
  digitalWrite(13, LOW);
}

void showDash(){
 displayOn();
 delay(morseElementLength * 3);
 displayOff();
 delay(morseElementLength);
}

void showDot(){
 displayOn();
 delay(morseElementLength);
 displayOff();
 delay(morseElementLength);
}

void showIntracharacter(){
 displayOff();
 delay(morseElementLength*2); //each element naturally has a one-dot space built in
}

void showSpace(){
  displayOff();
  delay(morseElementLength*6); //each element naturally has a one-dot space built in that follows it.
}
