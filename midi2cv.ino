/* 
 *    MIDI2CV 
 *    Copyright (C) 2017  Larry McGovern
 *  
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License <http://www.gnu.org/licenses/> for more details.
 */
 
#include <MIDI.h>
#include <SPI.h>

#define GATE  2
#define TRIG  3
#define CLOCK 4
#define DAC1  8 
#define DAC2  9

MIDI_CREATE_DEFAULT_INSTANCE();

void setup()
{
 pinMode(GATE, OUTPUT);
 pinMode(TRIG, OUTPUT);
 pinMode(CLOCK, OUTPUT);
 pinMode(DAC1, OUTPUT);
 pinMode(DAC2, OUTPUT);
 digitalWrite(GATE,LOW);
 digitalWrite(TRIG,LOW);
 digitalWrite(CLOCK,LOW);
 digitalWrite(DAC1,HIGH);
 digitalWrite(DAC2,HIGH);

 SPI.begin();

 MIDI.begin(MIDI_CHANNEL_OMNI);

 // Set initial pitch bend voltage to 0.5V (mid point).  With Gain = 1X, this is 1023
 // Other DAC outputs will come up as 0V, so don't need to be initialized
 setVoltage(DAC2, 0, 0, 1023);  
}


bool notes[88]; 

void loop()
{
  int type, note, velocity, channel, d1, d2;
  static unsigned long trig_timer=0, clock_timer=0, clock_timeout=0;
  static unsigned int clock_count=0;

  if ((trig_timer > 0) && (millis() - trig_timer > 20)) { 
    digitalWrite(TRIG,LOW); // Set trigger low after 20 msec 
    trig_timer = 0;  
  }

  if ((clock_timer > 0) && (millis() - clock_timer > 20)) { 
    digitalWrite(CLOCK,LOW); // Set clock pulse low after 20 msec 
    clock_timer = 0;  
  }
  
  if (MIDI.read()) {                    
    byte type = MIDI.getType();
    switch (type) {
      case midi::NoteOn: 
        note = MIDI.getData1() - 21; // A0 = 21, Top Note = 108
        velocity = MIDI.getData2(); // Velocity from 0 to 127
        channel = MIDI.getChannel();
        
        if ((note < 0) || (note > 87)) break;
        
        notes[note] = true;
        commandTopNote();
        
        // velocity range from 0 to 4095 mV  Left shift d2 by 5 to scale from 0 to 4095, 
        // and choose gain = 2X
        setVoltage(DAC1, 1, 1, velocity<<5);  // DAC1, channel 1, gain = 2X

        // Start trigger
        digitalWrite(TRIG,HIGH);
        trig_timer=millis();
        
        break;
        
      case midi::NoteOff:
        note = MIDI.getData1() - 21; // A0 = 21, Top Note = 108
        velocity = MIDI.getData2();
        channel = MIDI.getChannel();

        if ((note < 0) || (note > 87)) break;
        
        notes[note] = false;
        commandTopNote();
        
        break;
        
      case midi::PitchBend:
        d1 = MIDI.getData1();
        d2 = MIDI.getData2(); // d2 from 0 to 127, mid point = 64

        // Pitch bend output from 0 to 1023 mV.  Left shift d2 by 4 to scale from 0 to 2047.
        // With DAC gain = 1X, this will yield a range from 0 to 1023 mV.  
        setVoltage(DAC2, 0, 0, d2<<4);  // DAC2, channel 0, gain = 1X
        
        break;

      case midi::ControlChange: 
        d1 = MIDI.getData1();
        d2 = MIDI.getData2(); // From 0 to 127

        // CC range from 0 to 4095 mV  Left shift d2 by 5 to scale from 0 to 4095, 
        // and choose gain = 2X
        setVoltage(DAC2, 1, 1, d2<<5);  // DAC2, channel 1, gain = 2X
        break;
        
      case midi::Clock:
        if (millis() > clock_timeout + 300) clock_count = 0; // Prevents Clock from starting in between quarter notes after clock is restarted!
        clock_timeout = millis();
        
        if (clock_count == 0) {
          digitalWrite(CLOCK,HIGH); // Start clock pulse
          clock_timer=millis();    
        }
        clock_count++;
        if (clock_count == 24) {  // MIDI timing clock sends 24 pulses per quarter note.  Sent pulse only once every 24 pulses
          clock_count = 0;  
        }
        break;
        
      case midi::ActiveSensing: 
        break;
        
      default:
        d1 = MIDI.getData1();
        d2 = MIDI.getData2();
    }
  }
}

void commandTopNote()
{
  int topNote = 0;
  unsigned int mV; 
  bool noteActive = false;
  
  for (int i=0; i<88; i++)
  {
    if (notes[i]) {
      topNote = i;
      noteActive = true;
    }
  }

  if (!noteActive) // All notes are off
  {
    digitalWrite(GATE,LOW);
    return;
  }
  digitalWrite(GATE,HIGH);
  
  // Rescale 88 notes to 4096 mV, where note[0] = 0 mV and note[87] = 4096 mV
  // DAC output will be 47.069 mV per note, and 564.9655 mV per octive
  // Note that DAC output will need to be amplified by 1.77X for the standard 1V/octive
  
  mV = (unsigned int) ((float) topNote * (4095.0f / 87.0f) + 0.5); 

  setVoltage(DAC1, 0, 1, mV);  // DAC1, channel 0, gain = 2X
}

// setVoltage -- Set DAC voltage output
// dacpin: chip select pin for DAC.  Note and velocity on DAC1, pitch bend and CC on DAC2
// channel: 0 (A) or 1 (B).  Note and pitch bend on 0, velocity and CC on 2.
// gain: 0 = 1X, 1 = 2X.  
// mV: integer 0 to 4095.  If gain is 1X, mV is in units of half mV (i.e., 0 to 2048 mV).
// If gain is 2X, mV is in units of mV

void setVoltage(int dacpin, bool channel, bool gain, unsigned int mV)
{
  unsigned int command = channel ? 0x9000 : 0x1000;

  command |= gain ? 0x0000 : 0x2000;
  command |= (mV & 0x0FFF);
  
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(dacpin,LOW);
  SPI.transfer(command>>8);
  SPI.transfer(command&0xFF);
  digitalWrite(dacpin,HIGH);
  SPI.endTransaction();
}

