#include "postgres.h"
#include "fmgr.h"

#include "tsearch/ts_type.h"

extern void InvokeFuzzer();


void LLVMFuzzerTestOneInput(const unsigned char *Data, size_t Size) {
	static StringInfoData data;
	data.data = Data;
	data.len = Size;
	data.maxlen = Size;
	data.cursor = 0;

	Datum retval;

	retval = DirectFunctionCall1(tsvectorrecv,
						&data, /* fake stringinfo */
						InvalidOid, /* typeelem */
						-1 /* typmod */
						);
	(void) retval;
}
