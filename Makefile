CFLAGS += -std=gnu89 -Wall -pedantic
EXE = dwvw
all: $(EXE)
clean:
	rm -f -- $(EXE)
