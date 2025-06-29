CFLAGS += -std=gnu89 -Wall -pedantic
OBJS = decode.o decoder.o
EXE = decode decompress
all: $(EXE)
clean:
	rm -f -- $(OBJS) $(EXE)
decode: decoder.o fail.o
decoder.o: decoder.h
fail.o: fail.h
decompress: decoder.o fail.o
