CLANGVERS=-4.0

SAN = -fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters,trace-cmp
#SAN = -fsanitize-coverage=edge,indirect-calls,8bit-counters,trace-cmp

OBJS = \
	Fuzzer/FuzzerCrossOver.o 			\
	Fuzzer/FuzzerDriver.o 				\
	Fuzzer/FuzzerIO.o 					\
	Fuzzer/FuzzerLoop.o 				\
	Fuzzer/FuzzerMutate.o 				\
	Fuzzer/FuzzerSHA1.o 				\
	Fuzzer/FuzzerTraceState.o 			\
	Fuzzer/FuzzerUtil.o 				\
	Fuzzer/FuzzerUtilDarwin.cpp 		\
    Fuzzer/FuzzerUtilLinux.cpp 			\
    Fuzzer/FuzzerExtFunctionsDlsym.cpp 	\
    Fuzzer/FuzzerExtFunctionsWeak.cpp 	\
    Fuzzer/FuzzerTracePC.cpp 			\
	test_harness.o test_pg.o # fail_pg.o

all: test.so

%.o: %.cpp
	clang++$(CLANGVERS) -g -O2 -fPIC -o $@ -c -std=c++11 $<

test_harness.o: test_harness.cpp
	clang++$(CLANGVERS) -g -O0 -Wno-writable-strings -fPIC -c -std=c++11 $(SAN) test_harness.cpp

%.o: %.c
	clang$(CLANGVERS) -g -O0 -I`/usr/local/pgsql/bin/pg_config --includedir-server` -fPIC -o $@  -c $(SAN) -Wno-ignored-attributes $<

test.so: $(OBJS)
	clang++$(CLANGVERS) -fPIC -std=c++11 -shared -g -O0 -o test.so $(SAN) $(OBJS)

test: test.so
	/usr/local/pgsql/bin/psql -c "select fuzz(10000, 'select \$$1')"

clean:
	rm -f *.o *.so Fuzzer/*.o
