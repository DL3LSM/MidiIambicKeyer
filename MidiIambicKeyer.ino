///////////////////////////////////////////////////////////////////////////////
//
//  Iambic Morse Code Keyer Sketch
//  Copyright (c) 2009 Steven T. Elliott
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details:
//
//  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
//  Boston, MA  02111-1307  USA
//
//  http://openqrp.org/?p=343
//
//  "Trimmed" by Bill Bishop - wrb[at]wrbishop.com
//
//  MIDI and other interface stuff added by Mario Illgen - dl3lsm@gmail.com
//    inspired by MidiKey from Roger E Critchlow Jr - https://github.com/recri/keyer/blob/master/embedded/MidiKey/MidiKey.ino
//

#include <Bounce2.h>

// MIDI definitions
const int keyerChannel = 1;      // the MIDI channel number to send messages
const int base_note = 0;    // the base midi note

// commands and queries from the computer
const int inputChannel = 2; // the MIDI channel to receive commands as notes
const int wpmControl = 0;      // note for WPM command, value is wpm value
const int isKeyerControl = 1;    // set behavior as keyer or dumb interface: value > 0 -> Iambic Keyer, = 0 -> Interface
const int reverseControl = 2;  // value > 0 -> Paddle Reverse
const int iambicControl = 3;   // value > 0 -> Iambic B, = 0 -> Iambic A
// 
// query for keyer state
// Resonses:
//    responseIsKeyer: value = 1 - yes, value 0 - no, value = 2 - Winkeyer (not in this Sketch)
//  when keyer then more responses:
//    responseWpm: value = wpm
//    responseReverse: Paddle reverse: value > 0 - yes, value 0 - no
//    responseIambic: Iambic Mode: value > 0 -> Iambic B, = 0 -> Iambic A
const int getKeyerStateControl = 4;

// responses to the computer
const int responseChannel = 3;  // the channel to send response messages
const int responseFail = 0;
const int responseOk = 1;
const int responseIsKeyer = 2;    // value = 1 - yes, value 0 - no, value = 2 - Winkeyer (not in this Sketch)
const int responseWpm = 3;        // value = wpm
const int responseReverse = 4;    // Paddle reverse: value > 0 - yes, value 0 - no
const int responseIambic = 5;     // Iambic Mode: value > 0 -> Iambic B, = 0 -> Iambic A

// Instantiate a Bounce object
Bounce debouncer_DahPin = Bounce(); 

// Instantiate another Bounce object
Bounce debouncer_DitPin = Bounce();

int isKeyer = 0;    // always start as interface

///////////////////////////////////////////////////////////////////////////////
//
//                         openQRP CPU Pin Definitions
//
///////////////////////////////////////////////////////////////////////////////
//
// Digital Pins
//
int         paddleReverse = 0;
const int   DahPin          = 0;       // Dah paddle input or PTT
const int   DitPin          = 1;       // Dit paddle input or KEY
const int   ledPin        = 13;      //
//
////////////////////////////////////////////////////////////////////////////////
//
//  keyerControl bit definitions
//
#define     DIT_L      0x01     // Dit latch
#define     DAH_L      0x02     // Dah latch
#define     DIT_PROC   0x04     // Dit is being processed
#define     PDLSWAP    0x08     // 0 for normal, 1 for swap
#define     IAMBICB    0x10     // 0 for Iambic A, 1 for Iambic B
//
////////////////////////////////////////////////////////////////////////////////
//
//  Library Instantiations
//
////////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////
//
//  Global Variables
//
 
unsigned long       ditTime;                    // No. milliseconds per dit
unsigned char       keyerControl;
unsigned char       keyerState;
unsigned int        wpm = 20;

unsigned char       iambicMode = IAMBICB;       // or 0 for Iambic A
 
///////////////////////////////////////////////////////////////////////////////
//
//  State Machine Defines
 
enum KSTYPE {IDLE, CHK_DIT, CHK_DAH, KEYED_PREP, KEYED, INTER_ELEMENT };

///////////////////////////////////////////////////////////////////////////////
//
//  System Initialization
//
///////////////////////////////////////////////////////////////////////////////

int ptt;                   // the current ptt value
int key;                   // the current key value

void setup() {
 
    // Setup outputs
    pinMode(ledPin, OUTPUT);      // sets the digital pin as output
 
    // Setup control input pins
    // Setup the first button with an internal pull-up :
    pinMode(DahPin,INPUT_PULLUP);
    // After setting up the button, setup the Bounce instance :
    debouncer_DahPin.attach(DahPin);
    debouncer_DahPin.interval(5); // interval in ms
  
    // Setup the second button with an internal pull-up :
    pinMode(DitPin,INPUT_PULLUP);
    // After setting up the button, setup the Bounce instance :
    debouncer_DitPin.attach(DitPin);
    debouncer_DitPin.interval(5); // interval in ms
  
    digitalWrite(ledPin, LOW);   // turn the LED off

    setupIambic(isKeyer);

    usbMIDI.setHandleControlChange(myControlChange);  // set callback for commands  
}
 
///////////////////////////////////////////////////////////////////////////////
//
//  Main Work Loop
//
///////////////////////////////////////////////////////////////////////////////

void loop() {

  if (isKeyer == 0) {
    interfaceFunc();
  } else {
    keyerFunc();
  }

  // Read commands from input channel, callbacks are set up
  while (usbMIDI.read(inputChannel)) {
    // ignore incoming messages
  }
}

void interfaceFunc() {

  // Update the Bounce instances :
  debouncer_DahPin.update();
  debouncer_DitPin.update();

  // Get the updated ptt value
  int new_ptt = debouncer_DahPin.read();
  if (new_ptt != ptt) {
    if ((ptt = new_ptt) != 0) {
      usbMIDI.sendNoteOff(base_note+0, 0, keyerChannel);
    } else {
      usbMIDI.sendNoteOn(base_note+0, 99, keyerChannel);
    }
    usbMIDI.send_now();
  }

  // Get the updated key value
  int new_key = debouncer_DitPin.read();
  if (new_key != key) {
    if ((key = new_key) != 0) {
      digitalWrite(ledPin, LOW);    // set the LED off
      usbMIDI.sendNoteOff(base_note+1, 0, keyerChannel);
    } else {
      digitalWrite(ledPin, HIGH);   // set the LED on
      usbMIDI.sendNoteOn(base_note+1, 99, keyerChannel);
    }
    usbMIDI.send_now();
  }
}

void keyerFunc()
{
  static unsigned long ktimer;

  // Update the Bounce instances :
  debouncer_DahPin.update();
  debouncer_DitPin.update();
  
  // Basic Iambic Keyer
  // keyerControl contains processing flags and keyer mode bits
  // Supports Iambic A and B
  // State machine based, uses calls to millis() for timing.
 
  switch (keyerState) {
    case IDLE:
        // Wait for direct or latched paddle press
        if ((debouncer_DahPin.read() == LOW) ||
                (debouncer_DitPin.read() == LOW) ||
                    (keyerControl & 0x03)) {
            update_PaddleLatch();
            keyerState = CHK_DIT;
        }
        break;
 
    case CHK_DIT:
        // See if the dit paddle was pressed
        if (keyerControl & DIT_L) {
            keyerControl |= DIT_PROC;
            ktimer = ditTime;
            keyerState = KEYED_PREP;
        }
        else {
            keyerState = CHK_DAH;
        }
        break;
 
    case CHK_DAH:
        // See if dah paddle was pressed
        if (keyerControl & DAH_L) {
            ktimer = ditTime*3;
            keyerState = KEYED_PREP;
        }
        else {
            keyerState = IDLE;
        }
        break;
 
    case KEYED_PREP:
        // Assert key down, start timing, state shared for dit or dah
        digitalWrite(ledPin, HIGH);         // turn the LED on
        usbMIDI.sendNoteOn(base_note+1, 99, keyerChannel);
        ktimer += millis();                 // set ktimer to interval end time
        keyerControl &= ~(DIT_L + DAH_L);   // clear both paddle latch bits
        keyerState = KEYED;                 // next state
        break;
 
    case KEYED:
        // Wait for timer to expire
        if (millis() > ktimer) {            // are we at end of key down ?
            digitalWrite(ledPin, LOW);      // turn the LED off
            usbMIDI.sendNoteOff(base_note+1, 0, keyerChannel);
            ktimer = millis() + ditTime;    // inter-element time
            keyerState = INTER_ELEMENT;     // next state
        }
        else if (keyerControl & IAMBICB) {
            update_PaddleLatch();           // early paddle latch in Iambic B mode
        }
        break;
 
    case INTER_ELEMENT:
        // Insert time between dits/dahs
        update_PaddleLatch();               // latch paddle state
        if (millis() > ktimer) {            // are we at end of inter-space ?
            if (keyerControl & DIT_PROC) {             // was it a dit or dah ?
                keyerControl &= ~(DIT_L + DIT_PROC);   // clear two bits
                keyerState = CHK_DAH;                  // dit done, check for dah
            }
            else {
                keyerControl &= ~(DAH_L);              // clear dah latch
                keyerState = IDLE;                     // go idle
            }
        }
        break;
    }
    usbMIDI.send_now();
}
 
///////////////////////////////////////////////////////////////////////////////
//
//    Latch dit and/or dah press
//
//    Called by keyer routine
//
///////////////////////////////////////////////////////////////////////////////
 
void update_PaddleLatch()
{
  if (paddleReverse == 0) {
    if (debouncer_DitPin.read() == LOW) {
        keyerControl |= DIT_L;
    }
    if (debouncer_DahPin.read() == LOW) {
        keyerControl |= DAH_L;
    }
  } else {
    if (debouncer_DahPin.read() == LOW) {
        keyerControl |= DIT_L;
    }
    if (debouncer_DitPin.read() == LOW) {
        keyerControl |= DAH_L;
    }
  }
}
 
///////////////////////////////////////////////////////////////////////////////
//
//    Calculate new time constants based on wpm value
//
///////////////////////////////////////////////////////////////////////////////
 
void loadWPM (int wpm)
{
    ditTime = 1200/wpm;
}

// MIDI callback
void myControlChange(byte channel, byte control, byte value) {

  int ok = 1;
  
  if (channel != inputChannel) {
    // error, unexpected channel
    sendMidiResponseOk(0);
    return;
  }
  
  switch (control) {
    case isKeyerControl:
      if (value > 0) {
        setupIambic(1); 
      } else {
        setupIambic(0);
      }
      break;
    case iambicControl:
      if (value > 0) {
        setupIambicMode(1); 
      } else {
        setupIambicMode(0);
      }
      break;
    case wpmControl:
      wpm = value;
      setupIambic(1);
      break;
    case reverseControl:
      if (value > 0) {
        paddleReverse = 1;
      } else {
        paddleReverse = 0;
      }
      setupIambic(1);
      break;
    case getKeyerStateControl:
      sendKeyerStateResponse();
      break;
    default:
      // unknown command
      sendMidiResponseOk(0);
  }

  if (ok == 1) {
    sendMidiResponseOk(1);
  } else {
    sendMidiResponseOk(0);
  }
}

void setupIambic(int on) {
  
  if (on == 1) {
    keyerState = IDLE;
    keyerControl = iambicMode;
    loadWPM(wpm);
  } else {
    // interface mode
    ptt = digitalRead(DahPin);
    key = digitalRead(DitPin);
  }
  isKeyer = on;
}

void setupIambicMode(int modeB) {

  if (modeB == 1) {
    iambicMode = IAMBICB;
  } else {
    iambicMode = 0;
  }
  setupIambic(1);
}

void sendMidiResponseOk(int ok) {

  if (ok == 1) {
    // ok
    usbMIDI.sendControlChange(responseOk, 99, responseChannel);
  } else {
    // error
    usbMIDI.sendControlChange(responseFail, 0, responseChannel);
  }
}

void sendKeyerStateResponse() {

  // debug
  //digitalWrite(ledPin, HIGH);   // set the LED on
  //delay(100);
  //digitalWrite(ledPin, LOW);   // set the LED off
  // debug
  
  usbMIDI.sendControlChange(responseIsKeyer, isKeyer, responseChannel);
  
  if (isKeyer == 1) {
    // active, send more status infos
    usbMIDI.sendControlChange(responseWpm, wpm, responseChannel);
    usbMIDI.sendControlChange(responseReverse, paddleReverse, responseChannel);
    usbMIDI.sendControlChange(responseIambic, iambicMode, responseChannel);
  }
}
