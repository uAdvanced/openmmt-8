CC ?= gcc
CFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
CORE := ../core
SRCS := test_jam8.c $(CORE)/gen.c $(CORE)/harmony.c $(CORE)/tables.c $(CORE)/scene.c

.PHONY: all test clean

all: test

test_jam8: $(SRCS) $(CORE)/jam.h
	$(CC) $(CFLAGS) -DJAM8_HOST -I$(CORE) -o $@ $(SRCS)

test: test_jam8
	./test_jam8

clean:
	rm -f test_jam8
