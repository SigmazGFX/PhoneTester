/*/////////////////////////////////////////////////////////////////////////
// Arduino UNO phone device tester with display & QCX601 SLIC board      //
// 2018 -SigmazGFX                                                       // 
///////////////////////////////////////////////////////////////////////////
*/


//DTMF library to handle DTMF via audio sampling
#include <DTMF.h>

// NOTE that N MUST NOT exceed 160
// This is the number of samples which are taken in a call to
// .sample. The smaller the value of N the wider the bandwidth.
// For example, with N=128 at a sample rate of 8926Hz the tone
// detection bandwidth will be 8926/128 = 70Hz. If you make N
// smaller, the bandwidth increases which makes it harder to detect
// the tones because some of the energy of one tone can cross into
// an adjacent (in frequency) tone. But a larger value of N also means
// that it takes longer to collect the samples.
// A value of 64 works just fine, as does 128.
// NOTE that the value of N does NOT have to be a power of 2.
float n=128.0;
// sampling rate in Hz
float sampling_rate=8926.0;

// Instantiate the dtmf library with the number of samples to be taken
// and the sampling rate.
DTMF dtmf = DTMF(n,sampling_rate);
//----------------------------------

//set up states of the machine
#define IDLE_WAIT 1
#define RINGING 2
#define ACTIVE_CALL 3
#define GETTING_NUMBER 4

int sensorPin = A0;     // Analog pin used to sample audio for DTMF decoding
#define rflPin 2        // RFL pin to QCX601 pin J1-1, reverse signal (LOW)
#define hzPin 3         // 25Hz pin to QCX601 pin J1-2 (25Hz signal for ringing) _-_-_-_-_
#define rcPin 8         // RC pin to QCX601 pin J1-3, ring control, HIGH when ringing --___--___--___
#define shkPin 9        // switch hook pin from QCX601 pin J1-4
#define ringTestPin 12  // Pin assigned to start ring test

#define oscInterval 20 
#define ringInterval 6000
#define ringDuration 1200
#define statusCheckInterval 1000

#define tNewDig 500     // time since last SHK rising edge before counting for next digit
#define tHup 2000       // time since last SHK falling edge before hanging up/flushing number
#define tComplete 6000  // time since last SHK rising edge before quit collecting #'s
#define tDebounce 15    // debounce time

unsigned long currentMillis = 0L;
unsigned long oscPreviousMillis = 0L;
unsigned long ringPreviousMillis = 0L;
unsigned long statusPreviousMillis = 0L;
unsigned long lastShkDebounce = 0L;
unsigned long lastShkRise = 0L;
unsigned long lastShkFall = 0L;
int hookPrompt = 0;
int shkState = 0;           // LOW is on hook, HIGH is off hook
int edge;
int lastShkReading = 0;
int currentShkReading = 0;

int ringTest; 
byte pulses = 0;
byte digit = 0;
String number = "";
byte digits = 0;            // the number of digits we have collected
char numArray[11];
//byte gsmStatus;
byte state;


////////////////////////////////////////////////////////////////////////////
// 
//  SETUP
//
void setup() {
  
  pinMode(sensorPin,INPUT);
  pinMode(shkPin, INPUT_PULLUP);
  pinMode(hzPin, OUTPUT);
  pinMode(rcPin, OUTPUT);
  pinMode(rflPin, OUTPUT);
  digitalWrite(rflPin, 0);
  pinMode(ringTestPin, INPUT_PULLUP);

  Serial.begin(115200); // start serial for debug monitor
  Serial.println("\nTelephone Tester Ready..");
   state = IDLE_WAIT;
}
int nochar_count = 0;
float d_mags[8];
////////////////////////////////////////////////////////////////////////////
// 
//  MAIN LOOP
//

void loop() {

  currentMillis = millis(); // get snapshot of time for debouncing and timing

  // read and debounce hook pin
  currentShkReading = digitalRead(shkPin);
  if (currentShkReading != lastShkReading) {
    // reset debouncing timer
    lastShkDebounce = currentMillis;
  }
  if ((unsigned long)(currentMillis - lastShkDebounce) > tDebounce) {
    // debounce done, set shk state to debounced value if changed
    if (shkState != currentShkReading) {
      shkState = currentShkReading;
      if (shkState == HIGH) {
        lastShkRise = millis();
      } else {
        lastShkFall = millis();  
      }
    }
  }
  lastShkReading = currentShkReading;

  if ((unsigned long)(currentMillis - statusPreviousMillis) >= statusCheckInterval) {
    // Time to check control status 
    ringTest = !digitalRead(ringTestPin);
    statusPreviousMillis = currentMillis;
  }


  ////////////////////////////////////////////////////////////////////////////
  // 
  //  STATE IDLE_WAIT
  //
  if (state == IDLE_WAIT) {
    // wait for ring test or picking up reciever
    if (ringTest) {
      Serial.println("Ringing.");
      state = RINGING;
    }
   // #endif    
    if (shkState == HIGH) {
           if (hookPrompt == 0){
      Serial.println("Off hook. Dial test ready. ");
      }
      state = GETTING_NUMBER;
    }
  }
  ////////////////////////////////////////////////////////////////////////////
  // 
  //  STATE RINGING
  //
  if (state == RINGING) {  
    // Ringing interval 
    // How much time has passed, accounting for rollover with subtraction!
    if ((unsigned long)(currentMillis - ringPreviousMillis) >= ringInterval) {
      digitalWrite (rcPin,1); // Ring
      // Use the snapshot to set track time until next event
      ringPreviousMillis = currentMillis;
    }
    if (digitalRead(rcPin) && ((unsigned long)(currentMillis - ringPreviousMillis) >= ringDuration)) {
        digitalWrite(rcPin, 0); // Silent after ring duration
    }
    // 25Hz oscillation      
    // How much time has passed, accounting for rollover with subtraction!
    if ((unsigned long)(currentMillis - oscPreviousMillis) >= oscInterval) {
      // It's time to do something!
      if (digitalRead(rcPin)) {
        digitalWrite(hzPin, !digitalRead(hzPin)); // Toggle the 25Hz pin
      }    
      // Use the snapshot to set track time until next event
      oscPreviousMillis = currentMillis;
    }
    if (shkState == HIGH) {
      digitalWrite(rcPin, 0); // stop ringing
      digitalWrite(hzPin, 0);
      Serial.println("Answered. Loop initiated.");
     state = ACTIVE_CALL; 
    }
    if (!ringTest) {
      digitalWrite(rcPin, 0); // stop ringing
      digitalWrite(hzPin, 0);
      Serial.println("Going back to idle.");
      Serial.println("\nTelephone Tester Ready..");
      state = IDLE_WAIT;
    }
  
  }
  ////////////////////////////////////////////////////////////////////////////
  // 
  //  STATE ACTIVE_CALL
  //
  if (state == ACTIVE_CALL) {
    // keep state until on-hook 
    if ((shkState == LOW) && ((unsigned long)(currentMillis - lastShkFall) >= tHup)) {
      //flush everything, then go idle
      Serial.println("Hanging up. Going idle.");
      flushNumber();
      Serial.println("\nTelephone Tester Ready..");
      state = IDLE_WAIT;
    }
   if (!ringTest) {
      flushNumber();
      state = IDLE_WAIT;
    }
  }
  ////////////////////////////////////////////////////////////////////////////
  // 
  //  STATE GETTING_NUMBER
  //
  if (state == GETTING_NUMBER) {
    // count groups of pulses on SHK (loop disconnect) until complete number
    // if single digit, fetch stored number
    // then make call

    if (pulses < 1)
    {
      detectTones();
    }
      

    if (pulses && (unsigned long)(currentMillis - lastShkRise) > tNewDig) {
      // if there are pulses, check rising edge timer for complete digit timeout
      //digit = pulses - 1; // one pulse is zero, ten pulses is nine (swedish system)
      // for systems where ten pulses is zero, use code below instead:
      digit = pulses % 10;
      Serial.println(digit); // just for debug
      // add digit to number string
      number += (int)digit;
      digits++;
      pulses = 0;
    }

    if ((shkState == LOW) && (edge == 0)) {
      edge = 1;
    } else if ((shkState == HIGH) && (edge == 1)) {
      pulses++;
      Serial.print(". "); // just for debug . . . . .
      edge = 0;
    }
    
    if ((digits && (shkState == HIGH) && ((unsigned long)(currentMillis - lastShkRise) /* > tComplete)) || digits == 10 */))) {
      Serial.print("Number dialed: ");
      Serial.println(number);
      //number.toCharArray(numArray, 11);
     hookPrompt = 1;    
     state = ACTIVE_CALL;
    }
    if ((shkState == LOW) && (unsigned long)(currentMillis - lastShkFall) > tHup) {
      // reciever on hook, flush everything and go to idle state
      flushNumber();
      Serial.println("On hook.");
      Serial.println("\nTelephone Tester Ready..");
      hookPrompt = 0;
      state = IDLE_WAIT;
    }
  }
}  // END OF MAIN LOOP


////////////////////////////////////////////////////////////////////////////
// 
//  FUNCTIONS
//

void detectTones()
{
  char thischar;
    // This reads N samples from sensorpin (must be an analog input)
  // and stores them in an array within the library. Use while(1)
  // to determine the actual sampling frequency as described in the
  // comment at the top of this file
  /* while(1) */dtmf.sample(sensorPin);
  
  // The first argument is the address of a user-supplied array
  // of 8 floats in which the function will return the magnitudes
  // of the eight tones.
  // The second argument is the value read by the ADC when there
  // is no signal present. A voltage divider with precisely equal
  // resistors will presumably give a value of 511 or 512.
  // My divider gives a value of 506.
  // If you aren't sure what to use, set this to 512
  dtmf.detect(d_mags,506);

  // detect the button
  // If it is recognized, returns one of 0123456789ABCD*#
  // If unrecognized, returns binary zero

  // Pass it the magnitude array used when calling .sample
  // and specify a magnitude which is used as the threshold
  // for determining whether a tone is present or not
  //
  // If N=64 magnitude needs to be around 1200
  // If N=128 the magnitude can be set to 1800
  // but you will need to play with it to get the right value
  thischar = dtmf.button(d_mags,1800.);
  if(thischar) {
    Serial.print(thischar);
    nochar_count = 0;
    // Print the magnitudes for debugging
//#define DEBUG_PRINT
#ifdef DEBUG_PRINT
    for(int i = 0;i < 8;i++) {
      Serial.print("  ");
      Serial.print(d_mags[i]);
    }
    Serial.println("");
#endif
  } else {
    // print a newline 
    if(++nochar_count == 50)Serial.println("");
    // don't let it wrap around
    if(nochar_count > 30000)nochar_count = 51;
  }
}


void flushNumber() {
  digits = 0;
  number = "";
  pulses = 0;
  edge = 0;
}
