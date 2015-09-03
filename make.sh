#!/bin/sh
set -v -x

# Clean
rm *.o

# Build standard .cpp files from LLVM
for i in Fuzzer*.cpp ; do clang++ -fPIC -c -std=c++11 $i  ; done

# We don't want FuzzerMain
rm FuzzerMain.o

# We want our harness instead that provides an InvokeFuzzer() call
clang++ -Wno-writable-strings -fPIC -c -std=c++11 test_harness.cpp

# And our function to fuzz
#clang -fPIC -c -fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters test_function.c

# And our dummy main() Which calls InvokeFuzzer()
#clang -c test_main.c
# And our PG function entry point which calls InvokeFuzzer()
clang -I`/usr/local/pgsql/bin/pg_config --includedir-server` -fPIC -c test_pg.c

# Now link them all together
clang++ -shared -o test.so -fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters *.o
