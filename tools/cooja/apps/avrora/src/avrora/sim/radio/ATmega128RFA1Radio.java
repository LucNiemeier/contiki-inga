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
 */

package avrora.sim.radio;

import avrora.sim.Simulator;
import avrora.sim.clock.Synchronizer;
import avrora.sim.mcu.*;
import avrora.sim.output.SimPrinter;
import avrora.sim.state.*;
import avrora.sim.AtmelInterpreter;
import cck.text.StringUtil;
import cck.util.Arithmetic;
import cck.util.Util;
import avrora.sim.FiniteStateMachine;
import avrora.sim.energy.Energy;
import cck.text.*;

import java.util.*;

/**
 * The <code>ATmega128RFA1Radio</code> implements a simulation of the internal
 * radio of the ATmeag128RFA1 chip.
 * Verbose printers for this class include "radio.rfa1"
 *
 * @author David A. Kopf
 */
public class ATmega128RFA1Radio implements Radio {
    private final static boolean DEBUG   = false;  //state changes, interrupts
    private final static boolean DEBUGV  = false;  //pin changes
    private final static boolean DEBUGRX = false;  //receiver
    private final static boolean DEBUGTX = false;  //transmitter
    private final static boolean DEBUGA  = false;  //ACKs
    private final static boolean DEBUGC  = false;  //CCAs
    private final static boolean DEBUGQ  = true;   //"should not happen" debugs
    
    //-- Radio states, confusingly contained in the TRX_STATUS register --------
    public  byte rf231Status = 0;
    private int wakeinserted = 0;
    private static final byte STATE_BUSY_RX      = 0x01;
    private static final byte STATE_BUSY_TX      = 0x02;
    private static final byte STATE_RX_ON        = 0x06; 
    private static final byte STATE_TRX_OFF      = 0x08;
    private static final byte STATE_PLL_ON       = 0x09;
    private static final byte STATE_SLEEP        = 0x0F;
    private static final byte STATE_BUSY_RX_AACK = 0x11;
    private static final byte STATE_BUSY_TX_ARET = 0x12;
    private static final byte STATE_RX_AACK_ON   = 0x16;
    private static final byte STATE_TX_ARET_ON   = 0x19;
    private static final byte STATE_TRANSITION   = 0x1F;

    //-- Radio commands, confusingly written to the TRX_STATE register ---------
    //-- More confusion arises from the presence of the upper TRAC_STATUS bits -
    private static final byte CMD_NOP            = 0x00;
    private static final byte CMD_TX_START       = 0x02;
    private static final byte CMD_FORCE_TRX_OFF  = 0x03; 
    private static final byte CMD_FORCE_PLL_ON   = 0x04;
    private static final byte CMD_RX_ON          = 0x06;
    private static final byte CMD_TRX_OFF        = 0x08;
    private static final byte CMD_TX_ON          = 0x09;
    private static final byte CMD_RX_AACK_ON     = 0x16;
    private static final byte CMD_TX_ARET_ON     = 0x19;

    //-- Register addresses ---------------------------------------------------
    // Registers that have no trigger action can be modified through RAM peek/poke.
    // So far, this is all but the TRX_STATE and TRXPR registers which need implementation
    // in the RADIO class through the RADIODevice interface to the MCU.
    private static final int TRX_STATUS   = 0x141;
    private static final int TRX_STATE    = 0x142;
    private static final int TRX_CTRL_0   = 0x143;
    private static final int TRX_CTRL_1   = 0x144;
    private static final int PHY_TX_PWR   = 0x145;
    private static final int PHY_RSSI     = 0x146;
    private static final int PHY_ED_LEVEL = 0x147;
    private static final int PHY_CC_CCA   = 0x148;
    private static final int CCA_THRES    = 0x149;
    private static final int IRQ_MASK     = 0x14E;
    private static final int IRQ_STATUS   = 0x14F;
    private static final int SHORT_ADDR_0 = 0x160;
    private static final int SHORT_ADDR_1 = 0x161;
    private static final int PAN_ID_0     = 0x162;
    private static final int PAN_ID_1     = 0x163;
    private static final int IEEE_ADDR_0  = 0x164;
    private static final int IEEE_ADDR_1  = 0x165;
    private static final int IEEE_ADDR_2  = 0x166;
    private static final int IEEE_ADDR_3  = 0x167;
    private static final int IEEE_ADDR_4  = 0x168;
    private static final int IEEE_ADDR_5  = 0x169;
    private static final int IEEE_ADDR_6  = 0x16A;
    private static final int IEEE_ADDR_7  = 0x16B;
    private static final int XAH_CTRL_0   = 0x16C;
    private static final int CSMA_SEED_1  = 0x16E;
    private static final int TST_RX_LENGTH= 0x17B;
    private static final int TRXFBST      = 0x180;

/*

        rl.addIOReg("TST_RX_LENGTH", 0x15B);

        rl.addIOReg("TST_CTRL_DIGI", 0x156);

        rl.addIOReg("CSMA_BE", 0x14F);
        rl.addIOReg("CSMA_SEED_1", 0x14E);
        rl.addIOReg("CSMA_SEED_0", 0x14D);
        rl.addIOReg("XAH_CTRL_0", 0x14C);
        rl.addIOReg("IEEE_ADDR_7", 0x14B);
        rl.addIOReg("IEEE_ADDR_6", 0x14A);
        rl.addIOReg("IEEE_ADDR_5", 0x149);
        rl.addIOReg("IEEE_ADDR_4", 0x148);
        rl.addIOReg("IEEE_ADDR_3", 0x147);
        rl.addIOReg("IEEE_ADDR_2", 0x146);
        rl.addIOReg("IEEE_ADDR_1", 0x145);
        rl.addIOReg("IEEE_ADDR_0", 0x144);
        rl.addIOReg("PAN_ID_1", 0x143);
        rl.addIOReg("PAN_ID_0", 0x142);
        rl.addIOReg("SHORT_ADDR_1", 0x141);
        rl.addIOReg("SHORT_ADDR_0", 0x140); 

        rl.addIOReg("MAN_ID_1", 0x13F);
        rl.addIOReg("MAN_ID_0", 0x13E);
        rl.addIOReg("VERSION_NUM", 0x13D);
        rl.addIOReg("PART_NUM", 0x13C);
        rl.addIOReg("PLL_DCU", 0x13B);
        rl.addIOReg("PLL_CF", 0x13A);

        rl.addIOReg("FTN_CTRL", 0x138);
        rl.addIOReg("XAH_CTRL_1", 0x137);

        rl.addIOReg("RX_SYN", 0x135);

        rl.addIOReg("XOSC_CTRL", 0x132);
        rl.addIOReg("BATMON", 0x131);
        rl.addIOReg("VREG_CTRL", 0x130);   

        rl.addIOReg("IRQ_STATUS", 0x12F);
        rl.addIOReg("IRQ_MASK", 0x12E);
        rl.addIOReg("ANT_DIV", 0x12D);
        rl.addIOReg("TRX_CTRL_2", 0x12C);
        rl.addIOReg("SFD_VALUE", 0x12B);
        rl.addIOReg("RX_CTRL", 0x12A);
        rl.addIOReg("CCA_THRES", 0x129);
        rl.addIOReg("PHY_CC_CCA", 0x128);
        rl.addIOReg("PHY_ED_LEVEL", 0x127);
        rl.addIOReg("PHY_RSSI", 0x126);
        rl.addIOReg("PHY_TX_PWR", 0x125);
        rl.addIOReg("TRX_CTRL_1", 0x124);
        rl.addIOReg("TRX_CTRL_0", 0x123);
        rl.addIOReg("TRX_STATE", 0x122, "TRAC_STATUS[2:0],TRX_CMD[4:0]");
        rl.addIOReg("TRX_STATUS", 0x121, "CCA_DONE, CCA_STATUS, TST_STATUS, TRX_STATUS[4:0]");

        rl.addIOReg("AES_KEY", 0x11F);
        rl.addIOReg("AES_STATE", 0x11E);
        rl.addIOReg("AES_STATUS", 0x11D);
        rl.addIOReg("AES_CTRL", 0x11C);

        rl.addIOReg("TRXPR", 0x119, "......,SLPTR,TRXRST");
*/

    //-- Other constants --------------------------------------------------
    private static final int NUM_REGISTERS = 0x40;
    private static final int XOSC_START_TIME = 1000;// oscillator start time

    //-- Simulation objects -----------------------------------------------
    protected final AtmelInterpreter interpreter;
    protected final Microcontroller mcu;
    protected final Simulator sim;

    //-- Radio state ------------------------------------------------------
    protected final int xfreq;
    protected double BERtotal = 0.0D;
    protected int BERcount = 0;
    protected boolean txactive = false,rxactive = false;
    protected boolean sendingAck, waitingAck, handledAck;

    protected byte frameRetries, csmaRetries;
    protected boolean lastCRCok;
    protected byte DSN;

    protected Medium medium;
    protected Transmitter transmitter;
    protected Receiver receiver;

    protected final SimPrinter printer;

    //Radio energy
    protected static final String[] allModeNames = CC2420Energy.allModeNames();
    protected static final int[][] ttm = FiniteStateMachine.buildSparseTTM(allModeNames.length, 0);
    protected final FiniteStateMachine stateMachine;

    /**
     * The constructor for the ATmega128RFA1Radio class creates a new instance internal
     * to the specified microcontroller with the given clock frequency.
     *
     * @param mcu   the microcontroller unit in which this radio resides
     * @param xfreq the clock frequency of this microcontroller
     */
    public ATmega128RFA1Radio(Microcontroller mcu, int xfreq) {
        // set up references to MCU and simulator
        this.mcu = mcu;
        this.sim = mcu.getSimulator();
        this.interpreter = (AtmelInterpreter)sim.getInterpreter();
        this.xfreq = xfreq;

        // create a private medium for this radio
        // the simulation may replace this later with a new one.
        setMedium(createMedium(null, null));

        //setup energy recording
        stateMachine = new FiniteStateMachine(sim.getClock(), CC2420Energy.startMode, allModeNames, ttm);
        new Energy("Radio", CC2420Energy.modeAmpere, stateMachine, sim.getEnergyControl());

        // get debugging channel.
        printer = sim.getPrinter("radio.rfa1");

        // reset all registers
        reset();
    }

    /**
     * The <code>getFiniteStateMachine()</code> method gets a reference to the finite state
     * machine that represents this radio's state. For example, there are states corresponding
     * to "on", "off", "transmitting", and "receiving". The state names and numbers will vary
     * by radio implementation. The <code>FiniteStateMachine</code> instance allows the user
     * to instrument the state transitions in order to gather information during simulation.
     * @return a reference to the finite state machine for this radio
     */
    public FiniteStateMachine getFiniteStateMachine() {
        return stateMachine;
    }

    public void reset() {
        if (DEBUG && printer != null) printer.println("RFA1: RESET");
  //      for (int cntr = 0; cntr < NUM_REGISTERS; cntr++) {
  //          resetRegister(cntr);
  //      }
        lastCRCok = false;
        rf231Status = STATE_TRX_OFF;
        txactive = rxactive = true;
        transmitter.shutdown();
        receiver.shutdown();
    }

    //ccaDelay fires after the cca or energy detect delay.
    protected class ccaDelay implements Simulator.Event {      
        public void fire() {
            //a write to PHY_ED_LEVEL will endless recursion
            //interpreter.writeDataByte(PHY_ED_LEVEL, (byte)((interpreter.getDataByte(PHY_RSSI) & 0x1f) * 3));                    
            //update cca done and cca_status
            boolean ccaBusy = (interpreter.getDataByte(PHY_RSSI) & 0x1f) > ((interpreter.getDataByte(CCA_THRES) & 0x0f) << 1);
 /*
            //TODO: Carrier sense
            switch (mode) {
                case 0: //Carrier sense OR energy above threshold
                case 1: //Energy above threshold
                case 2: //Carrier sense only
                case 3: //Carrier sense AND energy above threshold
                break;
            }
*/
            interpreter.writeDataByte(TRX_STATUS, (byte) (interpreter.getDataByte(TRX_STATUS) & 0x3f));
            if (ccaBusy) {
                interpreter.writeDataByte(TRX_STATUS, (byte) (0x80 | (interpreter.getDataByte(TRX_STATUS) & 0x3f)));
            } else {
                interpreter.writeDataByte(TRX_STATUS, (byte) (0xC0 | (interpreter.getDataByte(TRX_STATUS) & 0x3f)));
            }
            if (ccaBusy) if (DEBUGC && printer !=null) printer.println("RFA1: TRX24_CCA_ED_DONE interrupt, ED " + interpreter.getDataByte(PHY_ED_LEVEL)+" busy ="+ccaBusy);
            //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 CCA_ED_DONE", true);
            interpreter.setPosted(62, true);
        }
    }
    protected ccaDelay ccaDelayEvent = new ccaDelay();

    /**
     * The <code>newCommand()</code> method is called after a write to the TRX_STATE register.
     * It alters the radio state according to the rules for possible state transitions, sets
     * the TRX_STATE register accordingly, and returns the command + TRAC_STATUS bits
     * for writing to the TRX_STATE register. TRAC_STATUS is only used in extended operation.
     * A write to the PHY_ED_LEVEL register is trapped with a special code. It initiates a CCA
     * in extended mode.
     * TODO: write to PHY_CC_CCA upper bit should initiate CCA in normal mode.
     *
     * @param byte the command
     * @return byte the command+TRAC_STATUS to be written to the status register
     */
    public byte newCommand(byte val) {
        //TODO:check for invalid state transitions
        //TODO:proper transition times through STATE_TRANSITION_IN_PROGRESS
        switch (val) {
            case (byte) 0x42:
                if (DEBUGC && printer!=null) printer.println("RFA1: CCA_REQUEST, extended mode PHYRSSI = " +interpreter.getDataByte(PHY_RSSI) + " CCATHRES " + (interpreter.getDataByte(CCA_THRES) & 0x0f));
                //clear status and done bit
                interpreter.writeDataByte(TRX_STATUS, (byte) (interpreter.getDataByte(TRX_STATUS) & 0x3f));
                //See data sheet for the various possibilities. In extended mode rx done is set immediately with no CCA.
                //In BUSY_RX carrier sense only the result is available immediately.
                //wait 140 usec (8.75 symbol periods)
                receiver.clock.insertEvent(ccaDelayEvent, 875*receiver.cyclesPerByte/200);
                
                //Put the result into the PHY_ED_LEVEL register
                //return in val for now to avoid recursion
                val = (byte)((interpreter.getDataByte(PHY_RSSI) & 0x1f) * 3);
                //return the ED result for the super to write to the register
                return(val);

            case CMD_NOP:
                if (DEBUG && printer != null) printer.println("RFA1: NOP");
                break;
            case CMD_TX_START:
                if (DEBUG && printer != null) printer.println("RFA1: TX_START");
                rf231Status = STATE_BUSY_TX;
                sendingAck = false;
                if (rxactive) receiver.shutdown();
                if (!txactive) transmitter.startup();
                break;
            case CMD_FORCE_TRX_OFF:
                if (DEBUG && printer != null) printer.println("RFA1: FORCE_TRX_OFF");
                rf231Status = STATE_TRX_OFF;
                if (txactive) transmitter.shutdown();
                if (rxactive) receiver.shutdown();
                break;
            case CMD_FORCE_PLL_ON:
                if (DEBUG && printer != null) printer.println("RFA1: FORCE_PLL_ON");
                rf231Status = STATE_PLL_ON;     
                break;
            case CMD_RX_ON:
                if (DEBUG && printer != null) printer.println("RFA1: RX_ON");
                rf231Status = STATE_RX_ON;
                if (txactive) transmitter.shutdown();
                if (!rxactive) receiver.startup();
                break;
            case CMD_TRX_OFF:
                if (DEBUG && printer != null) printer.println("RFA1: TRX_OFF");
                rf231Status = STATE_TRX_OFF;
                if (txactive) transmitter.shutdown();
                if (rxactive) receiver.shutdown();
                break;
            case CMD_TX_ON:
                if (DEBUG && printer != null) printer.println("RFA1: PLL_ON");
                rf231Status = STATE_PLL_ON;
                break;
            case CMD_RX_AACK_ON:
                if (DEBUG && printer != null) printer.println("RFA1: RX_AACK_ON");
                rf231Status = STATE_RX_AACK_ON;
                //TODO: Set TRAC_STATUS to INVALID for potential SUCCESS_WAIT_FOR_ACK return in slotted mode
             // val|=0xE0;
                if (waitingAck) printer.println("rx on command resets waitingack");
                waitingAck = false;
                if (txactive) transmitter.shutdown();
                if (!rxactive) receiver.startup();
                break;
            case CMD_TX_ARET_ON:
                if (DEBUG && printer != null) printer.println("RFA1: TX_ARET_ON");
                rf231Status = STATE_BUSY_TX_ARET;
                sendingAck = false;
                frameRetries = (byte) ((interpreter.getDataByte(XAH_CTRL_0) & 0xF0) >> 4);
                csmaRetries  = (byte) ((interpreter.getDataByte(XAH_CTRL_0) & 0x0E) >> 1);
                if (DEBUGC && printer!=null) printer.println("RFA1: Frame, csma retries = " + frameRetries + " " + csmaRetries);
                //slottedOperation = (registers[XAH_CTRL_0] & 0x01) != 0; //TODO: slotted operation
                //Set TRAC_STATUS to INVALID
                val|=0xE0;
                // Transmission starts on the rising edge of SLPTR
                if (rxactive) receiver.shutdown();
                if (!txactive) transmitter.startup();             
                break;
            default:
                if (printer != null) printer.println("RFA1: Invalid TRX_CMD, treat as NOP" + val);
                break;
        }
        interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status));
        return val;
    }

    //wakupDelay fires after transition from SLEEP or RESET to TRX_OFF. The nominal delay is 240 usec from SLEEP or
    //32 usec after RESET, but board capacitance can delay the oscillator startup to as much as 1000 usec.
    protected class wakeupDelay implements Simulator.Event {      
        public void fire() {
            rf231Status = STATE_TRX_OFF;
            if (wakeinserted !=1) System.out.println("wake > 1 when fired");
            wakeinserted--;
            interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status));
            if (DEBUG && printer !=null) printer.println("RFA1: TRX24_AWAKE interrupt");
            //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 AWAKE", true);
            interpreter.setPosted(65, true);
        }
    }
    protected wakeupDelay wakeupDelayEvent = new wakeupDelay();

    /**
     * The <code>pinChangeSLP()</code> method indicates a change in the multifunction SLPTR pin,
     * or for the ATmega128rfa1, the SLPTR bit in the TRXPR register.
     *
     * @param val the new pin status ( 0 = low)
     * @return the new radio state
     */
    public void pinChangeSLP(byte val) {
        if (DEBUG && printer != null) printer.println("RFA1: SLP pin change to " + val +", state is "+ rf231Status);     
        if (val != 0) {  //pin was raised
            switch (rf231Status) {
                //Sleep if off, initiate transmission if tx on
                case STATE_TRX_OFF:
                    if (rxactive) receiver.shutdown();
                    if (txactive) transmitter.shutdown();
                    stateMachine.transition(0);//change to off state
                    rf231Status = STATE_SLEEP;
                    break;
                case STATE_PLL_ON:
                    rf231Status = STATE_BUSY_TX;
                    break;
                case STATE_BUSY_TX_ARET:
           //     case STATE_TX_ARET_ON:
            //        rf231Status = STATE_BUSY_TX_ARET;
            //    case STATE_BUSY_TX_ARET:
             //        if (!txactive) transmitter.startup();
                     break;
                default:
                System.out.println("RFA1: should not be in this state " + rf231Status);
                    //dont know what to do here
                    break;
            }
        } else {    //pin was lowered
            switch (rf231Status) {
                //Go to idle if sleeping
                case STATE_SLEEP:
                    if (rxactive) receiver.shutdown();
                    if (txactive) transmitter.shutdown();
                    //TODO:idle state before or after wake?
                    stateMachine.transition(3);//change to on state
                    //wait 384 usec (24 symbol periods, 12 bytes). Datasheet says 240 typical.
                    if (wakeinserted != 0) System.out.println("wake already inserted");
                    wakeinserted++;
                    transmitter.clock.insertEvent(wakeupDelayEvent, 12*transmitter.cyclesPerByte);
                    break;
                default:
                    if (DEBUGQ && printer!=null) printer.println("RFA: SLP pin lowered but not sleeping, state = " + rf231Status);
                    //dont know what to do here
                    break;
            }
        }
        interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status));
    }
    /**
     * The <code>resetRegister()</code> method resets the specified register's value
     * to its default.
     *
     * @param addr the address of the register to reset
     */
    void resetRegister(int addr) {
    /*
        char val = 0x0000;
        switch (addr) {
            case MAIN:
                val = 0xf800;
                break;
         }
        registers[addr] = val;
        */
    }

    /**
     * The <code>computeStatus()</code> method computes the status byte of the radio.
     */
    void computeStatus() {
                 if (DEBUG && printer!=null) printer.println("RFA1: computestatus");
        // do nothing.
    }

    public Simulator getSimulator() {
        return sim;
    }

    public double getPower() {
        //return power in dBm
        double power=0;
        switch (interpreter.getDataByte(PHY_TX_PWR)&0x0f) {
            case  0:power=  3.0;break;
            case  1:power=  2.8;break;
            case  2:power=  2.3;break;
            case  3:power=  1.8;break;
            case  4:power=  1.3;break;
            case  5:power=  0.7;break;
//          case  6:power=  0.0;break;
            case  7:power= -1.0;break;
            case  8:power= -2.0;break;
            case  9:power= -3.0;break;
            case 10:power= -4.0;break;
            case 11:power= -5.0;break;
            case 12:power= -7.0;break;
            case 13:power= -9.0;break;
            case 14:power=-12.0;break;
            case 15:power=-17.0;break;
        }
        if (DEBUGV && printer!=null) printer.println("RFA1: getPower returns "+ power + " dBm");
        return power;
    }

    public int getChannel() {
        return interpreter.getDataByte(PHY_CC_CCA) & 0x1F;
    }

    public double getFrequency() {
 //       int channel=interpreter.getDataByte(PHY_CC_CCA) & 0x1F;
        double frequency = 2405 + 5*(getChannel() - 11 );
//      if (printer != null) printer.println("RFA1: getFrequency returns "+frequency+" MHz (channel " + channel + ")");
        return frequency;
    }

    public class ClearChannelAssessor implements BooleanView {
        public void setValue(boolean val) {
        //     if (printer != null) printer.println("RFA1: set clearchannel");
             // ignore writes.
        }

        public boolean getValue() {
          if (DEBUGRX && printer!=null) printer.println("RFA1: getValue?");
          return true;
        }
    }

    private static final int TX_IN_PREAMBLE = 0;
    private static final int TX_SFD = 1;
    private static final int TX_LENGTH = 3;
    private static final int TX_IN_PACKET = 4;
    private static final int TX_CRC_1 = 5;
    private static final int TX_CRC_2 = 6;
    private static final int TX_END = 7;
    private static final int TX_WAIT = 8;

    /* ------------------------------------------CRC Computation --------------------------------*/
    protected static final int[] reverse_bits = {
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff};
/*  //TODO: which is faster
    int crcAccumulatet(int crc, byte val) {
	    crc = ((crc >> 8) & 0xff) | (crc << 8) & 0xffff;
	    crc ^= (val & 0xff);
	    crc ^= (crc & 0xff) >> 4;
	    crc ^= (crc << 12) & 0xffff;
	    crc ^= (crc & 0xff) << 5;
	    crc = crc & 0xffff;
        return crc;
    } 
*/
    short crcAccumulate(short crc, byte val) {
        int i = 8;
        crc = (short) (crc ^ val << 8);
        do {
            if ((crc & 0x8000) != 0) crc = (short) (crc << 1 ^ 0x1021);
            else crc = (short) (crc << 1);
        } while (--i > 0);
        return crc;
    }


    /* ------------------------------------------Transmitter-------------------------------------*/
    public class Transmitter extends Medium.Transmitter {
        private static final int TX_IN_PREAMBLE = 0;
        private static final int TX_SFD = 1;
        private static final int TX_LENGTH = 3;
        private static final int TX_IN_PACKET = 4;
        private static final int TX_CRC_1 = 5;
        private static final int TX_CRC_2 = 6;
        private static final int TX_END = 7;
        private static final int TX_WAIT = 8;

        protected int state, counter, length, TRXFBptr;
        protected short crc;
        protected boolean waitForAck;

        //ackTimeOut fires 54 symbol periods (864 usec) after tx transition to rx to receive an ack
        protected class ackTimeOut implements Simulator.Event {      
            public void fire() {
            //  if (activated) {
                    waitingAck = false;
                    if (!handledAck) {
                        //Ack not received, abort or retry
                        handledAck = true;
                        if (false && frameRetries != 0) {
                            //Autoretry
                            System.out.println("tx_aret #" + frameRetries);
                            frameRetries--;
                             if (rxactive) receiver.shutdown();
                             if (!txactive) transmitter.startup();
                        } else {
                           //Set TRAC_STATUS to no ack and return to TX_ARET_ON
                        //    interpreter.writeDataByte(TRX_STATE, (byte) (0xA0 | (interpreter.getDataByte(TRX_STATE) & 0x1F)));
                            interpreter.writeDataByte(TRX_STATE, (byte)0xA0);
                            rf231Status = STATE_TX_ARET_ON;
                            interpreter.writeDataByte(TRX_STATUS, rf231Status);
                            state = TX_WAIT;//???
                                                  //      if (txactive) printer.println("tx active after ack timeout");
                            if (!rxactive)  printer.println("rx1 not active before shutdown, status is " + rf231Status);
                            receiver.shutdown();
                            if (DEBUGA & printer !=null) printer.println("RFA1: TRX24 TX_END interrupt, no ack");
                            //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 TX_END"), true);
                            interpreter.setPosted(64, true);
                        }
                    }
            //  }
            }
        }
        protected ackTimeOut ackTimeOutEvent = new ackTimeOut();

        public Transmitter(Medium m) {
            super(m, sim.getClock());
        }

        public byte nextByte() {
            if (rxactive) printer.println("ractive while transmitting");
            if (!txactive) printer.println("tx not active while transmitting");
            byte val = 0;
            switch (state) {
                case TX_IN_PREAMBLE:
                    //always 4 octects of zero
                    if (++counter >= 4) state = TX_SFD;
                    break;
                case TX_SFD:
                    // TODO:read SFD_VALUE
                  //  val = (byte) 0xA7;
                    val = (byte) 0x7A;  //sky compatibility
                    state = TX_LENGTH;
                    break;
                case TX_LENGTH:
                    if (sendingAck) {
                        length = 5;
                    } else {//data frame
                        // length is the first byte in the RAM buffer
                        TRXFBptr = 0x180;
                        length =  interpreter.getDataByte(TRXFBptr++);
                    }
                    if (DEBUGTX && printer!=null) printer.println("RFA1: Tx frame length " + length);
                    state = TX_IN_PACKET;
                    counter = 0;
                    crc = 0;
                    val = (byte) length;
                    break;
                case TX_IN_PACKET:
                    if (sendingAck) {
                        switch (counter) {
                            case 0://FCF_low
                                val = 2;
                             // val = 0x12;  //TODO:handle pending flag
                                 break;
                            case 1://FCF_hi
                                val = 0;
                                break;
                            case 2://Sequence number
                                val = DSN;
                                waitForAck = false;
                                state = TX_END;
                                break;
                        }
                        counter++;
                    } else {
                        val = interpreter.getDataByte(TRXFBptr++);
                        counter++;
                        if (counter == 1) {
                            //check the ack request bit in the FCF
                            waitForAck = (val & 0x20) != 0;
                            if (waitForAck) if (DEBUGA && printer!=null) printer.println("RFA1: TX will wait for ack");
                        }
                    }
                    //Calculate CRC and switch state if necessary
                   // if ((interpreter.getDataByte(TRX_CTRL_1) & 0x20) !=0) {  //Test TX_AUTO_CRC_ON bit
                    if (true) {
                        // accumulate CRC if enabled.
                        crc = crcAccumulate(crc, (byte) reverse_bits[((int) val) & 0xff]);
                        if (counter >= length - 2) {
                            // switch to CRC state if when 2 bytes remain.
                            state = TX_CRC_1;
                        }
                    } else if (counter >= length) {
                        // AUTOCRC not enabled, switch to packet end mode when done.
                        state = TX_END;
                    }
                    break;
                case TX_CRC_1:
                    state = TX_CRC_2;
                    val = Arithmetic.high(crc);
                    val = (byte) reverse_bits[((int) val) & 0xff];
                    break;
                case TX_CRC_2:
                    val = Arithmetic.low(crc);
                    val = (byte) reverse_bits[((int) val) & 0xff];
                    state = TX_END;
                    break;
            }
            if (DEBUGTX && printer != null) printer.println("RFA1 " + StringUtil.to0xHex(val, 2) + " --------> ");
            // common handling of end of transmission
            if (state == TX_END) {
                //Set the TRAC status bits in the TRX_STATE register
                //0 success 1 pending 2 waitforack 3 accessfail 5 noack 7 invalid
                if (waitForAck && (rf231Status == STATE_BUSY_TX_ARET)) {
                    //Show waiting for ack, and switch to rx mode
                    waitingAck = true;
                    handledAck = false;
               //     interpreter.writeDataByte(TRX_STATE, (byte) (0x40 | (interpreter.getDataByte(TRX_STATE) & 0x1F)));
                  //  interpreter.writeDataByte(TRX_STATE, (byte) 0x40);//wrong this is for rxaack state
                    //wait 54 symbol periods (864 usec, 27 bytes) to receive the ack  
                    clock.insertEvent(ackTimeOutEvent, 27*cyclesPerByte);
                    if (!txactive) printer.println("tx not active at txend");
                    transmitter.shutdown();
                    if (rxactive) printer.println("rx active at txend");
                    receiver.startup();
                    state = TX_WAIT;
                    return val;
                } else {
                    rf231Status = STATE_PLL_ON;
                    interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status));
                    interpreter.writeDataByte(TRX_STATE, (byte) 0);
                    transmitter.shutdown();
                }
              //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 TX_END"), true);
                interpreter.setPosted(64, true);
                if (DEBUGTX && printer != null) printer.println("RFA1: TX_END interrupt");
                // transmitter stays in this state until it is really switched off
                state = TX_WAIT;
            }
            return val;
        }

        void startup() {
            if (!txactive) {
                txactive = true;
                // the PLL lock time is implemented as the leadCycles in Medium
                stateMachine.transition(6 + 15 - (interpreter.getDataByte(PHY_TX_PWR)&0x0f));//change to Tx(power) state
                state = TX_IN_PREAMBLE;
                counter = 0;
                beginTransmit(getPower(),getFrequency());
                if (DEBUGTX && printer!=null) printer.println("RFA1: TX Startup");
            } else printer.println("tx startup while active");
        }

        void shutdown() {
            if (txactive) {
                txactive = false;
                endTransmit();
                if (DEBUGTX && printer!=null) printer.println("RFA1: TX shutdown");
                stateMachine.transition(3);//change to RX TODO:FIX this
            }else printer.println("tx shutdown while not active");
        }
    }

    /* ------------------------------------------Receiver------------------------------------*/
    private static final int RECV_SFD_SCAN = 0;
    private static final int RECV_SFD_MATCHED_1 = 1;
    private static final int RECV_SFD_MATCHED_2 = 2;
    private static final int RECV_IN_PACKET = 3;
    private static final int RECV_CRC_1 = 4;
    private static final int RECV_CRC_2 = 5;
    private static final int RECV_END_STATE = 6;
    private static final int RECV_OVERFLOW = 7;
    private static final int RECV_WAIT = 8;

    //Address recognition variables
    protected byte[] PANId = {0, 0};
    protected byte[] macPANId = {0, 0};
    protected byte[] ShortAddr = {0, 0};
    protected byte[] macShortAddr = {0, 0};
    protected static final byte[] SHORT_BROADCAST_ADDR = {-1, -1};
    protected byte[] LongAdr = {-1, -1, -1, -1, -1, -1, -1, -1};
    protected byte[] IEEEAdr = {-1, -1, -1, -1, -1, -1, -1, -1};
    protected static final byte[] LONG_BROADCAST_ADDR = {-1, -1, -1, -1, -1, -1, -1, -1};

    public class Receiver extends Medium.Receiver {
        protected int state;
        protected int RXFBptr;
        protected int counter;
        protected int length;
        protected short crc;
        protected byte crcLow;

        public Receiver(Medium m) {
            super(m, sim.getClock());
        }

        public byte nextByte(boolean lock, byte b) {
            //If we got switched off while receiving just return
            if (!rxactive) return(0);
            
            if (state == RECV_END_STATE) {
                state = RECV_SFD_SCAN; // to prevent loops when calling shutdown/endReceive
                // packet ended before
                if (sendingAck && lastCRCok) { //send ack?
                    if (DEBUGA && printer!=null) printer.println("RFA1: Send Ack");
                    receiver.shutdown();
                    transmitter.startup();
                } else {
                    if (lock) {
                        // the medium is still locked, so there could be more packets!
                        if (DEBUGRX && printer!=null) printer.println("RFA1: still locked");
                        // fire the probes manually
                        if (probeList != null) {
                            System.out.println("probeList is not null!");
                            probeList.fireAfterReceiveEnd(Receiver.this);
                        }
                    }
                }
                return b;
            }
              
            if (!lock) {
                if (DEBUGRX && printer != null) printer.println("RF231 locklost, state= "+state);
                // the transmission lock has been lost
                switch (state) {
                    case RECV_SFD_MATCHED_2:
                    case RECV_IN_PACKET:
                    case RECV_CRC_1:
                    case RECV_CRC_2:
                         if (DEBUGRX && printer != null) printer.println("RFA1: PLL_UNLOCK Interrupt");
                         //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 PLL_UNLOCK"), true);
                         interpreter.setPosted(58, true);
                        //packet lost in middle -> drop frame
                        // fall through
                    
                    case RECV_SFD_MATCHED_1: // packet has just started
                        if (DEBUGRX && printer != null) printer.println("RF231 packet started");
                        state = RECV_SFD_SCAN;
                        //rx start invalidates CRC and ED register
                        interpreter.writeDataByte(PHY_RSSI, (byte) (interpreter.getDataByte(PHY_RSSI) & 0x7f)); //Clear RX_CRC_VALID
                        interpreter.writeDataByte(PHY_ED_LEVEL, (byte) 0xff);
                        //switch to busy state unless waiting for ack in BUSY_TX_ARET
                        if (rf231Status == STATE_RX_AACK_ON) {
                            rf231Status = STATE_BUSY_RX_AACK;
                        } else if (rf231Status == STATE_RX_ON) {
                        printer.println("setting busyrx state here!");
                            rf231Status = STATE_BUSY_RX;
                        }
                        interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status | (interpreter.getDataByte(TRX_STATUS) & 0xE0)));
                        //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 RX_START"), true);
                        if (DEBUGRX & printer !=null) printer.println("RFA1: TRX24 RX_START interrupt");
                        interpreter.setPosted(60, true);
                        break;
                    default:
                        if (DEBUGRX && printer!=null) printer.println("RFA1: RX badstate " + state);
                        break;
                }
                return b;
            }
              if (txactive)  {
                printer.println("txactive while receiving5 " + state);
                transmitter.shutdown();
            }          
            if (DEBUGRX && printer != null) printer.println("RFA1 <======== " + StringUtil.to0xHex(b, 2));
            switch (state) {
                case RECV_SFD_MATCHED_1:
      //              if (b == (byte) 0xA7) {
                    if (b == (byte) 0x7A) {  //for sky emulation compatibility
                    // check against the second byte of the SYNCWORD register.
                        state = RECV_SFD_MATCHED_2;
                        //If waiting on ack in BUSY_TX_ARET don't make any status changes
                        if (waitingAck) break;
                        if (rf231Status == STATE_BUSY_TX_ARET) printer.println("busy txaret but not waiting for ack");
                        if (rf231Status== STATE_TX_ARET_ON) printer.println("txaret but not waiting for ack");
                        //rx start invalidates CRC and ED register
                        interpreter.writeDataByte(PHY_RSSI,(byte)(interpreter.getDataByte(PHY_RSSI) & 0x7f)); //Clear RX_CRC_VALID
                        interpreter.writeDataByte(PHY_ED_LEVEL, (byte)0xff);
                        if (rf231Status == STATE_RX_AACK_ON) {
                            rf231Status = STATE_BUSY_RX_AACK;
                        } else if (rf231Status == STATE_RX_ON) {
                            rf231Status = STATE_BUSY_RX;
                        } else if (rf231Status == STATE_BUSY_RX_AACK) {
                            printer.println("already in BUSY_RX_AACK on, probably a missed ack");
                        } else if (rf231Status == STATE_BUSY_RX) {
                            printer.println("already in BUSY_RX, should not be");
                        } else {
                            printer.println("not in a receive state! " + rf231Status + " " + interpreter.getDataByte(TRX_STATUS));
                            if (rf231Status == STATE_SLEEP) {
                                printer.println("RF231: Sleep while receiving");
                                //we were turned off while receiving
                                return 0;
                            }
                            //hmmm whats going on
                            printer.println("rxactive = " + rxactive + "   txactive = " + txactive);
                            return 0;
                        }
                        interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status | (interpreter.getDataByte(TRX_STATUS) & 0xE0)));
                        if (DEBUGRX && printer != null) printer.println("RFA1: RX_START interrupt");
                        //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 RX_START"), true);
                        interpreter.setPosted(60, true);
                        break;
                    }
                    // fallthrough if we failed to match the second byte
                    // and try to match the first byte again.
                case RECV_SFD_SCAN:
                    // check against the first byte of the SYNCWORD register.
                    if (b == (byte) 0x00) {
                        state = RECV_SFD_MATCHED_1;
                    } else {
                        state = RECV_SFD_SCAN;
                    }
                    break;

                case RECV_SFD_MATCHED_2:
                    // SFD matched. read the length from the next byte.
                    length = b & 0x7f;
                    if (length == 0) {  // ignore frames with 0 length
                        state = RECV_SFD_SCAN;
                        break;
                    }
                    // Store length in TXT_RX_LENGTH register - TODO:for acks?
                    interpreter.writeDataByte(TST_RX_LENGTH, (byte) length);
                    // Start transferring bytes to RAM buffer
          //          RXFBptr = 0x180;
                    counter = 0;
                    state = RECV_IN_PACKET;
                    if (waitingAck) {
                        if (length != 5) {
                         //not an ack, show failure and abort rx
                            if (DEBUGA && printer!=null) printer.println("RFA1: Expecting ack, got something else " + rf231Status);
                            handledAck = true;
                            //Status goes from BUSY_TX_ARET to TX_ARET_ON
                            rf231Status = STATE_TX_ARET_ON;
                            //TODO:should upper bits change?
                            interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status | (interpreter.getDataByte(TRX_STATUS) & 0xE0)));
                            //Set TRAC_STATUS bits to failure
                        //    interpreter.writeDataByte(TRX_STATE, (byte) (0xA0 | (interpreter.getDataByte(TRX_STATE) & 0x1F)));
                             interpreter.writeDataByte(TRX_STATE, (byte) 0xA0); 
                            state = RECV_SFD_SCAN;
                            //Post the TRX_END interrupt
                           //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 TX_END"), true);
                            interpreter.setPosted(64, true);
                            if (!rxactive) {
                                printer.println("rx2 not active before shutdown");
                                return(0);
                            }
                            receiver.shutdown();
                        }
                    } else {
                        // Start transferring bytes to RAM buffer
                        RXFBptr = 0x180;
                        crc = 0;                   
                        //Update the energy detect register
                        //This should have been an average rssi over the previous 8 symbols
                        //but basically is 3 times PHY_RSSI
                        interpreter.writeDataByte(PHY_ED_LEVEL, (byte)((interpreter.getDataByte(PHY_RSSI) & 0x1f) * 3));
                    }
                    break;

                case RECV_IN_PACKET:
                    // we are in the body of the packet.
                    counter++;
                    if (waitingAck) {
                        if (handledAck) {
                            printer.println("waiting for ack but handled during reception");
                        }
                        //show success after complete ack reception
                        //TODO: verify checksum
                        if (counter == 5) {
                            if (DEBUGA && printer!=null) printer.println("RFA1: Got ack");
                            handledAck = true;
                            rf231Status = STATE_TX_ARET_ON;
                            interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status | (interpreter.getDataByte(TRX_STATUS) & 0xE0)));
                           // interpreter.writeDataByte(TRX_STATE, (byte) (interpreter.getDataByte(TRX_STATE) & 0x1F));  
                            interpreter.writeDataByte(TRX_STATE, (byte) 0);                               
                            if (!rxactive) printer.println("rfa1 rx3 not active before shutdown");
                            if (txactive) printer.println("rfa1 tx active before startup");
                            receiver.shutdown();
                          //  transmitter.startup();
                           //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 TX_END"), true);
                            if (DEBUGA & printer !=null) printer.println("RFA1: TRX24 TX_END interrupt, ack");
                            interpreter.setPosted(64, true);
                            return(b);
                        } else if (counter > 5) {
                            if (DEBUGQ && printer!=null) printer.println("RFA1: can an not get here");
                        }
                        break;
                    }
                    interpreter.writeDataByte(RXFBptr++, b);

                    //Address Recognition and sequence number
                    //TODO:handle promiscuous mode
                    if (counter <= 13) {
                        // address match enabled only in RA_AACK mode TODO: should be BUSY_RX_AACK
                        boolean satisfied = matchAddress(b, counter);
                        if (!satisfied) {
                            if (DEBUGRX && printer!=null) printer.println("RFA1: No address match");
                            //reject frame
                            //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 RX_END"), true);
                            // wait for end of packet
                            state = RECV_WAIT;
                            break;
                        }
                        /*
                        if (rf231Status == STATE_RX_AACK_ON) {
                            // is AACK_I_AM_COORD set?
                            if ((registers[CSMA_SEED_1] & 0x04) == 0) {
                                if (!satisfied) {
                                    //reject frame
                                    if (DEBUG && (printer!=null)) printer.println("Dropped, no address match");   
                                    // wait for end of packet
                                    state = RECV_WAIT;
                                    break;
                                } else if (counter == 13) { //TODO: use the correct number based on short/long address
                                    // set the interrupt bit in the status register if interrupts or polling is enabled
                                    if ((registers[IRQ_MASK] & 0x20) !=0) { //interrupt is enabled
                                        registers[IRQ_STATUS] |= 0x20;
                                        if (RF231_interrupt > 0) interpreter.setPosted(RF231_interrupt, true);
                                        if (DEBUG && printer!=null) printer.println("RFA1: MASK_AMI interrupt");
                                    } else if ((registers[TRX_CTRL_1] & 0x02) == 1) { //polling enabled
                                        registers[IRQ_STATUS] |= 0x20;
                                    }
                                }
                            }
                        }
                        */
                    }
                
                    // no overflow occurred and address ok
                 //   if (autoCRC.getValue()) {
                    if (true) {
                        crc = crcAccumulate(crc, (byte) reverse_bits[((int) b) & 0xff]);
                        if (counter == length - 2) {
                            // transition to receiving the CRC.
                            state = RECV_CRC_1;
                        }
                    } else if (counter == length) {
                        // no AUTOCRC, but reached end of packet.
                        clearBER();  // will otherwise be done by getCorrelation() in state RECV_CRC_2
                        lastCRCok = false;
                        state = RECV_END_STATE;
                    }

                    break;
                case RECV_CRC_1:
                    interpreter.writeDataByte(RXFBptr++, b);
                    crcLow = b;
                    state = RECV_CRC_2;
                    break;
                
                case RECV_CRC_2:
                    state = RECV_END_STATE; 
                    interpreter.writeDataByte(RXFBptr++, b);                    
                    crcLow = (byte)reverse_bits[((int) crcLow) & 0xff]; 
                    b = (byte)reverse_bits[((int) b) & 0xff];
                    short crcResult = (short) Arithmetic.word(b, crcLow);
                    //LQI is written in this position
                    b = (byte) ((byte)getCorrelation() & 0x7f);
                    if (crcResult == crc) {
                        b |= 0x80;  //TODO: should this really increase the LQI
                        lastCRCok = true;
                    } else {
                        // TODO: reject frame if not promiscuous mode
                        // reset ACK flags set by the SACK/SACKPEND commands since ACK is only sent when CRC is valid
                        lastCRCok = false;
                        if (DEBUGQ && printer!=null) printer.println("RFA1: CRC received " + StringUtil.to0xHex(crcResult, 4) + " calculated " + StringUtil.to0xHex(crc, 4));
                    }
            
                    interpreter.writeDataByte(RXFBptr++, b);
                    if (DEBUGRX && printer !=null) printer.println("RFA1: RX_END interrupt");
                    //interpreter.setPosted(mcu.getProperties().getInterrupt("TRX24 RX_END"), true);
                    interpreter.setPosted(61, true);

                   if (rf231Status == STATE_BUSY_TX_ARET) {
                            printer.println(" STATE_BUSY_TX_ARET here! " + rf231Status + " " + interpreter.getDataByte(TRX_STATUS));
                    } else if (rf231Status == STATE_TX_ARET_ON) {
                            System.out.println("TX_ARET state in receiver here");
                    }
                    if (lastCRCok && (rf231Status == STATE_BUSY_RX_AACK) && (interpreter.getDataByte(0x180) & 0x20) == 0x20) {
                        //send ack if we are not receiving ack frame
                        if ((interpreter.getDataByte(CSMA_SEED_1) & 0x10) == 0) {
                            if ((interpreter.getDataByte(0x180) & 0x07) != 2) {
                                //should check if link local destination?
                                if (DEBUGA && printer!=null) printer.println("RFA1: Ack requested");
                                sendingAck = true;
                                handledAck = false;
                            }
                        }
                    } else {
                        if (DEBUGA && printer!=null) {
                            if (!lastCRCok) printer.println("RFA1: No ack, CRC failed");
                            if (rf231Status != STATE_BUSY_RX_AACK) printer.println("RFA: status no longer rxaack: "+ rf231Status);
                        }
                        if (rf231Status == STATE_BUSY_RX_AACK) {
                            rf231Status = STATE_RX_AACK_ON;
                        } else if (rf231Status == STATE_BUSY_RX) {
                               rf231Status = STATE_RX_AACK_ON;                      
                            //in a cca possibly?
                        } else {
                         //   rf231Status = STATE_RX_ON;
                            rf231Status = STATE_RX_AACK_ON;
                        }
                        interpreter.writeDataByte(TRX_STATUS, (byte) (rf231Status | (interpreter.getDataByte(TRX_STATUS) & 0xE0)));
                    }
                    break;

                case RECV_OVERFLOW:
                    // do nothing. we have encountered an overflow.
                    break;
                case RECV_WAIT:
                    // just wait for the end of the packet
                    if (++counter == length) {
                        clearBER();  // will otherwise be done by getCorrelation()
                        state = RECV_SFD_SCAN;
                     //   SendAck = SENDACK_NONE;  // just in case we received SACK(PEND) in the meantime
                    }
                    break;
                default:
                    if (DEBUGQ && printer!=null) printer.println("RFA1: Unknown state in receiver");
                    break;
            }
            return b;
        }
      
        private boolean matchAddress(byte b, int counter) {
            if (counter > 1 && (interpreter.getDataByte(0x180) & 0x04) == 4) {
                // no further address decoding is done for reserved frames
                return true;
            }
            switch (counter) {
                case 1://frame type subfield contents an illegal frame type?
                    if ((interpreter.getDataByte(0x180) & 0x04) == 4)
                        return false;
                    break;
                case 3://Sequence number
                    if ((interpreter.getDataByte(0x180) & 0x07) != 0 && (interpreter.getDataByte(0x180) & 0x04) != 4) {
                        DSN = b;
                        // we have a new DSN now. Therefore, we cannot send an ACK for the last frame any more.
                        lastCRCok = false;
                     }
                    
                    break;
                case 5:
                    PANId[0]=interpreter.getDataByte(0x183);
                    PANId[1]=interpreter.getDataByte(0x184);
                    macPANId[0]=interpreter.getDataByte(PAN_ID_0);
                    macPANId[1]=interpreter.getDataByte(PAN_ID_1);
                  //  printer.println("PANId " + PANId[0] + " " +PANId[1]);
                   // printer.println("macPANId " + macPANId[0]+" "+macPANId[1]);
                    if (((interpreter.getDataByte(0x181) >> 2) & 0x02) != 0) {//DestPANId present?
                        if (!Arrays.equals(PANId, macPANId) && !Arrays.equals(PANId, SHORT_BROADCAST_ADDR)) {
                            return false;
                        }
                    } else
                    if (((interpreter.getDataByte(0x181) >> 2) & 0x03) == 0) {//DestPANId and dest addresses are not present
                        if (((interpreter.getDataByte(0x181) >> 6) & 0x02) != 0) {//SrcPANId present
                            if ((interpreter.getDataByte(0x180) & 0x07) == 0) {//beacon frame: SrcPANid shall match macPANId unless macPANId = 0xffff
                //                if (!Arrays.equals(PANId, macPANId) && !Arrays.equals(macPANId, SHORT_BROADCAST_ADDR) && !BCN_ACCEPT.getValue())
                 //                   return false;
                            } else
                            if (((interpreter.getDataByte(0x180)& 0x07) == 1) || ((interpreter.getDataByte(0x180) & 0x07) == 3)) {//data or mac command
                        //        if (!PAN_COORDINATOR.getValue() || !Arrays.equals(PANId,macPANId)) return false;
                            }
                        }
                    }
                    break;
                case 7://If 16-bit Destination Address exits check if  match
                    if (((interpreter.getDataByte(0x181) >> 2) & 0x03) == 2) {
                        ShortAddr[0] = interpreter.getDataByte(0x185);
                        ShortAddr[1] = interpreter.getDataByte(0x186);
                        macShortAddr[0] = interpreter.getDataByte(SHORT_ADDR_0);
                        macShortAddr[1] = interpreter.getDataByte(SHORT_ADDR_1);
                        if (!Arrays.equals(ShortAddr, macShortAddr) && !Arrays.equals(ShortAddr, SHORT_BROADCAST_ADDR)) {
                        //   printer.println("shortadr " + ShortAddr[0]+ShortAddr[1]);
                        //   printer.println("macshortaddr " + macShortAddr[0]+" "+macShortAddr[1]);
                            return false;
                        }
                      
                    }
                    break;
                case 13://If 64-bit Destination Address exits check if match
                    if (((interpreter.getDataByte(0x181) >> 2) & 0x03) == 3) {
                        LongAdr[0] = interpreter.getDataByte(0x185);
                        LongAdr[1] = interpreter.getDataByte(0x186);
                        LongAdr[2] = interpreter.getDataByte(0x187);
                        LongAdr[3] = interpreter.getDataByte(0x188);
                        LongAdr[4] = interpreter.getDataByte(0x189);
                        LongAdr[5] = interpreter.getDataByte(0x18A);
                        LongAdr[6] = interpreter.getDataByte(0x18B);
                        LongAdr[7] = interpreter.getDataByte(0x18C);
                        IEEEAdr[0] = interpreter.getDataByte(IEEE_ADDR_0);
                        IEEEAdr[1] = interpreter.getDataByte(IEEE_ADDR_1);
                        IEEEAdr[2] = interpreter.getDataByte(IEEE_ADDR_2);
                        IEEEAdr[3] = interpreter.getDataByte(IEEE_ADDR_3);
                        IEEEAdr[4] = interpreter.getDataByte(IEEE_ADDR_4);
                        IEEEAdr[5] = interpreter.getDataByte(IEEE_ADDR_5);
                        IEEEAdr[6] = interpreter.getDataByte(IEEE_ADDR_6);
                        IEEEAdr[7] = interpreter.getDataByte(IEEE_ADDR_7);
                        if (!Arrays.equals(LongAdr, IEEEAdr) && !Arrays.equals(LongAdr, LONG_BROADCAST_ADDR)) {
                         //   printer.println(" longadr " + LongAdr[0]+" "+LongAdr[1]+" "+LongAdr[2]+" "+LongAdr[3]+" "+LongAdr[4]+" "+LongAdr[5]+" "+LongAdr[6]+" "+LongAdr[7]);
                         //   printer.println(" IEEEAdr " + IEEEAdr[0]+" "+IEEEAdr[1]+" "+IEEEAdr[2]+" "+IEEEAdr[3]+" "+IEEEAdr[4]+" "+IEEEAdr[5]+" "+IEEEAdr[6]+" "+IEEEAdr[7]);
                            return false;
                        }
                    }
                    break;
            }
            return true;
        }

        void startup() {
            if (!rxactive) {
                rxactive = true;
                stateMachine.transition(3);//change to receive state
                state = RECV_SFD_SCAN;
                clearBER();
                beginReceive(getFrequency());
      //          clock.insertEvent(rssiValidEvent, 4*cyclesPerByte);  // 8 symbols = 4 bytes
                if (DEBUGRX && printer!=null) printer.println("RFA1: RX startup");
            } else printer.println("receiver startup while active");
        }

        void shutdown() {
            if (rxactive) {
                rxactive = false;
                endReceive();
                state = RECV_SFD_SCAN;
                stateMachine.transition(1);//change to idle state
             //   setRssiValid(false);
                if (DEBUGRX && printer!=null) printer.println("RFA1: RX shutdown");
            } else printer.println("receiver shutdown while not active");
        }

        void resetOverflow() {
            state = RECV_SFD_SCAN;
        }
   /* -----------------------------------RSSI, LQI, PER------------------------------------*/
        //LUT for max and min correlation values depending on PER
        private final int [] Corr_MAX = {110,109,109,109,107,107,107,107,107,
        107,107,107,103,102,102,102,101,101,101,101,99,94,92,94,101,97,98,97,97,97,97,97,
        94,94,94,94,94,94,94,94,94,94,94,94,92,89,89,89,89,89,88,88,88,88,88,86,86,86,
        86,86,86,86,86,86,85,85,85,85,85,85,83,83,83,83,83,83,83,83,79,78,78,78,78,78,
        76,76,76,74,74,74,74,74,74,74,74,74,74,66,65,65,65};
        private final int [] Corr_MIN = {95,95,94,91,90,90,89,89,89,88,88,88,82,
        82,82,82,76,76,76,76,76,76,74,74,74,74,74,74,72,72,72,72,72,72,72,72,69,69,69,69,
        69,69,69,69,69,69,69,69,69,69,69,69,69,69,69,69,67,67,67,67,67,67,65,65,65,65,65,
        65,65,64,64,63,63,63,63,63,63,63,63,63,61,61,61,60,60,60,58,58,56,56,56,55,55,55,
        50,50,50,50,50,50,50};
        protected double Correlation;
/*
        private void setRssiValid (boolean v){
            if (DEBUG && printer!=null) printer.println("RF231: setrssivalid "+ rssi_val);
            registers[PHY_RSSI] |= 0x80; //Set RX_CRC_VALID
        }
        
        private boolean getRssiValid (){
            if (DEBUG && printer!=null) printer.println("RF231: getRssivalid");
            //RF231 RSSI is always valid in non-extended mode
            return true;
        }
 */       

        public double getCorrelation (){
            int PERindex = (int)(getPER()*100);
            Random random = new Random();
            //get the range, casting to long to avoid overflow problems
            long range = (long)Corr_MAX[PERindex] - (long)Corr_MIN[PERindex] + 1;
            // compute a fraction of the range, 0 <= frac < range
            long fraction = (long)(range * random.nextDouble());
            double corr = fraction + Corr_MIN[PERindex];
            if (DEBUGRX && printer!=null) printer.println("RFA1: returncorr " + corr);
            return corr;
        }

        public void setRSSI (double Prec){
            //RSSI register in units of 3dBm, 0 <-90dBm, 28 >=-10 dBm
            int rssi_val = (((int) Math.rint(Prec) + 90) / 3) +1;
            if (rssi_val < 0) rssi_val = 0;
            if (rssi_val > 28) rssi_val = 28;
            if (DEBUGRX && printer!=null) printer.println("RFA1: setrssi " + rssi_val + " " + this);
            interpreter.writeDataByte(PHY_RSSI, (byte) (rssi_val | interpreter.getDataByte(PHY_RSSI) & 0xE0));
        }

        public double getRSSI (){
            int rssi_val = interpreter.getDataByte(PHY_RSSI) & 0x1F;
            return -90 + 3*(rssi_val-1);
        }

        public void setBER (double BER){
            if (DEBUGRX && printer != null) printer.println("RFA1: setber");
            BERcount++;
            if (BERcount > 5) {
                BERtotal += BER;
            }
        }
        
        public double getPER (){
            double PER = 0.0D;
            if (BERcount > 5) {
                //compute average BER after SHR
                PER = BERtotal/(BERcount-5);
                //considering i.i.s errors i compute PER
                PER = 1D-Math.pow((1D-PER),(BERcount-5)*8);
            }
            clearBER();
            if (DEBUGRX && printer!=null) printer.println("RFA1: getPER " + PER);
            return PER;
        }
        
        public void clearBER() {
            BERcount = 0;
            BERtotal = 0.0D;
        }

        /**
         * The <code>RssiValid</code> class implements a Simulator Event
         * that is fired when the RSSI becomes valid after 8 symbols
         */
         /*
        protected class RssiValid implements Simulator.Event {
        
            public void fire() {
                if (activated) {
                    setRssiValid(true);
                }
            }
        }
        protected RssiValid rssiValidEvent = new RssiValid();
        */

    }

    private long toCycles(long us) {
        return us * sim.getClock().getHZ() / 1000000;
    }

    public static Medium createMedium(Synchronizer synch, Medium.Arbitrator arbitrator) {
        return new Medium(synch, arbitrator, 250000, 48, 8, 8 * 128);
    }

    public Medium.Transmitter getTransmitter() {
        return transmitter;
    }

    public Medium.Receiver getReceiver() {
        return receiver;
    }

    public void setMedium(Medium m) {
        medium = m;
        transmitter = new Transmitter(m);
        receiver = new Receiver(m);
    }

    public Medium getMedium() {
        return medium;
    }

}
