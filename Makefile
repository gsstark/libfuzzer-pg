SAN = -fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters

OBJS = FuzzerCrossOver.o FuzzerDriver.o FuzzerIO.o \
	FuzzerInterface.o FuzzerLoop.o FuzzerMutate.o \
	FuzzerSHA1.o FuzzerSanitizerOptions.o FuzzerTraceState.o FuzzerUtil.o \
	test_harness.o test_pg.o

all: test.so

%.o: %.cpp
	clang++ -g -O2 -fPIC -c -std=c++11 $<

test_harness.o: test_harness.cpp
	clang++ -g -O0 -Wno-writable-strings -fPIC -c -std=c++11 $(SAN) test_harness.cpp

%.o: %.c
	clang -g -O0 -I`/usr/local/pgsql/bin/pg_config --includedir-server` -fPIC  -c $(SAN) test_pg.c

test.so: $(OBJS)
	clang++ -shared -g -O0 -o test.so $(SAN) $(OBJS)

test: test.so
	/usr/local/pgsql/bin/psql -c "select fuzz(10000, 'select \$$1')"

clean:
	rm -f *.o *.so
