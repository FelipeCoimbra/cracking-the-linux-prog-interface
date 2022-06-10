CC := /usr/bin/gcc
CFLAGS=-Wall

ALL_CHPT_SRCS := $(wildcard ./**/*.c)
MAIN_SRC := run.c
ALL_CHPT_OBJS := $(wildcard ./**/*.o)

.PHONY: all clean

all: $(patsubst %.c, %.o, $(ALL_CHPT_SRCS))
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MAIN_SRC) $^ -o run

clean:
	rm -f $(ALL_CHPT_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@	
