#Flags
CC=x86_64-w64-mingw32-gcc
SOURCES=libretro.c fake6502.c
OBJECTS=$(SOURCES:.c=.o)
PROGRAM=f8emu_libretro.dll
CCFLAGS=-O3 -Wall -D__LIBRETRO__
LDFLAGS=-shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=./link.T -Wl,--no-undefined

#Make program
all: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $(PROGRAM) $(OBJECTS) -lm

#Dependencies
%.o: %.c
	$(CC) $(CCFLAGS) -c $< -o $@

#Clean up
.PHONY: clean
clean:
	rm $(OBJECTS) $(PROGRAM)