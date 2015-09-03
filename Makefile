OBJS = FuzzerCrossOver.o FuzzerDriver.o FuzzerIO.o \
	FuzzerInterface.o FuzzerLoop.o FuzzerMutate.o \
	FuzzerSHA1.o FuzzerSanitizerOptions.o FuzzerTraceState.o FuzzerUtil.o \
	test_harness.o test_pg.o

%.o: %.cpp
	clang++ -fPIC -c -std=c++11 $<

all: test.so

test_harness.o: test_harness.cpp
	clang++ -Wno-writable-strings -fPIC -c -std=c++11 test_harness.cpp

%.o: %.c
	clang -I`/usr/local/pgsql/bin/pg_config --includedir-server` -fPIC -c test_pg.c

test.so: $(OBJS)
	clang++ -shared -o test.so -fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters $(OBJS)

test: test.so
	/usr/local/pgsql/bin/psql -c 'select fuzz()'
