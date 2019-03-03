CC = musl-gcc
LD = musl-gcc
ERRLVL = -Wall -Wextra -pedantic -Wimplicit-fallthrough=0
CFLAGS = -std=c99 -g $(ERRLVL)
LDFLAGS = -static

all : intr

intr : intr.o
	$(LD) $(LDFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@

strip : intr
	strip intr
clean :
	rm -rf *.o intr
