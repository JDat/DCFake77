-include .config

CFLAGS += -MMD -Wall

LDLIBS += " "

.PHONY:		all clean

all:		dcf

dcf:		dcfake77hw.o dcfake77.o 
			gcc -o dcfake77 *.o
clean:
		rm -f *.o *.d dcfake77

-include *.d
