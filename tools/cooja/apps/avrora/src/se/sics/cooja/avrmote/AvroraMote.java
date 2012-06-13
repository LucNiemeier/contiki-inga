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

import java.io.File;
import java.util.ArrayList;
import java.util.Collection;

import org.apache.log4j.Logger;
import org.jdom.Element;

import se.sics.cooja.Mote;
import se.sics.cooja.MoteInterface;
import se.sics.cooja.MoteInterfaceHandler;
import se.sics.cooja.MoteMemory;
import se.sics.cooja.MoteType;
import se.sics.cooja.Simulation;
import se.sics.cooja.motes.AbstractEmulatedMote;
import avrora.arch.avr.AVRProperties;
import avrora.core.LoadableProgram;
import avrora.sim.AtmelInterpreter;
import avrora.sim.Simulator;
import avrora.sim.mcu.AtmelMicrocontroller;
import avrora.sim.mcu.EEPROM;
import avrora.sim.platform.Platform;
import avrora.sim.platform.PlatformFactory;

/**
 * @author Joakim Eriksson, Fredrik Osterlind, David Kopf
 */
public abstract class AvroraMote extends AbstractEmulatedMote implements Mote {
  public static Logger logger = Logger.getLogger(AvroraMote.class);

  private MoteInterfaceHandler moteInterfaceHandler;
  private MoteType moteType;
  private PlatformFactory factory;

  private Platform platform = null;
  private EEPROM EEPROM = null;
  private AtmelInterpreter interpreter = null;
  private AvrMoteMemory memory = null;

  /* Stack monitoring variables */
  private boolean stopNextInstruction = false;

  public AvroraMote(Simulation simulation, MoteType type, PlatformFactory factory) {
    setSimulation(simulation);
    moteType = type;
    this.factory = factory;

    /* Schedule us immediately */
    requestImmediateWakeup();
  }

  protected boolean initEmulator(File fileELF) {
    try {
      LoadableProgram program = new LoadableProgram(fileELF);
      program.load();
      platform = factory.newPlatform(1, program.getProgram());
      AtmelMicrocontroller cpu = (AtmelMicrocontroller) platform.getMicrocontroller();
      EEPROM = (EEPROM) cpu.getDevice("eeprom");
      AVRProperties avrProperties = (AVRProperties) cpu.getProperties();
      Simulator sim = cpu.getSimulator();
      interpreter = (AtmelInterpreter) sim.getInterpreter();
      memory = new AvrMoteMemory(program.getProgram().getSourceMapping(), avrProperties, interpreter);
    } catch (Exception e) {
      logger.fatal("Error when initializing Avora mote: " + e.getMessage(), e);
      return false;
    }
    return true;
  }

  public Platform getPlatform() {
    return platform;
  }

  /**
   * Abort current tick immediately.
   * May for example be called by a breakpoint handler.
   */
  public void stopNextInstruction() {
    stopNextInstruction = true;
  }

  private MoteInterfaceHandler createMoteInterfaceHandler() {
    return new MoteInterfaceHandler(this, getType().getMoteInterfaceClasses());
  }

  protected void initMote() {
    initEmulator(moteType.getContikiFirmwareFile());
    moteInterfaceHandler = createMoteInterfaceHandler();
  }

  public void setEEPROM(int address, int i) {
    byte[] eedata = EEPROM.getContent();
    eedata[address] = (byte) i;
  }

  public int getID() {
    return getInterfaces().getMoteID().getMoteID();
  }

  public MoteType getType() {
    return moteType;
  }
  public MoteMemory getMemory() {
    return memory;
  }
  public MoteInterfaceHandler getInterfaces() {
    return moteInterfaceHandler;
  }

  private long cyclesExecuted = 0;
  private long cyclesUntil = 0;
  public void execute(long t) {
    /* Wait until mote boots */
    if (moteInterfaceHandler.getClock().getTime() < 0) {
      scheduleNextWakeup(t - moteInterfaceHandler.getClock().getTime());
      return;
    }

    if (stopNextInstruction) {
      stopNextInstruction = false;
      throw new RuntimeException("Avrora requested simulation stop");
    }

    /* Execute one millisecond */
    cyclesUntil += this.getCPUFrequency()/1000;
    while (cyclesExecuted < cyclesUntil) {
      int nsteps = interpreter.step();
      if (nsteps > 0) {
        cyclesExecuted += nsteps;
      } else {
        logger.debug("halted?");
        try{Thread.sleep(200);}catch (Exception e){System.err.println("avoraMote: " + e.getMessage());}
      }
    }

    /* Schedule wakeup every millisecond */
    /* TODO Optimize next wakeup time */
    scheduleNextWakeup(t + Simulation.MILLISECOND);
  }

  @SuppressWarnings("unchecked")
  public boolean setConfigXML(Simulation simulation, Collection<Element> configXML, boolean visAvailable) {
    setSimulation(simulation);
    initEmulator(moteType.getContikiFirmwareFile());
    moteInterfaceHandler = createMoteInterfaceHandler();

    for (Element element: configXML) {
      String name = element.getName();

      if (name.equals("motetype_identifier")) {
        /* Ignored: handled by simulation */
      } else if (name.equals("interface_config")) {
        Class<? extends MoteInterface> moteInterfaceClass = simulation.getGUI().tryLoadClass(
            this, MoteInterface.class, element.getText().trim());

        if (moteInterfaceClass == null) {
          logger.fatal("Could not load mote interface class: " + element.getText().trim());
          return false;
        }

        MoteInterface moteInterface = getInterfaces().getInterfaceOfType(moteInterfaceClass);
        moteInterface.setConfigXML(element.getChildren(), visAvailable);
      }
    }

    /* Schedule us immediately */
    requestImmediateWakeup();
    return true;
  }

  public Collection<Element> getConfigXML() {
    ArrayList<Element> config = new ArrayList<Element>();
    Element element;

    /* Mote interfaces */
    for (MoteInterface moteInterface: getInterfaces().getInterfaces()) {
      element = new Element("interface_config");
      element.setText(moteInterface.getClass().getName());

      Collection<Element> interfaceXML = moteInterface.getConfigXML();
      if (interfaceXML != null) {
        element.addContent(interfaceXML);
        config.add(element);
      }
    }

    return config;
  }
}
