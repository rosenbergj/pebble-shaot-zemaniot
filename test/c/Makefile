# Host build of the pure C modules plus their test harness.
#
#     make -C test/c test
#
# Nothing here touches the Pebble toolchain: shaot.c, hebdate.c and solar.c are
# deliberately free of pebble.h so they can be checked with plain gcc against
# the same fixtures the node suites used.

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O1 -g

PURE = ../../src/c/shaot.c ../../src/c/hebdate.c ../../src/c/solar.c ../../src/c/trig.c ../../src/c/numparse.c ../../src/c/weather.c ../../src/c/shabbat.c
HDRS = ../../src/c/shaot.h ../../src/c/hebdate.h ../../src/c/solar.h ../../src/c/trig.h ../../src/c/numparse.h ../../src/c/weather.h ../../src/c/shabbat.h fixtures.h

run_tests: run_tests.c $(PURE) $(HDRS)
	$(CC) $(CFLAGS) -o $@ run_tests.c $(PURE) -lm

fixtures.h: ../fixtures/hebdate.json ../fixtures/zmanim.json ../gen/json_to_c.py
	python3 ../gen/json_to_c.py

.PHONY: test clean
test: run_tests
	./run_tests

clean:
	rm -f run_tests
