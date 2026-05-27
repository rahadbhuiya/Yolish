CC     = gcc
CFLAGS = -std=c11 -Wall -O2
SAN    = -fsanitize=address,undefined -fno-omit-frame-pointer -g
SRCS   = lexer.c parser.c eval.c main.c
OBJS   = $(SRCS:.c=.o)

all: ys

ys: $(OBJS)
	$(CC) -o ys $(OBJS)
	@echo "Built: ./ys"

%.o: %.c yolish.h
	$(CC) $(CFLAGS) -c $< -o $@

install: ys
	sudo cp ys /usr/local/bin/ys
	@echo "Installed: ys is now available system-wide"

debug: CFLAGS += $(SAN)
debug: LDFLAGS = $(SAN)
debug: $(OBJS)
	$(CC) $(SAN) -o ys_debug $(OBJS)
	@echo "Built: ./ys_debug  (AddressSanitizer + UBSan)"

clean:
	rm -f ys ys_debug $(OBJS)