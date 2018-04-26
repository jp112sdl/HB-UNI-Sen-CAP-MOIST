//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2018-04-16 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------

//Sensor:
//https://www.dfrobot.com/wiki/index.php/Capacitive_Soil_Moisture_Sensor_SKU:SEN0193

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

//Korrekturfaktor der Clock-Ungenauigkeit, wenn keine RTC verwendet wird
#define SYSCLOCK_FACTOR    0.88

#define LED_PIN            4
#define BATT_EN_PIN        3
#define BATT_SENS_PIN      A6

//SENSOR_EN_PIN und SENSOR_PIN sind immer paarweise und kommagetrennt hinzuzufügen:
//Beispiel für 3 Sensoren
//byte SENSOR_EN_PINS[]      {5 , 6, 7}; //VCC Pin des Sensors
//byte SENSOR_PINS[]         {14,15,16}; //AOut Pin des Sensors

byte SENSOR_EN_PINS[]      {5 }; //VCC Pin des Sensors
byte SENSOR_PINS[]         {14}; //AOut Pin des Sensors

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, 0x11, 0x01},          // Device ID
  "JPCAPM0001",                // Device Serial
  {0xF3, 0x11},                // Device Model
  0x10,                        // Firmware Version
  as::DeviceType::THSensor,    // Device Type
  {0x01, 0x01}                 // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<10, 11, 12, 13> SPIType;
typedef Radio<SPIType, 2> RadioType;
typedef StatusLed<LED_PIN> LedType;
typedef AskSin<LedType, BatterySensorUni<BATT_SENS_PIN, BATT_EN_PIN>, RadioType> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);
      battery.init(seconds2ticks(60UL * 60) * SYSCLOCK_FACTOR, sysclock); //battery measure once an hour
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

DEFREGISTER(UReg1, 0x23, 0x24, 0x25, 0x26)
class UList1 : public RegList1<UReg1> {
  public:
    UList1 (uint16_t addr) : RegList1<UReg1>(addr) {}
    bool HIGHValue (uint16_t value) const {
      return this->writeRegister(0x23, (value >> 8) & 0xff) && this->writeRegister(0x24, value & 0xff);
    }
    uint16_t HIGHValue () const {
      return (this->readRegister(0x23, 0) << 8) + this->readRegister(0x24, 0);
    }

    bool LOWValue (uint16_t value) const {
      return this->writeRegister(0x25, (value >> 8) & 0xff) && this->writeRegister(0x26, value & 0xff);
    }
    uint16_t LOWValue () const {
      return (this->readRegister(0x25, 0) << 8) + this->readRegister(0x26, 0);
    }

    void defaults () {
      clear();
      HIGHValue(830);
      LOWValue(420);
    }
};

class WeatherEventMsg : public Message {
  public:
    void init(uint8_t msgcnt, uint8_t channel, uint8_t val, bool batlow) {

      Message::init(0x0d, msgcnt, 0x70, BCAST , batlow ? 0x80 : 0x00, 0x00);
      pload[0] = val;
      pload[1] = channel;
    }
};

class WeatherChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0>, public Alarm {
    WeatherEventMsg msg;
    uint16_t      millis;
    uint8_t       humidity;

  public:
    WeatherChannel () : Channel(), Alarm(0), millis(0) {}
    virtual ~WeatherChannel () {}

    void measure() {
      digitalWrite(SENSOR_EN_PINS[(number() - 1)], HIGH);
      _delay_ms(500);
      uint16_t sens_val = 0;
      for (uint8_t i = 0; i < 10; i++) {
        sens_val += analogRead(SENSOR_PINS[(number() - 1)]);
        _delay_ms(10);
      }
      sens_val = sens_val / 10;
      digitalWrite(SENSOR_EN_PINS[(number() - 1)], LOW);
      DPRINT(F("+Sensor   (#")); DDEC(number()); DPRINT(F(") V: ")); DDECLN(sens_val);
      uint16_t range = this->getList1().HIGHValue() - this->getList1().LOWValue();
      uint16_t base = sens_val - this->getList1().LOWValue();
      uint8_t pct_inv = ((100 * base) / range);
      humidity = (pct_inv > 100) ? 0 : 100 - pct_inv;
      DPRINT(F("+Humidity (#")); DDEC(number()); DPRINT(F(") %: ")); DDECLN(humidity);
    }

    virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
      measure();

      uint8_t msgcnt = device().nextcount();
      tick = delay();
      sysclock.add(*this);
      msg.init(msgcnt, number(), humidity, device().battery().low());
      device().sendPeerEvent(msg, *this);
    }

    uint32_t delay () {
      uint16_t _txMindelay = 30;
      _txMindelay = device().getList0().Sendeintervall();
      if (_txMindelay == 0) _txMindelay = 30;
      return seconds2ticks(_txMindelay * 60 * SYSCLOCK_FACTOR);
    }

    void configChanged() {
      DPRINTLN(F("Config changed List1"));
      DPRINT(F("*HIGHValue (#"));DDEC(number());DPRINT(F("): ")); DDECLN(this->getList1().HIGHValue());
      DPRINT(F("*LOWValue  (#"));DDEC(number());DPRINT(F("): ")); DDECLN(this->getList1().LOWValue());
    }

    void setup(Device<Hal, UList0>* dev, uint8_t number, uint16_t addr) {
      for (int i = 0; i < sizeof(SENSOR_PINS); i++) {
        pinMode(SENSOR_PINS[i], INPUT);
        pinMode(SENSOR_EN_PINS[i], OUTPUT);
      }
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

class UType : public MultiChannelDevice<Hal, WeatherChannel, sizeof(SENSOR_PINS), UList0> {
  public:
    typedef MultiChannelDevice<Hal, WeatherChannel, sizeof(SENSOR_PINS), UList0> TSDevice;
    UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr) {}
    virtual ~UType () {}

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINT("*LOW BAT Limit: ");
      DDECLN(this->getList0().lowBatLimit());
      DPRINT("*Wake-On-Radio: ");
      DDECLN(this->getList0().burstRx());
      this->battery().low(this->getList0().lowBatLimit());
      DPRINT("*Sendeintervall: "); DDECLN(this->getList0().Sendeintervall());
    }
};

UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  if (sizeof(SENSOR_PINS) != sizeof(SENSOR_EN_PINS)) {
    DPRINTLN(F("!!! ERROR: Anzahl SENSOR_PINS entspricht nicht der Anzahl SENSOR_EN_PINS"));
  } else {
    printDeviceInfo();
    sdev.init(hal);
    buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
    sdev.initDone();
  }
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

void printDeviceInfo() {
  HMID ids;
  sdev.getDeviceID(ids);

  uint8_t ser[10];
  sdev.getDeviceSerial(ser);

  DPRINT(F("Device Info: "));
  for (int i = 0; i < 10; i++) {
    DPRINT(char(ser[i]));
  }
  DPRINT(" ("); DHEX(ids); DPRINTLN(")");
}


