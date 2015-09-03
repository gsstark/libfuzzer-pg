#include "FuzzerInterface.h"
#include "FuzzerInternal.h"

// This function should be defined by the user.
extern "C" void FuzzOne(const uint8_t *Data, size_t Size);
extern "C" int GoFuzz();

int GoFuzz() {
	int argc = 6;
	char *argv[] = { "PostgresFuzzer",
					 "-verbosity=9",
					 "-iterations=100",
					 "-runs=10",
					 "-save_minimized_corpus=1",
					 "/var/tmp/corpus",
					 NULL };
	return fuzzer::FuzzerDriver(argc, argv, FuzzOne);
}
