#include "Fuzzer/FuzzerInterface.h"
#include "Fuzzer/FuzzerInternal.h"
#include <string.h>
#include <signal.h>

extern "C" int FuzzOne(const uint8_t *Data, size_t Size);
extern "C" int GoFuzz(unsigned runs);
extern "C" void aborthandler(int signum, siginfo_t *info, void *cxt);
extern "C" void staticdeathcallback();
//extern "C" void errorcallback(const char *errorname);

int GoFuzz(unsigned runs) {
	char runarg[] = "-runs=400000000999";
	sprintf(runarg, "-runs=%u", runs);
	char *argvdata[] = {
		"PostgresFuzzer",
		runarg,
		"-verbosity=1",
		"-only_ascii=1",
		"-timeout=60",
		"-report_slow_units=1",
		"-use_traces=1",
		"/var/tmp/corpus",
		"-max_len=32",
		NULL
	};
	char **argv = argvdata;
	int argc = sizeof(argv)/sizeof(*argv) - 1;

	/* Catch abort and print out the test case */
	struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_sigaction = aborthandler;
	sigaction(SIGABRT, &sigact, 0);

	return fuzzer::FuzzerDriver(&argc, &argv, FuzzOne);
}

void aborthandler(int signum, siginfo_t *info, void *cxt) {
#if 0
	fuzzer::Fuzzer::StaticDeathCallback();
	exit(0);
#endif
	raise(SIGSEGV);
}

//void staticdeathcallback() {
//	fuzzer::Fuzzer::StaticDeathCallback();
//}	

//void errorcallback(const char *errorname) {
//	fuzzer::Fuzzer::StaticErrorCallback(errorname);
//}	
