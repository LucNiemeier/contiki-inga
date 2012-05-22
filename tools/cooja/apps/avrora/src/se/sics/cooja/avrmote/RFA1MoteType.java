/*
 * Copyright (c) 2012, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

package se.sics.cooja.avrmote;

import se.sics.cooja.AbstractionLevelDescription;
import se.sics.cooja.ClassDescription;
import se.sics.cooja.Mote;
import se.sics.cooja.MoteInterface;
import se.sics.cooja.Simulation;
import se.sics.cooja.avrmote.AvroraMoteType;

import se.sics.cooja.avrmote.interfaces.AvrDebugger;
import se.sics.cooja.avrmote.interfaces.AvroraClock;
import se.sics.cooja.avrmote.interfaces.AvroraLED;
import se.sics.cooja.avrmote.interfaces.AvroraADC;
import se.sics.cooja.avrmote.interfaces.RFA1Serial;
import se.sics.cooja.avrmote.interfaces.RFA1Radio;
import se.sics.cooja.avrmote.interfaces.RFA1ID;
import se.sics.cooja.interfaces.Mote2MoteRelations;
import se.sics.cooja.interfaces.MoteAttributes;
import se.sics.cooja.interfaces.Position;

/**
 * Bare AVR atmega128rfa1 mote types emulated in Avrora.
 * 
 * @author Joakim Eriksson, Fredrik Osterlind, David Kopf
 */
@ClassDescription("RFA1 Mote Type")
@AbstractionLevelDescription("Emulated level")
public class RFA1MoteType extends AvroraMoteType {

  // The returned string is used for mote type name and icon jpg file
  public final String getMoteName() {
    return ("RFA1");
  }
  // The returned string is used for firmware file extension
  public final String getMoteContikiTarget() {
    return ("avr-atmega128rfa1");
  }

  public final Mote generateMote(Simulation simulation) {
    RFA1Mote mote = new RFA1Mote(simulation, this);
    mote.initMote();
    return mote;
  }

 @SuppressWarnings("unchecked")
 public Class<? extends MoteInterface>[] getAllMoteInterfaceClasses() {
    return new Class[] {
        Position.class,
        RFA1ID.class,
        AvroraLED.class,
        AvroraADC.class,
        RFA1Radio.class,
        AvroraClock.class,
        RFA1Serial.class,
        AvrDebugger.class,
        Mote2MoteRelations.class,
        MoteAttributes.class
    };
  }

}
