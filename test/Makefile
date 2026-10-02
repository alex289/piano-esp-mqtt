CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -I../components/chord_detect/include

test_chord_detect: test_chord_detect.c ../components/chord_detect/chord_detect.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: check clean
check: test_chord_detect
	./test_chord_detect

clean:
	rm -rf test_chord_detect test_chord_detect.dSYM
