//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <MultiChannelDevice.h>

// Arduino Pro mini 8 Mhz
// Arduino pin for the config button
#define CONFIG_BUTTON_PIN 8

// number of available peers per channel
#define PEERS_PER_CHANNEL 6


// all library classes are placed in the namespace 'as'
using namespace as;

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x11, 0x01},          // Device ID
  "JPCAPM0001",               // Device Serial
  {0xF3, 0x11},              // Device Model
  0x10,                       // Firmware Version
  as::DeviceType::THSensor,   // Device Type
  {0x01, 0x01}               // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> SPIType;
typedef Radio<SPIType, 2> RadioType;
typedef StatusLed<4> LedType;
typedef AskSin<LedType, BatterySensor, RadioType> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);

      battery.init(seconds2ticks(60UL * 60), sysclock); //battery measure once an hour
      battery.low(22);
      battery.critical(18);
    }

    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;


DEFREGISTER(UReg0, MASTERID_REGS, DREG_BURSTRX, DREG_LOWBATLIMIT, 0x21, 0x22)
class UList0 : public RegList0<UReg0> {
  public:
    UList0 (uint16_t addr) : RegList0<UReg0>(addr) {}
    bool Sendeintervall (uint16_t value) const {
      return this->writeRegister(0x21, (value >> 8) & 0xff) && this->writeRegister(0x22, value & 0xff);
    }
    uint16_t Sendeintervall () const {
      return (this->readRegister(0x21, 0) << 8) + this->readRegister(0x22, 0);
    }
    void defaults () {
      clear();
      burstRx(false);
      lowBatLimit(22);
      Sendeintervall(30);
    }
};

class WeatherEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t val, bool batlow) {
      Message::init(0x0c, msgcnt, 0x70, BCAST , batlow ? 0x80 : 0x00, 0x00);
      pload[0] = val;
    }
};

class WeatherChannel : public Channel<Hal, List1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
    WeatherEventMsg msg;
    uint16_t      millis;

  public:
    WeatherChannel () : Channel(), Alarm(5), millis(0) {}
    virtual ~WeatherChannel () {}

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      uint8_t msgcnt = device().nextcount();
      tick = delay();
      sysclock.add(*this);

      msg.init(msgcnt, 65, device().battery().low());

      device().sendPeerEvent(msg, *this);
    }

    uint32_t delay () {
      uint16_t _txMindelay = 30;
      _txMindelay = device().getList0().Sendeintervall();
      if (_txMindelay == 0) _txMindelay = 30;
      return seconds2ticks(_txMindelay * 60);
    }

    void configChanged() {
      //DPRINTLN("Config changed List1");
    }

    void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
      Channel::setup(dev, number, addr);
      sysclock.add(*this);
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      return 0;
    }
};

class UType : public MultiChannelDevice<Hal, WeatherChannel, 1, UList0> {
  public:
    typedef MultiChannelDevice<Hal, WeatherChannel, 1, UList0> TSDevice;
    UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~UType () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINTLN("Config Changed List0");
      DPRINT("LOW BAT Limit: ");
      DDECLN(this->getList0().lowBatLimit());
      DPRINT("Wake-On-Radio: ");
      DDECLN(this->getList0().burstRx());
      this->battery().low(this->getList0().lowBatLimit());
      DPRINT("Sendeintervall: "); DDECLN(this->getList0().Sendeintervall());
    }
};

UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();
  if ( worked == false && poll == false ) {
    if ( hal.battery.critical() ) {
      hal.activity.sleepForever(hal);
    }
    hal.activity.savePower<Sleep<>>(hal);
  }
}

