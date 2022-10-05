/*   V1.0.0
 *   OSC / ADSR
 *   
 *   This code will require the midi library
 *   as well as the mozzi library
*/

//potentiometers:
  //A0 = attack
  //A1 = decay
  //A2 = sustain
  //A3 = release
  //A4 = portamento
  //A5 = osc mode
// OUTPUT = A9 on most arduino.... A11 on arduino mega and typically the bigger ones

#define CONTROL_RATE 128
#define CV_OUT 8  // A8
#define LED 13 // A13

#include <MIDI.h>
#include <MozziGuts.h>
#include <Oscil.h> /oscSaw/ oscillator template
#include <Line.h> // for envelope
#include <tables/cos2048_int8.h> // oscil data
#include <tables/saw1024_int8.h> // oscil data
#include <tables/smoothsquare8192_int8.h> // NB portamento requires table > 512 size?
#include <mozzi_midi.h>
#include <ADSR.h>
#include <mozzi_fixmath.h>
#include <Portamento.h>
#include "ADSRReadings.h"

MIDI_CREATE_DEFAULT_INSTANCE();

Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> oscCarrier(SMOOTHSQUARE8192_DATA);
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> oscSquare2(SMOOTHSQUARE8192_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscModulator(COS2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscModulator2(COS2048_DATA);
Oscil<COS2048_NUM_CELLS, AUDIO_RATE> oscLFO(COS2048_DATA); //maybe later?
Oscil<SAW1024_NUM_CELLS, AUDIO_RATE> oscSaw1(SAW1024_DATA); 
Oscil<SAW1024_NUM_CELLS, AUDIO_RATE> oscSaw2(SAW1024_DATA); 

ADSR<CONTROL_RATE,CONTROL_RATE> envelope;
ADSR<CONTROL_RATE,CONTROL_RATE> envelope2;

Portamento <CONTROL_RATE> aPortamento;

struct ADSRReadings adsrReadings = {1, 1, 1, 0};

int chanGain1;
int chanGain2;
unsigned int carrierXmodDepth1;
unsigned int carrierXmodDepth2;
// TODO: assign a cv output
//unsigned int cv_out = 0;
unsigned int cv_in = 0;

float carrierFreq = 10.f;
float carrier2 = 10.f;

float modFreq = 10.f;
float modFreq2 = 10.f;
float modDepth = 0;

int nextenv = 1;
int bitmask = 64;
int multiplier;
int portSpeed = 0;

float shifted = 0.1f;
byte mode = 0;

byte lastNotes[] = { 0, 0 };

float modOffsets[] = {
  4,3.5,3,2.5,2,1.5,1,0.6666666,0.5,0.4,0.3333333,0.2857,0.25,0,0,0,0,0,0,
}; // harmonic ratios corresponding to DP's preferred intervals of 7, 12, 7, 19, 24, 0, 12, -12, etc

void setup() {
  pinMode(LED, OUTPUT);   // midi received LED
  pinMode(CV_OUT, OUTPUT); // For CV OUT

  MIDI.begin(MIDI_CHANNEL_OMNI);

  MIDI.setHandleNoteOn(handleNoteOn); 
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleProgramChange(handleProgramChange); 
  MIDI.setHandleContinue(handleContinue); 
  MIDI.setHandleStop(handleStop); 
  MIDI.setHandleStart(handleStart); 

  int twentySeconds = 20000;
  envelope.setADLevels(127,100);
  envelope.setTimes(20, 20, twentySeconds, 1200);
  envelope2.setADLevels(127, 100);
  envelope2.setTimes(20, 20, twentySeconds, 1200);

  aPortamento.setTime(0u);
  oscLFO.setFreq(10);

  startMozzi(CONTROL_RATE); 
}


void updateFreqs(float modOffset = 1){
  oscCarrier.setFreq(carrierFreq);
  oscSaw1.setFreq(carrierFreq);

  carrierXmodDepth1 = carrierFreq * modDepth;
  modFreq = (carrierFreq * modOffset / (64 / bitmask) );

  oscSquare2.setFreq(carrier2);
 // oscSaw2.setFreq(carrier2 * (1.0f + ((shifted -0.1f) / 384)) );
  oscSaw2.setFreq(carrier2);
  carrierXmodDepth2 = carrier2 * modDepth;
  modFreq2 = (carrier2 * modOffset / (64 / bitmask) );
  
  oscModulator.setFreq( modFreq ); 
  oscModulator2.setFreq( modFreq2 ); 
}

void handleProgramChange (byte channel, byte number){
  float modOffset = modOffsets[number % 15];
  updateFreqs(modOffset);
}

void handleNoteOn(byte channel, byte note, byte velocity) { 
  digitalWrite(CV_OUT, HIGH);    //send out CV trig for duration of note
  aPortamento.start(note);
  if(portSpeed){
    nextenv = 1;   
    lastNotes[1] = 999;
  }
  if(nextenv == 1){ 
    noteToCarrier(&carrierFreq, envelope, note, 0);
    updateFreqs();
  }
  else{
    noteToCarrier(&carrier2, envelope2, note, 1);
    updateFreqs();
  }
}

void noteToCarrier(float* carrierFrequency, ADSR<CONTROL_RATE, CONTROL_RATE> adsrEnvelope, byte note, int index){
  digitalWrite(LED, HIGH);

  lastNotes[index] = note;
  adsrEnvelope.noteOn();
  nextenv = 2 - index;
  carrierFrequency = parseNote(note);
}

float parseNote(byte note){
  return Q16n16_to_float(Q16n16_mtof(Q8n0_to_Q16n16(note)));
}

void handleStop () {}

void handleStart () {}

void handleContinue () {}

void handleNoteOff(byte channel, byte note, byte velocity) { 
  digitalWrite(CV_OUT, LOW);
  digitalWrite(LED, LOW);

  if (note == lastNotes[0]){ //kill envelope on released note
    envelope.noteOff(); 
    if (chanGain2)
      nextenv = 1;
  } 
  if (note == lastNotes[1]){
    envelope2.noteOff(); 
    if (chanGain1)
      nextenv = 2;
  }
  
}


void handlePitchBend (byte channel, int bend) //pitch bend, and mod wheel. I wouldn't use this right now as the code is incapable of "multitasking"
{
  //bend value from +/-8192, translate to 0.1-8 Hz?
  shifted = float ((bend + 8500) / 2048.f ) + 0.1f;  
  if (mode == 0) 
    oscLFO.setFreq(shifted);
  else if (mode == 1){
     bitmask = 1 << (int (shifted)); 
     multiplier = 7 - (int (shifted)); 
     updateFreqs();
  }
  else if (mode == 2)
    oscSaw2.setFreq(carrier2 * (1.0f + ((shifted - 0.1f) / 384)) );
  else
    bitmask = 64;
}



void handleControlChange (byte channel, byte number, byte value){
  switch(number) {
  // could trap different controller IDs here
    case 1: 
      float divided = float (value / 46.f ); // values=0-127, map to 0-2.75
      modDepth = (divided * divided);      // square to 0-8
      if (modDepth > 7.5)
        modDepth = 7.5;
      if (modDepth < 0.2)
        modDepth = 0; // easier to get pure tone 
      updateFreqs();
      break;
  }
}

void updateADSR(){
  envelope.setAttackTime(adsrReadings.attack);
  envelope.setReleaseTime(adsrReadings.release);
  envelope.setDecayTime(adsrReadings.decay);
  envelope.setSustainTime(adsrReadings.sustain);
  
  envelope2.setAttackTime(adsrReadings.attack);
  envelope2.setReleaseTime(adsrReadings.release);
  envelope2.setDecayTime(adsrReadings.decay);
  envelope2.setSustainTime(adsrReadings.sustain);

  envelope.update();
  envelope2.update();
}

void updateControl(){
  MIDI.read();

  adsrReadings = { analogRead(A0), analogRead(A1), analogRead(A2), analogRead(A3) };
  portSpeed = analogRead(A4);
  mode = map(analogRead(A5), 0, 1023, 0, 5);
  cv_in = map(mozziAnalogRead(A6), 0, 1023, 48, 95);
  
  updateADSR();
  aPortamento.setTime(portSpeed * 2); //multiple of two otherwise effect is kind of indiscernable

  chanGain1 = envelope.next();
  chanGain2 = envelope2.next();

  if(chanGain1 > 120) 
    chanGain1 = 120;  //changain is the next envelope
  if(chanGain2 > 120) 
    chanGain2 = 120;  //which we dont want to exceed 120

  if(portSpeed) {
    carrierFreq = Q16n16_to_float(aPortamento.next());
    updateFreqs();
  }

  if (mode == 3) {
    modFreq *= 1.0f + (shifted / 384); // generates fake LFO via detuning mod osc
    modFreq2 *= 1.0f + (shifted / 384); // generates fake LFO via detuning mod osc
    oscModulator.setFreq( modFreq ); 
    oscModulator2.setFreq( modFreq2 ); 
  }
}

int bitCrush(int x, int a){
  return (x >> a) << a;
} // destroys a certain amount of bits in a number via shifts

int updateAudio(){
  switch(mode){
    case 0: // FM with LFO 
      int LFO = oscLFO.next();

      long vibrato = ( LFO * oscModulator.next() ) >>7 ;
      vibrato *= carrierXmodDepth1;
      long vibrato2 = ( LFO * oscModulator2.next() ) >>7 ;
      vibrato2 *= carrierXmodDepth2;

      return (int) ((
          (oscCarrier.phMod( vibrato >>3 ) * chanGain1 ) 
        + (oscSquare2.phMod( vibrato2 >>3 ) * chanGain2 ) 
        )) >> 9;
      // >> 9 = 9 fast binary divisions by 2 = divide by 512
        break;
  
    case 1: // ADDING square + "converted" sine
      int squaredSin1 = ( (oscModulator.next() & bitmask) ) << multiplier;
      int squaredSin2 = ( (oscModulator2.next() & bitmask) ) << multiplier;
      return (int) 
        (( 
          ( ( (oscCarrier.next() + squaredSin1 )>>1 )* chanGain1 )
        + ( ( (oscSquare2.next() + squaredSin2 )>>1 )* chanGain2 )
        )) >> 8;
      break;
      
    case 2: // 2 tri poly
      return (int)(( 
          ( oscSaw1.next() *chanGain1 )
        + ( oscSaw2.next() *chanGain2 ) 
        )) >> 9;
      break;

    case 3: //big whoop
      return (int)(
          (( (oscCarrier.next()^oscModulator.next()) * chanGain1 )  
        + ( (oscSquare2.next()^oscModulator2.next())* chanGain2 ) 
        )) >> 8;
      break;
  
    case 4: // 2 sq poly ORd with respective sines
      return (int) (( ( (oscCarrier.next()|oscModulator.next() ) * envelope.next()) + ( (oscSquare2.next()|oscLFO.next() ) * envelope2.next()) )) >> 8;
      break;
  
    case 5: // 2 sq poly
      return (int) (( (oscCarrier.next() * envelope.next()) + (oscSquare2.next() * envelope2.next()) )) >> 8;
      //return (int) ( (oscCarrier.next() | ( (oscModulator.next() * int (modDepth+1))>>3 ) )  * (envelope.next() ) ) >> 7;
      break;
  }
}

void loop() {
  audioHook();
} 
