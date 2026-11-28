# PC simulator for the TFT43 Grbl UI: make && ./sim out/
CC ?= gcc
OUT ?= out
SRC = sim_main.c sim_hw.c ../src/ui.c ../src/grbl.c ../src/gfx.c ../src/fonts.c

all: sim
	mkdir -p $(OUT)
	./sim $(OUT)
	python3 ppm2png.py $(OUT)

sim: $(SRC) ../src/*.h
	$(CC) -O1 -g -Wall -Wno-unused-parameter -I../src -DFW_VERSION=\"sim\" $(SRC) -o $@

clean:
	rm -rf sim $(OUT)

.PHONY: all clean
