#!/bin/bash 
#./spiffsgen.py 0x160000 ../website esp32_website.bin
./mklittlefs -d 3 -s 0x160000 -c ../website esp32_website.bin
cp esp32_website.bin ..

