CC ?= clang
CFLAGS ?= -Wall -Wextra -O2 -g -Iinclude -mmacosx-version-min=12.3
LDFLAGS ?= -Wl,-platform_version,macos,12.3.0,12.3.0
ASFLAGS ?= -g -mmacosx-version-min=12.3

SRCS = src/main.c \
       src/elf_loader.c \
       src/patcher.c \
       src/sandbox.c \
       src/syscall_dispatcher.c \
       src/syscall_handlers.c \
       src/rootfs_installer.c \
       src/jump_stub.s

OBJS = src/main.o \
       src/elf_loader.o \
       src/patcher.o \
       src/sandbox.o \
       src/syscall_dispatcher.o \
       src/syscall_handlers.o \
       src/rootfs_installer.o \
       src/jump_stub.o

TARGET = ltm

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) $(ASFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) src/*.o tests/*.o tests/*.elf tests/*.bin
