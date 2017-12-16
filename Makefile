include make.mk

all: config.h simulation

config.h: config.def.h
	cp $< $@

clean:
	rm -f simulation simulation.o
