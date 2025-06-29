CFLAGS += -std=gnu89 -Wall -pedantic
OBJS = decode.o decoder.o
EXE = decode
all: $(EXE)
clean:
	rm -f -- $(OBJS) $(EXE)
decode: decoder.o
decoder.o: decoder.h
