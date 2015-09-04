#include "postgres.h"
#include "funcapi.h"
#include "executor/spi.h"

#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "access/xact.h"

#define SUBTRANSACTIONS

extern void GoFuzz();

PG_MODULE_MAGIC;

SPIPlanPtr plan;

PG_FUNCTION_INFO_V1(fuzz);
Datum
fuzz(PG_FUNCTION_ARGS)
{
	unsigned runs = PG_GETARG_INT32(0);
	text *expr_text = PG_GETARG_TEXT_P(1);
	char *expr = text_to_cstring(expr_text);
	Oid argtypes[1] = { TEXTOID };
	int retval;

	if (runs > 400000000)
		elog(ERROR, "Unreasonable number of runs");

	retval = SPI_connect();
	if (retval != SPI_OK_CONNECT)
		abort();
	plan = SPI_prepare(expr, 1, argtypes);
	if (!plan)
		elog(ERROR, "Failed to plan query");

	retval = SPI_getargcount(plan);
	if (retval != 1)
		elog(ERROR, "Query to fuzz must take precisely one parameter");

	GoFuzz(runs);
	PG_RETURN_NULL();
}		

void FuzzOne(const char *Data, size_t Size) {
	text *arg = cstring_to_text_with_len(Data, Size);

	static unsigned long n_execs, n_success, n_fail, n_null;
	MemoryContext oldcontext = CurrentMemoryContext;
 	ResourceOwner oldowner = CurrentResourceOwner;

	n_execs++;

	/* Not sure why we're being passed NULL */
	if (!Data) {
		n_null++;
		return;
	}

#if 0
	static MemoryContext tmp_cxt;
	if (!tmp_cxt) {
		tmp_cxt = AllocSetContextCreate(CurrentMemoryContext,
										"Temp Fuzzer SPI Context",  
										ALLOCSET_DEFAULT_MINSIZE,
										ALLOCSET_DEFAULT_INITSIZE,
										ALLOCSET_DEFAULT_MAXSIZE);
	}
	MemoryContextReset(tmp_cxt);
#endif

	BeginInternalSubTransaction(NULL);
	MemoryContextSwitchTo(CurrentMemoryContext);

 	PG_TRY();
 	{
		Datum values[1] = { PointerGetDatum(arg) };

		int retval = SPI_execute_plan(plan, values,
								  NULL /* nulls */,
								  true, /* read-only */
								  0 /* max rows */);
		SPI_freetuptable(SPI_tuptable);

		if (retval == SPI_OK_SELECT)
			n_success++;
		else if (retval >= 0)
			fprintf(stderr, "SPI reports non-select run retval=%d\n", retval);
		else
			abort();

		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;
		SPI_restore_connection();
 	}
 	PG_CATCH();
 	{
		/* Save error info */
		MemoryContextSwitchTo(oldcontext);
		/* ErrorData  *edata = CopyErrorData(); */
		FlushErrorState();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;

		SPI_restore_connection();

		n_fail++;
	}
	PG_END_TRY();

	/* WTF? */
	pfree(arg);

	/* Every power of two executions print progress */
	if ((n_execs & (n_execs-1)) == 0) {
		fprintf(stderr, "FuzzOne n=%lu  success=%lu  fail=%lu  null=%lu\n", n_execs, n_success, n_fail, n_null);
		MemoryContextStats(TopMemoryContext);
	}
}
