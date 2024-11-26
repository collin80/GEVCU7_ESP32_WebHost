#!/bin/bash 
for file in /dev/ttyUSB*
do
 ./esptool.py --chip esp32 --port $file --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x290000 esp32_website.bin 0x10000 esp32_program.bin
done
sleep 1
exit
