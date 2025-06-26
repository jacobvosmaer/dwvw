CFLAGS = -std=gnu89 -Wall -pedantic
OBJS = decode
all: $(OBJS)
clean:
	rm -f -- $(OBJS)
