#!/bin/sh

cp $BASE_DIR/../custom-scripts/S41network-config $BASE_DIR/target/etc/init.d
chmod +x $BASE_DIR/target/etc/init.d/S41network-config

cp $BASE_DIR/../custom-scripts/systeminfo.py $BASE_DIR/target/usr/bin/systeminfo.py
chmod +x $BASE_DIR/target/usr/bin/systeminfo.py

cp $BASE_DIR/../custom-scripts/S50systeminfo $BASE_DIR/target/etc/init.d
chmod +x $BASE_DIR/target/etc/init.d/S50systeminfo

# Compile and install the Simple Driver
make -C $BASE_DIR/../modules/simple_driver/

make -C $BASE_DIR/../modules/xtea_driver_challenge/
