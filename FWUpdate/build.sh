#!/bin/bash
cd ../HB-UNI-Sen-CAP-MOIST
rm HB-UNI-Sen-CAP-MOIST.ino.with_bootloader.eightanaloginputs.hex
mv -f HB-UNI-Sen-CAP-MOIST.ino.eightanaloginputs.hex HB-UNI-Sen-CAP-MOIST-OTA-FW.hex
cd ../FWUpdate

./makeota.sh f311 f311$2 $1 > $1_BOOTLOADER.hex
#./flash.sh $1_BOOTLOADER.hex

rm update.tar.gz
cd src/
rm *.eq3
../prepota.sh ../../HB-UNI-Sen-CAP-MOIST/HB-UNI-Sen-CAP-MOIST-OTA-FW.hex 
tar czf ../update.tar.gz *
cd ..
./flash-ota -f src/HB-UNI-Sen-CAP-MOIST-OTA-FW_*.eq3 -s $1 -U /dev/cu.wchusbserial1420
