#include "FuzzerInterface.h"
#include "FuzzerInternal.h"

// This function should be defined by the user.
extern "C" void LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);
extern "C" int InvokeFuzzer();

int InvokeFuzzer() {
	int argc = 3;
	char *argv[] = { "PostgresFuzzer", "-verbosity=9", "/var/tmp/corpus", NULL };
	return fuzzer::FuzzerDriver(argc, argv, FuzzOne);
}
