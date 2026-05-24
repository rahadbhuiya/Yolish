CC     = gcc
CFLAGS = -std=c11 -Wall -O2
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

clean:
	rm -f ys $(OBJS)
