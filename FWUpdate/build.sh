#!/bin/bash

./makeota.sh f311 f31101 JPCAPM0001 > JPCAPM0001.hex
./flash.sh JPCAPM0001.hex

rm update.tar.gz
cd src/
tar czf ../update.tar.gz *

