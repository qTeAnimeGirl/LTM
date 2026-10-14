CC ?= clang
CFLAGS ?= -Wall -Wextra -O2 -g -Iinclude
ASFLAGS ?= -g

SRCS = src/main.c \
       src/elf_loader.c \
       src/patcher.c \
       src/sandbox.c \
       src/syscall_dispatcher.c \
       src/syscall_handlers.c \
       src/jump_stub.s

OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.s=.o)

TARGET = ltm

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@python3 scripts/enable_x18_preservation.py $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) src/*.o tests/*.o tests/*.elf tests/*.bin
