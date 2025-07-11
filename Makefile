CFLAGS += -std=gnu89 -Wall -pedantic
OBJS = decode.o decoder.o fail.o int.o
EXE = decode decompress
all: $(EXE)
clean:
	rm -f -- $(OBJS) $(EXE)
decoder.o: decoder.h
decode: decoder.o fail.o int.o
fail.o: fail.h
aiff.o: aiff.h int.o
int.o: int.h
decompress: decoder.o fail.o aiff.o int.o
