CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -g -MMD -MP
TARGET  = procmon
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

tests/test_proc: tests/test_proc.c src/proc.o
	$(CC) $(CFLAGS) -Isrc -o $@ $^

test: tests/test_proc
	./tests/test_proc

clean:
	rm -f $(TARGET) src/*.o src/*.d tests/test_proc tests/*.d

-include $(OBJ:.o=.d)

.PHONY: all test clean