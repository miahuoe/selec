CC = musl-gcc
LD = musl-gcc
ERRLVL = -Wall -Wextra -pedantic -Wimplicit-fallthrough=0
CFLAGS = -std=c99 -g $(ERRLVL)
LDFLAGS = -static

all : intr

intr : intr.o utf8.o terminal.o
	$(LD) $(LDFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@

strip : intr
	strip -S --strip-unneeded --remove-section=.note.gnu.gold-version --remove-section=.comment --remove-section=.note --remove-section=.note.gnu.build-id --remove-section=.note.ABI-tag $^

clean :
	rm -rf *.o intr
