package se.sics.cooja.avrmote.interfaces;

import java.util.ArrayDeque;

import org.apache.log4j.Logger;

import se.sics.cooja.Mote;
import se.sics.cooja.MoteTimeEvent;
import se.sics.cooja.Simulation;
import se.sics.cooja.avrmote.AvroraMote;
import se.sics.cooja.dialogs.SerialUI;
import avrora.sim.mcu.AtmelMicrocontroller;

public class AvroraUsart1 extends SerialUI {
  private static Logger logger = Logger.getLogger(AvroraUsart1.class);

  private Mote myMote;
  private avrora.sim.mcu.USART usart;
  private MoteTimeEvent receiveNextByte;

  private ArrayDeque<Byte> rxData = new ArrayDeque<Byte>();

  public AvroraUsart1(Mote mote) {
    myMote = mote;
    receiveNextByte = new MoteTimeEvent(mote, 0) {
      public void execute(long t) {
        if (usart.receiving) {
          /* XXX TODO Postpone how long? */
          myMote.getSimulation().scheduleEvent(this, t+Simulation.MILLISECOND);
          return;
        }
        usart.startReceive();
      }
    };

    /* this should go into some other piece of code for serial data */
    AtmelMicrocontroller mcu = (AtmelMicrocontroller) ((AvroraMote)myMote).CPU.getSimulator().getMicrocontroller();
    usart = (avrora.sim.mcu.USART) mcu.getDevice(getUsart());
    if (usart != null) {
      usart.connect(new avrora.sim.mcu.USART.USARTDevice() {
        public avrora.sim.mcu.USART.Frame transmitFrame() {
          if (rxData.isEmpty()) {
            logger.warn("no data for uart");
            return new avrora.sim.mcu.USART.Frame((byte)'?', false, 8);
          }

          Byte data = rxData.pollFirst();
          if (!receiveNextByte.isScheduled() && rxData.size() > 0) {
            myMote.getSimulation().scheduleEvent(receiveNextByte, myMote.getSimulation().getSimulationTime());
          }

          return new avrora.sim.mcu.USART.Frame(data, false, 8);
        }
        public void receiveFrame(avrora.sim.mcu.USART.Frame frame) {
          dataReceived(frame.value);
        }
      });
    } else {
      System.out.println("*** Warning Avrora could not find usart1 interface...");
    }
  }

  public String getUsart() {
    return "usart1";
  }

  public Mote getMote() {
    return myMote;
  }

  public void writeArray(byte[] s) {
    for (byte b: s) {
      writeByte(b);
    }
  }

  public void writeByte(byte b) {
    if (usart == null) {
      return;
    }

    rxData.addLast(b);
    if (!receiveNextByte.isScheduled()) {
      myMote.getSimulation().scheduleEvent(receiveNextByte, myMote.getSimulation().getSimulationTime());
    }
  }

  public void writeString(String s) {
    if (usart == null) {
      return;
    }
    writeArray(s.getBytes());
    writeByte((byte)'\n');
  }
}
