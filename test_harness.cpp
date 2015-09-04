#include "FuzzerInterface.h"
#include "FuzzerInternal.h"

// This function should be defined by the user.
extern "C" void FuzzOne(const uint8_t *Data, size_t Size);
extern "C" int GoFuzz(unsigned runs);

int GoFuzz(unsigned runs) {
	int argc = 5;

	char runarg[] = "-runs=400000000999";
	sprintf(runarg, "-runs=%u", runs);
	char *argv[] = { "PostgresFuzzer",
					 "-verbosity=1",
					 runarg,
					 "-save_minimized_corpus=1",
					 "/var/tmp/corpus",
					 NULL };
	return fuzzer::FuzzerDriver(argc, argv, FuzzOne);
}
