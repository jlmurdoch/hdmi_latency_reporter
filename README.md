# HDMI Latency Reporter
This is a basic utility to ascertain the approximate audio and video latency of an HDMI AV system.

It should print the latency on screen after a short flash / beep and the sensors picking it up. 

It uses the [mlozenzati fork of PicoDVI](https://github.com/mlorenzati/PicoDVI) with audio support over DVI.

Hardware used:
- Raspberry Pi Pico, inserted into a...
- [Olimex RP2040 Pico PC](https://www.olimex.com/Products/MicroPython/RP2040-PICO-PC/open-source-hardware) with DVI out
- Phototransistor @ GPIO 21
- [VM-Clap1](https://www.tindie.com/products/nirdvash/vm-clap1-hand-clap-sensor/) Clap detector @ GPIO 20

Notes: 
- This is written to work with the Arduino IDE. 
- The code isn't perfect:
  - Artifacts on screen
  - The audio will cut out now-and-again
  - No guarantee of true video / audio sync
  - Behaves differently on monitors / TVs (adjust the blanking settings)
  - Identified latency to around +/-2ms

## Sources
Original DVI code: [https://github.com/Wren6991/PicoDVI]()
Original audio code: [https://github.com/shuichitakano/pico_lib]()
Combined by mlorenzati: [https://github.com/mlorenzati/PicoDVI]()
