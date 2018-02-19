/*/////////////////////////////////////////////////////////////////////////////
  // Arduino controlled QCX601 SLIC as PSTN device testing device              //
  // Pulse or DTMF digit detection as well as reciever and transmitter testing //
  //2018 -SigmazGFX                                                            //
  ///////////////////////////////////////////////////////////////////////////////
*/

//Includes-------------------
#include <DTMF.h>
#include <LiquidCrystal_I2C.h>
#include <LcdBarGraphX.h>


//DTMF tunings---------------
float n = 128.0;
float sampling_rate = 8926.0;
DTMF dtmf = DTMF(n, sampling_rate);
//----------------------------------

//set up states of the machine
#define IDLE_WAIT 1
#define RINGING 2
#define GETTING_NUMBER 4

int sensorPin = A0;     // Analog pin used to sample audio for DTMF decoding
#define rflPin 2        // RFL pin to QCX601 pin J1-1, reverse signal (LOW)
#define hzPin 7         // 25Hz pin to QCX601 pin J1-2 (25Hz signal for ringing) _-_-_-_-_
#define rcPin 4         // RC pin to QCX601 pin J1-3, ring control, HIGH when ringing --___--___--___
#define shkPin 5        // switch hook pin from QCX601 pin J1-4
#define dTonePin 11     // audio pin for simulated dialtone receiver test
#define ringTestPin 12  // Pin assigned to start ring test

unsigned long pulseDuration;
#define oscInterval 25    //Frequency of Ringer pulse 25hz
#define ringInterval 6000 //Time between rings
#define ringDuration 1800 //Length of ring event
#define statusCheckInterval 100

#define tNewDig 500     // time since last SHK rising edge before counting for next digit
#define tHup 1000       // time since last SHK falling edge before hanging up
#define tDebounce 5    // debounce time

unsigned long currentMillis = 0L;
unsigned long oscPreviousMillis = 0L;
unsigned long ringPreviousMillis = 0L;
unsigned long statusPreviousMillis = 0L;
unsigned long lastShkDebounce = 0L;
unsigned long lastShkRise = 0L;
unsigned long lastShkFall = 0L;

int digitTime = 0;
int hookSpeed = 0;
int pulsePerSec = 0;
int playDtone = 1;           // Play sinmulated dialtone
int shkState = 0;           // LOW is on hook, HIGH is off hook
int edge;
int lastShkReading = 0;
int currentShkReading = 0;
int dialType = 1;           //toggles DTMF or Pulse detection 0-Pulse 1-DTMF
int ringTest;
byte pulses = 0;
byte digit = 0;
byte state;


//I2CLCD
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);   // -- creating LCD instance
#define BACKLIGHT_PIN     13

//LCDBarGraph----------------
LcdBarGraphX lbg0(&lcd, 20, 0, 2); // -- # of segments, position, line,
byte i0 = 0;


////////////////////////////////////////////////////////////////////////////
//
//  SETUP
//
void setup() {

  // Switch on the backlight
  pinMode ( BACKLIGHT_PIN, OUTPUT );
  digitalWrite ( BACKLIGHT_PIN, HIGH );
  lcd.begin(20, 4);              // initialize the lcd

  pinMode(sensorPin, INPUT);
  pinMode(shkPin, INPUT);
  pinMode(hzPin, OUTPUT);
  pinMode(rcPin, OUTPUT);
  pinMode(rflPin, OUTPUT);
  digitalWrite(rflPin, 0);
  pinMode(ringTestPin, INPUT_PULLUP);
  Serial.begin(9600); // start serial for debug monitor
  lcd.home();
  lcd.clear();
  lcd.print("--------------------" );
  lcd.setCursor (0, 1);
  lcd.print("| Telephone Tester |");
  lcd.setCursor(0, 2);
  lcd.print("|  2018 SIGMAZGFX  |");
  lcd.setCursor(0, 3);
  lcd.print("---------------------");
  delay(3000);
  lcd.clear();
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
    lcd.home();
    lcd.print("  Telephone Tester");
    lcd.setCursor (0, 1);
    lcd.print("  Ready..");
    lcd.setCursor(0, 3);
    lcd.print("      On Hook");

    if (ringTest) {
      lcd.home();
      lcd.print("    Ringer Test    ");
      lcd.setCursor(0, 1);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print("                    ");
      lcd.setCursor(0, 2);
      lcd.print("Sending ring current");
      lcd.setCursor(0, 3);
      lcd.print("                    ");
      lcd.setCursor(0, 4);
      lcd.print("                    ");

      state = RINGING;
    }


    if (shkState == HIGH) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Off hook / Dial test ");
      Serial.println("Off hook. Dial test ready. ");
      lcd.setCursor(0, 2);
      lcd.print("Flash hook for");
      lcd.setCursor(0, 3);
      lcd.print("pulse dial test.");
      Serial.println("Flash hook for pulse dial test.");
      state = GETTING_NUMBER;
    }
  }

  ////////////////////////////////////////////////////////////////////////////
  //
  //  STATE RINGING
  //
  if (state == RINGING) {

    //Ringing Interval
    // How much time has passed, accounting for rollover with subtraction!
    if ((unsigned long)(currentMillis - ringPreviousMillis) >= ringInterval) {
      digitalWrite (rcPin, 1); // Ring
      // Use the snapshot to set track time until next event
      ringPreviousMillis = currentMillis;
    }
    if (digitalRead(rcPin) && ((unsigned long)(currentMillis - ringPreviousMillis) >= ringDuration)) {
      digitalWrite(rcPin, 0); // Silent after ring duration
      lcd.setCursor(0, 2);


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
      lcd.setCursor(0, 3);
      lcd.print("Handset Lifted");
    }

    if (!ringTest) {
      digitalWrite(rcPin, 0); // stop ringing
      digitalWrite(hzPin, 0);
      lcd.clear();
      state = IDLE_WAIT;
    }
  }

  //  STATE GETTING_NUMBER
  //
  if (state == GETTING_NUMBER) {

    //Play a simulated dialtone until the first digit is dialed
    if (playDtone == 1) {
      tone(11, 87.5); //Simulated dialtone albeint single freq its really just for testing the RX of the phone
    }
    if (playDtone == 0) {
      noTone(11);
    }

    // count groups of pulses on SHK (loop disconnect)

    if (pulses && (unsigned long)(currentMillis - lastShkRise) > tNewDig) {
      // if there are pulses, check rising edge timer for complete digit timeout

      digit = pulses % 10;
      lcd.home();
      lcd.print("  Pulse Dial Test   ");
      lcd.setCursor(4, 3);
      lcd.print("[");
      lcd.setCursor(15, 3);
      lcd.print("]");
      lcd.setCursor(0, 2);
      lcd.print("Digit dialed: ");
      lcd.print(digit);

      //pulse speed and PPS

      hookSpeed = (lastShkRise - lastShkFall);
      pulsePerSec = (1000 / (lastShkRise - lastShkFall));


      lcd.setCursor(0, 3);
      lcd.print("                    ");
      lcd.setCursor(0, 3);
      lcd.print("PPS: ");
      lcd.print(pulsePerSec);
      lcd.print(" Hook mS: ");
      lcd.print(hookSpeed);


      //Serial.println(digit); // just for debug
      dialType = 0;
      pulses = 0;
    }
    // Enable DTMF detection
    if (dialType == 1)
    {
      detectTones();
    }
    if ((shkState == LOW) && (edge == 0)) {
      edge = 1;
    } else if ((shkState == HIGH) && (edge == 1)) {
      pulses++;
      //Serial.print(". "); // just for debug . . . . .
      edge = 0;
      playDtone = 0; //Turn off dial tone simulation
    }

    if ((shkState == LOW) && (unsigned long)(currentMillis - lastShkFall) > tHup) {
      // reciever on hook, flush everything and go to idle state
      flushNumber();
      lcd.clear();
      lcd.home(); //go home 0,0
      lcd.print("  Telephone Tester");
      lcd.setCursor (2, 1);
      lcd.print("Ready..");
      lcd.setCursor(6, 3);
      lcd.print("On Hook");
      playDtone = 1;
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
  dtmf.detect(d_mags, 506);

  //added to normalize the magnitudes
  for (int i = 0; i < 8; i++) {
    d_mags[i] /= n;
  }

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

  //added to normalize the magnitudes
  thischar = dtmf.button(d_mags, 20.);
  //thischar = dtmf.button(d_mags, 1800.);

  if (thischar) {
    lcd.clear();
    lcd.home();
    lcd.print("   DTMF Dial Test   ");
    lcd.setCursor(0, 2);
    lcd.print("  Digit dialed: ");
    lcd.print(thischar);
    playDtone = 0;  //Turn off dial tone simulation
    nochar_count = 0;
    // Print the magnitudes for debugging
    //#define DEBUG_PRINT
#ifdef DEBUG_PRINT
    for (int i = 0; i < 8; i++) {
      Serial.print("  ");
      Serial.print(d_mags[i]);
    }
    Serial.println("");
#endif
  } else {
    // print a newline
    if (++nochar_count == 50)Serial.println("");
    // don't let it wrap around
    if (nochar_count > 30000)nochar_count = 51;
  }
}


void flushNumber() {
  dialType = 1; //DTMF
  pulses = 0;
  edge = 0;
}

