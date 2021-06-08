//- -----------------------------------------------------------------------------------------------------------------------
// AskSin++
// 2016-10-31 papa Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2019-05-03 jp112sdl Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
// 2019-05-04 stan23 Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//- -----------------------------------------------------------------------------------------------------------------------
// ci-test=yes board=328p aes=no

//Sensor:
//https://www.dfrobot.com/wiki/index.php/Capacitive_Soil_Moisture_Sensor_SKU:SEN0193

// define this to read the device id, serial and device type from bootloader section
// #define USE_OTA_BOOTLOADER

// #define NO_DS18B20 //use model without temperature sensor

#define EI_NOTEXTERNAL
#include <EnableInterrupt.h>
#define SENSOR_ONLY

// Arduino Pro mini 8 Mhz
// Arduino pin for the config button
#define CONFIG_BUTTON_PIN      8
#define LED_PIN                4
#define BATT_EN_PIN            5
#define BATT_SENS_PIN          14  // A0

#define CC1101_GDO0_PIN        2
#define CC1101_CS_PIN          10
#define CC1101_MOSI_PIN        11
#define CC1101_MISO_PIN        12
#define CC1101_SCK_PIN         13
const uint8_t SENSOR_PINS[]    {15, 16, 17}; //AOut Pins der Sensoren (hier A1, A2 und A3)
//bei Verwendung von > 3 Sensoren sollten die Vcc der Sensoren auf 2 Enable Pins verteilt werden (max. Last pro AVR-Pin beachten!)
const uint8_t SENSOR_EN_PINS[] {6};

#define DS18B20_PIN            3


#define DEVICE_CHANNEL_COUNT sizeof(SENSOR_PINS)
#include <AskSinPP.h>
#include <LowPower.h>

#include <Register.h>
#include <MultiChannelDevice.h>

#ifndef NO_DS18B20
#include <OneWire.h>
#include <sensors/Ds18b20.h>
OneWire oneWire(DS18B20_PIN);
#endif

// number of available peers per channel
#define PEERS_PER_CHANNEL 4

// all library classes are placed in the namespace 'as'
using namespace as;

//Korrekturfaktor der Clock-Ungenauigkeit, wenn keine RTC verwendet wird
#define SYSCLOCK_FACTOR    0.88

#ifdef NO_DS18B20
#define DEVICE_MODEL  0x11
#else
#define DEVICE_MODEL  0x12
#endif

// define all device properties
const struct DeviceInfo PROGMEM devinfo = {
  {0xF3, DEVICE_MODEL, 0x00},  // Device ID
  "JPCAPM0000",                // Device Serial
  {0xF3, DEVICE_MODEL},        // Device Model
  0x10,                        // Firmware Version
  as::DeviceType::THSensor,    // Device Type
  {0x01, 0x01}                 // Info Bytes
};

/**
   Configure the used hardware
*/
typedef AvrSPI<CC1101_CS_PIN, CC1101_MOSI_PIN, CC1101_MISO_PIN, CC1101_SCK_PIN> SPIType;
typedef Radio<SPIType, CC1101_GDO0_PIN> RadioType;
typedef StatusLed<LED_PIN> LedType;
typedef AskSin<LedType, BatterySensorUni<BATT_SENS_PIN, BATT_EN_PIN, 0>, RadioType> BaseHal;
class Hal : public BaseHal {
  public:
    void init (const HMID& id) {
      BaseHal::init(id);
      battery.init(seconds2ticks(60UL * 60) * SYSCLOCK_FACTOR, sysclock); //battery measure once an hour
      battery.low(22);
      battery.critical(19);
    }

    bool runready () {
      return sysclock.runready() || BaseHal::runready();
    }
} hal;


DEFREGISTER(UReg0, MASTERID_REGS, DREG_LOWBATLIMIT, 0x21, 0x22)
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
      lowBatLimit(22);
      Sendeintervall(30);
    }
};

DEFREGISTER(UReg1, 0x01, 0x02, 0x03, 0x04, 0x23, 0x24, 0x25, 0x26)
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

#ifndef NO_DS18B20
    bool Offset (int32_t value) const {
      return
          this->writeRegister(0x01, (value >> 24) & 0xff) &&
          this->writeRegister(0x02, (value >> 16) & 0xff) &&
          this->writeRegister(0x03, (value >> 8)  & 0xff) &&
          this->writeRegister(0x04, (value)       & 0xff)
          ;
    }

    int32_t Offset () const {
      return
          ((int32_t)(this->readRegister(0x01, 0)) << 24) +
          ((int32_t)(this->readRegister(0x02, 0)) << 16) +
          ((int32_t)(this->readRegister(0x03, 0)) << 8 ) +
          ((int32_t)(this->readRegister(0x04, 0))      )
          ;
    }
#endif

    void defaults () {
      clear();
      HIGHValue(830);
      LOWValue(420);
#ifndef NO_DS18B20
      Offset(0);
#endif
    }
};

class WeatherEventMsg : public Message {
  public:
  void init(uint8_t msgcnt, uint8_t *h, bool batlow, uint8_t volt, __attribute__ ((unused))  int16_t temperature, __attribute__ ((unused))  int8_t offset) {

#ifndef NO_DS18B20
    int16_t t = temperature + offset;
    DPRINT(F("+Temp         C : ")); DDECLN(t);
#endif
    DPRINT(F("+Battery      V : ")); DDECLN(volt);
#ifdef NO_DS18B20
#define PAYLOAD_OFFSET 0
#else
#define PAYLOAD_OFFSET 2
#endif

    Message::init(0xc + PAYLOAD_OFFSET + (DEVICE_CHANNEL_COUNT * 2), msgcnt, 0x53, (msgcnt % 20 == 1) ? (BIDI | WKMEUP) : BCAST, batlow ? 0x80 : 0x00, 0x41);

#ifndef NO_DS18B20
    pload[0] = (t >> 8) & 0xff;
    pload[1] = (t)      & 0xff;
#endif

    pload[PAYLOAD_OFFSET] = (volt)   & 0xff;
    for (uint8_t s = 0; s < DEVICE_CHANNEL_COUNT; s++) {
      DPRINT(F("+Humidity (#")); DDEC(s + 1); DPRINT(F(") %: ")); DDECLN(h[s]);
      pload[1+PAYLOAD_OFFSET+(s * 2)] = 0x42 + s;
      pload[2+PAYLOAD_OFFSET+(s * 2)] = h[s] & 0xff;
    }
  }
  void init(uint8_t msgcnt, uint8_t *h, bool batlow, uint8_t volt) {
    init(msgcnt, h, batlow, volt, 0, 0);
  }
};

class WeatherChannel : public Channel<Hal, UList1, EmptyList, List4, PEERS_PER_CHANNEL, UList0> {
  public:
    WeatherChannel () : Channel() {}
    virtual ~WeatherChannel () {}

    void configChanged() {
      DPRINT(F("Config changed List1 (CH "));DDEC(number());DPRINTLN(F(")"));
#ifndef NO_DS18B20
      if (number() == 1) { DPRINT(F("*Offset    : ")); DDECLN(this->getList1().Offset()); }
#endif
      if (number() > 1)  { DPRINT(F("*HIGHValue : ")); DDECLN(this->getList1().HIGHValue()); }
      if (number() > 1)  { DPRINT(F("*LOWValue  : ")); DDECLN(this->getList1().LOWValue()); }
    }

    uint8_t status () const {
      return 0;
    }

    uint8_t flags () const {
      return 0;
    }
};

class UType : public MultiChannelDevice<Hal, WeatherChannel, DEVICE_CHANNEL_COUNT + 1, UList0> {
public:
#ifndef NO_DS18B20
  Ds18b20      sensor[1];
#endif
  class SensorArray : public Alarm {
       UType& dev;

       public:
         uint8_t       humidity[DEVICE_CHANNEL_COUNT];
         SensorArray (UType& d) : Alarm(0), dev(d) {}
         virtual ~SensorArray () {}

         void measure() {
           //enable all moisture sensors
           for (uint8_t s = 0; s < sizeof(SENSOR_EN_PINS); s++) {
             digitalWrite(SENSOR_EN_PINS[s], HIGH);
             _delay_ms(5);
           }

           //wait a moment to settle
           _delay_ms(500);
           //now measure all sensors
           for (uint8_t s = 0; s < DEVICE_CHANNEL_COUNT; s++) {
             uint16_t sens_val = 0;

             //measure 8 times and calculate average
             for (uint8_t i = 0; i < 8; i++) {
               sens_val += analogRead(SENSOR_PINS[s]);
               _delay_ms(10);
             }
             sens_val /= 8;

             DPRINT(F("+Analog     (#")); DDEC(s + 1); DPRINT(F("): ")); DDEC(sens_val);
             uint16_t upper_limit = dev.channel(s + 2).getList1().HIGHValue();
             uint16_t lower_limit = dev.channel(s + 2).getList1().LOWValue();
             if (sens_val > upper_limit) {
               humidity[s] = 0;
               DPRINTLN(F(" higher than limit!"));
             }
             else if (sens_val < lower_limit) {
               humidity[s] = 100;
               DPRINTLN(F(" lower than limit!"));
             }
             else {
               uint16_t range = upper_limit - lower_limit;
               uint32_t base = sens_val - lower_limit;
               uint8_t pct_inv = (base * 100) / range;
               humidity[s] = 100 - pct_inv;
               DPRINTLN("");
             }

             //humidity[s] = random(0,100);

           }
           //disable all moisture sensors
           for (uint8_t s = 0; s < sizeof(SENSOR_EN_PINS); s++)
             digitalWrite(SENSOR_EN_PINS[s], LOW);

#ifndef NO_DS18B20
           Ds18b20::measure(dev.sensor, 1);
#endif
         }

         virtual void trigger (__attribute__ ((unused)) AlarmClock& clock) {
           measure();
           tick = delay();
           WeatherEventMsg& msg = (WeatherEventMsg&)dev.message();
#ifndef NO_DS18B20
           msg.init(dev.nextcount(), humidity, dev.battery().low(), dev.battery().current(), dev.sensor[0].temperature(), dev.channel(1).getList1().Offset());
#else
           msg.init(dev.nextcount(), humidity, dev.battery().low(), dev.battery().current());
#endif
           dev.send(msg, dev.getMasterID());
           sysclock.add(*this);
         }

         uint32_t delay () {
           //Sendeintervall festlegen
           uint16_t _txDelay = max(dev.getList0().Sendeintervall(), 1);
           return seconds2ticks(_txDelay * 60 * SYSCLOCK_FACTOR);
         }

      } sensarray;


    typedef MultiChannelDevice<Hal, WeatherChannel, DEVICE_CHANNEL_COUNT + 1, UList0> TSDevice;
    UType(const DeviceInfo& info, uint16_t addr) : TSDevice(info, addr), sensarray(*this) {}
    virtual ~UType () {}

    void init (Hal& hal) {
      TSDevice::init(hal);
      for (uint8_t s = 0; s < DEVICE_CHANNEL_COUNT; s++)
        pinMode(SENSOR_PINS[ s ], INPUT);

      for (uint8_t s = 0; s < sizeof(SENSOR_EN_PINS); s++)
      pinMode(SENSOR_EN_PINS[s], OUTPUT);

#ifndef NO_DS18B20
      uint8_t sensorcount = Ds18b20::init(oneWire, sensor, 1);
      DPRINT(F("DS18B20 Sensor "));DPRINTLN((sensorcount > 0) ? F("OK"):F("ERROR"));
#endif
      sensarray.set(seconds2ticks(5));
      sysclock.add(sensarray);
    }

    virtual void configChanged () {
      TSDevice::configChanged();
      DPRINT(F("*LOW BAT Limit: "));
      DDECLN(this->getList0().lowBatLimit());
      this->battery().low(this->getList0().lowBatLimit());
      DPRINT(F("*Sendeintervall: ")); DDECLN(this->getList0().Sendeintervall());

    }
};

UType sdev(devinfo, 0x20);
ConfigButton<UType> cfgBtn(sdev);

void setup () {
  DINIT(57600, ASKSIN_PLUS_PLUS_IDENTIFIER);
  sdev.init(hal);
  DDEVINFO(sdev);
  buttonISR(cfgBtn, CONFIG_BUTTON_PIN);
  sdev.initDone();
}

void loop() {
  bool worked = hal.runready();
  bool poll = sdev.pollRadio();

  if ( worked == false && poll == false ) {
    if ( hal.battery.critical() ) {
      DPRINT(F("Battery critical! "));DDECLN(hal.battery.current());
      Serial.flush();
      hal.activity.sleepForever(hal);
    }
    hal.activity.savePower<Sleep<>>(hal);
  }
}
