#include "postgres.h"
#include "funcapi.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "access/xact.h"

#include <string.h>

#define SUBTRANSACTIONS


extern void GoFuzz();
extern void staticdeathcallback();

size_t
WatchMemoryContextStats(MemoryContext context);
static void
MemoryContextStatsInternal(MemoryContext context, int level,
						   MemoryContextCounters *totals);


PG_MODULE_MAGIC;

SPIPlanPtr plan;

static bool alldone=0;
void fuzz_exit_handler(int code, Datum arg) {
	if (!alldone)
		staticdeathcallback();
}

/* Postgres SQL Function to invoke fuzzer */

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

	on_proc_exit(fuzz_exit_handler, 0);

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

	SPI_finish();

	alldone = 1;

	PG_RETURN_NULL();
}		

/* 
 * Callback from fuzzer to execute one fuzz test case as set up in
 * global "plan" variable by fuzz() 
 */

void FuzzOne(const char *Data, size_t Size) {
	text *arg = cstring_to_text_with_len(Data, Size);

	static unsigned long n_execs, n_success, n_fail, n_null;
	MemoryContext oldcontext = CurrentMemoryContext;
 	ResourceOwner oldowner = CurrentResourceOwner;

	CHECK_FOR_INTERRUPTS();

	n_execs++;

	/* Not sure why we're being passed NULL */
	if (!Data) {
		n_null++;
		return;
	}

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
		ErrorData  *edata = CopyErrorData();
		FlushErrorState();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;

		SPI_restore_connection();

		n_fail++;

		/* INTERNAL_ERROR is definitely a bug. The other two are
		 * debatable but in particular we're interested in infinite
		 * recursion caught by check_for_stack_depth() which shows up
		 * as STATEMENT_TOO_COMPLEX which is in the
		 * PROGRAM_LIMIT_EXCEEDED category
		 */
		int sqlerrcode = ERRCODE_TO_CATEGORY(edata->sqlerrcode);
		if (sqlerrcode == ERRCODE_PROGRAM_LIMIT_EXCEEDED ||
			sqlerrcode == ERRCODE_INSUFFICIENT_RESOURCES ||
			sqlerrcode == ERRCODE_INTERNAL_ERROR)
			{
				/* Force it to be fatal */
				PG_exception_stack = NULL;
				edata->elevel = ERROR;
				ReThrowError(edata);
				/* canthappen */
				abort();
			}
		else
			{
				FreeErrorData(edata);
			}
	}
	PG_END_TRY();

	pfree(arg);

	/* Every power of two executions print progress */
	if ((n_execs & (n_execs-1)) == 0) {
		static int  old_n_execs;
		fprintf(stderr, "FuzzOne n=%lu  success=%lu  fail=%lu  null=%lu\n", n_execs, n_success, n_fail, n_null);
		size_t totaldiff = WatchMemoryContextStats(TopMemoryContext);
		unsigned long ndiff = n_execs - old_n_execs;
		if (ndiff > 0 && totaldiff > 0)
			fprintf(stderr, "Memory used: %lu bytes in %lu calls (%lu bytes/call)\n", totaldiff, ndiff, totaldiff / ndiff);
		if ((totaldiff > 0 && n_execs > 200) || (totaldiff > 10000 && n_execs > 5)) {
			MemoryContextStats(TopMemoryContext);
		}		
		old_n_execs = n_execs;
	}
}


size_t
WatchMemoryContextStats(MemoryContext context)
{
	int totaldiff;
	static MemoryContextCounters old_totals;
	MemoryContextCounters grand_totals;

	memset(&grand_totals, 0, sizeof(grand_totals));
	MemoryContextStatsInternal(context, 0, &grand_totals);
	totaldiff = grand_totals.totalspace - old_totals.totalspace;

	if (totaldiff > 0)
		
		fprintf(stderr,
				"Memory Use Summary: %zu bytes in %zd blocks; %zu free (%zd chunks); %zu used\n",
				grand_totals.totalspace, grand_totals.nblocks,
				grand_totals.freespace, grand_totals.freechunks,
				grand_totals.totalspace - grand_totals.freespace);
		
	old_totals = grand_totals;

	return totaldiff;
}

/*
 * MemoryContextStatsInternal
 *		One recursion level for MemoryContextStats
 *
 * Copied from mcxt.c with the printouts and max_children removed
 *
 */
static void
MemoryContextStatsInternal(MemoryContext context, int level,
						   MemoryContextCounters *totals)
{
	MemoryContext child;
	int			ichild;

	AssertArg(MemoryContextIsValid(context));

	/* Examine the context itself */
	(*context->methods->stats) (context, level, false, totals);

	/* Examine children */
	for (child = context->firstchild, ichild = 0;
		 child != NULL;
		 child = child->nextchild, ichild++)
	{
		MemoryContextStatsInternal(child, level + 1,
								   totals);
	}
}
