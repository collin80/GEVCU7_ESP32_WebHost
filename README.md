An ESP32 program that runs on the on-board ESP32 on a GEVCU7 board. 

Provides a web interface as well as telnet style access to the GEVCU7 serial console. 

The web interface (http://gevcu7.local/) shows every StatusEntry value live and lets you view and set
every ConfigEntry parameter of each enabled device. The page lives in website/ and talks to the ESP32
over a websocket at /ws. The ESP32 relays requests to GEVCU7 over the serial link (see the protocol notes
at the top of ESP32Driver.cpp in the GEVCU7 project) and caches status values so a new page shows
everything right away. Build/flash the page with `pio run -t uploadfs` or bin/buildwebimg.sh.

Utilizes mDNS to allow for a nice host name (gevcu7.local by default). 

Meant to be compiled with ESP32-Arduino 3.0 or higher
