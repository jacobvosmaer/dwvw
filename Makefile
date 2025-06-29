CFLAGS += -std=gnu89 -Wall -pedantic -flto
OBJS = decode
all: $(OBJS)
clean:
	rm -f -- $(OBJS)
decode: decoder.c