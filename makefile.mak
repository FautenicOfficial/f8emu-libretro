#Flags
CC=x86_64-w64-mingw32-gcc
SOURCES_CC=libretro.c fake6502.c
CCFLAGS=-O3 -Wall -D__LIBRETRO__

CXX=x86_64-w64-mingw32-g++
SOURCES_CXX=
CXXFLAGS=-O3 -Wall -D__LIBRETRO__

LD=x86_64-w64-mingw32-gcc
PROGRAM=f8emu_libretro.dll
OBJECTS=$(SOURCES_CC:.c=.o) $(SOURCES_CXX:.cpp=.o)
LDFLAGS=-shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=./link.T -Wl,--no-undefined
LIBS=-lm

#Make program
all: $(OBJECTS)
	$(LD) $(LDFLAGS) -o $(PROGRAM) $(OBJECTS) $(LIBS)

#Dependencies
%.o: %.c
	$(CC) $(CCFLAGS) -c $< -o $@
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

#Clean up
.PHONY: clean
clean:
	rm $(OBJECTS) $(PROGRAM)