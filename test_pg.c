#include "postgres.h"
#include "funcapi.h"

#include <stdio.h>

extern void GoFuzz();

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(fuzz);

Datum
fuzz(PG_FUNCTION_ARGS)
{
	GoFuzz();
	PG_RETURN_NULL();
}		



#include "tsearch/ts_type.h"

void FuzzOne(const unsigned char *Data, size_t Size) {
	static StringInfoData data;
	data.data = (char *)Data;
	data.len = Size;
	data.maxlen = Size;
	data.cursor = 0;

	Datum retval;

	printf("Called with Data=%p size=%zu\n", Data, Size);

	/* Not sure why we're being passed NULL */
	if (!Data)
		return;

	PG_TRY();
	{
		retval = DirectFunctionCall3(tsvectorrecv,
									 PointerGetDatum(&data), /* fake stringinfo */
									 ObjectIdGetDatum(InvalidOid), /* typeelem */
									 Int32GetDatum(-1) /* typmod */
									 );
		(void) retval;
	}
	PG_CATCH();
	{
		return;
	}
	PG_END_TRY();

}
