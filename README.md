This project is a hobbyst DIY 2.1 amplifier based on TDA2030A and TDA1557Q chips.

The heart of the project is an arduino compatible custom board called "iterface board" 
with atmega328 microcontroller and 16x2 LCD panel, encoder, resistive keyboard and old-school 
audio processor TDA8425.
It is possible to make it even on a breadboard using a standard arduino and couple of wires :)

Some basic boards, such as interface board and amp boards contains both pcb design and electrical schematics.
All drawing (build/pdfs/pngs/etc) are already prepared to be printed on transarent sheets to make a final PCBs 
from the presensitized PCBs at home.

A few other things are not yet described (such as power board, connection between modules, etc, etc).
To be continued...

Custom eagle libraries are located under eagle_libraries folder.
To make a gerber / images from the command line you must have:
1. OSX (could work on other UNIX/Linux boxes)
2. eagle
3. homebrew
4. gerbv

Scripts should be changed to reflect a current Eagle installation path.
