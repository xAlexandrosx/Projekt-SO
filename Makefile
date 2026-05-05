# ── Kompilator i flagi ───────────────────────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L

# ── Pliki projektu ───────────────────────────────────────────────────────────
TARGET  = foldersync
SRCS    = main.c daemon.c signals.c copy.c sync.c
OBJS    = $(SRCS:.c=.o)
HEADERS = daemon.h signals.h copy.h sync.h

# ── Cel domyślny ─────────────────────────────────────────────────────────────
.PHONY: all clean

all: $(TARGET)

# Linkowanie
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Kompilacja każdego modułu
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Sprzątanie ───────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)
