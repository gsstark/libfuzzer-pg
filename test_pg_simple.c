#include "postgres.h"
#include "funcapi.h"
#include "executor/spi.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "catalog/pg_type.h"
#include "utils/array.h"
#include "utils/timeout.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/guc.h"
#include "access/xact.h"
#include "regex/regex.h"

#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

extern void GoFuzz();
extern void staticdeathcallback();
extern void errorcallback(const char *errorname);

static int in_fuzzer;

PG_MODULE_MAGIC;

SPIPlanPtr plan;

void fuzz_exit_handler(int code, Datum arg) {
	if (in_fuzzer)
		abort();
}


static void limit_resources() {
	int i;
	struct rlimit old, new;
	struct {
		char *resource_name;
		int resource;
		rlim_t new_soft_limit;
		rlim_t new_hard_limit;
	} limits[] = {
		// { "max memory size", RLIMIT_AS, 200000000 },
		{ "core file size", RLIMIT_CORE, 0 , 0},
		// { "cpu time", RLIMIT_CPU, 1, 300},
		{ "data seg size", RLIMIT_DATA, 200000000, RLIM_INFINITY},
	};
	
	for (i=0; i<sizeof(limits)/sizeof(*limits); i++) {
		int retval;
		retval = getrlimit(limits[i].resource, &old);
		if (retval < 0) {
			perror("getrlimit");
			abort();
		}
		new.rlim_cur = limits[i].new_soft_limit;
		new.rlim_max = limits[i].new_soft_limit;
		if (new.rlim_max > old.rlim_max)
			new.rlim_max = old.rlim_max;
		fprintf(stderr, "Setting %s to %zd / %zd (was %zd / %zd)\n", 
				limits[i].resource_name, 
				new.rlim_cur, new.rlim_max, 
				old.rlim_cur, old.rlim_max);
		retval = setrlimit(limits[i].resource, &new);
		if (retval < 0) {
			perror("setrlimit");
			abort();
		}
	}
}

PG_FUNCTION_INFO_V1(test_fuzz_environment);
Datum
test_fuzz_environment(PG_FUNCTION_ARGS){
	elog(WARNING, "setting rlimit");
	limit_resources();

	elog(WARNING, "setting statement_timeout");
	SetConfigOption("statement_timeout", "200", PGC_SUSET, PGC_S_OVERRIDE);

	PG_RETURN_NULL();
}	

/* Postgres SQL Function to invoke fuzzer */

SPIPlanPtr plan;

PG_FUNCTION_INFO_V1(fuzz);
Datum
fuzz(PG_FUNCTION_ARGS)
{
	unsigned runs = PG_GETARG_INT32(0);
	text *expr_text = PG_GETARG_TEXT_P(1);
	char *expr = text_to_cstring(expr_text);
	Oid argtypes[1] = { TEXTOID };

	struct rlimit new;
	new.rlim_cur = new.rlim_max = 0;
	setrlimit(RLIMIT_CORE, &new);
	new.rlim_cur = 1; new.rlim_max = 300;
	setrlimit(RLIMIT_CPU, &new);
	new.rlim_cur = 200000000; new.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_DATA);

	SPI_connect();
	/* Prepare once before we start the driver */
	plan = SPI_prepare(expr, 1, argtypes);
	/* Invoke the driver via the C++ code */
	GoFuzz(runs);
	SPI_finish();

	PG_RETURN_NULL();
}		

static struct {
	int errcode;
	int count;
} errcode_counts[100];
int num_counts;

static int inc_errcode_count(int errcode) {
	int i;
	for (i=0;i<num_counts;i++) {
		if (errcode_counts[i].errcode == errcode) {
			return ++errcode_counts[i].count;
		}
	}
	if (num_counts >= 100)
		abort();
	errcode_counts[num_counts].errcode = errcode;
	errcode_counts[num_counts].count = 1;
	num_counts++;
	return 1;
}
static void list_errcode_counts() {
	int i;
	fprintf(stderr, "Error codes seen");
	for (i=0; i<num_counts; i++) {
		fprintf(stderr, " %s:%d", unpack_sql_state(errcode_counts[i].errcode), errcode_counts[i].count);
	}
	fprintf(stderr, "\n");
}

/* 
 * Callback from fuzzer to execute one fuzz test case as set up in
 * global "plan" variable by fuzz() 
 */

int FuzzOne(const char *Data, size_t Size) {
	text *arg = cstring_to_text_with_len(Data, Size);

	MemoryContext oldcontext = CurrentMemoryContext;
 	ResourceOwner oldowner = CurrentResourceOwner;

	BeginInternalSubTransaction(NULL);
 	PG_TRY();
 	{
		Datum values[1] = { PointerGetDatum(arg) };
		int retval;

		/* Slow queries are bad but if they CHECK_FOR_INTERRUPTS often
		   enough then that's not too bad. We must directly call
		   enable_timeout because STATEMENT_TIMEOUT is only armed in
		   postgres.c which SPI bypasses */
		CHECK_FOR_INTERRUPTS();
		enable_timeout_after(STATEMENT_TIMEOUT, 100);

		retval = SPI_execute_plan(plan, values,
								  NULL /* nulls */,
								  true, /* read-only */
								  0 /* max rows */);
		disable_timeout(STATEMENT_TIMEOUT, true);

		SPI_freetuptable(SPI_tuptable);

		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;
		SPI_restore_connection();
 	}
 	PG_CATCH();
 	{
		/* Save error info */
		MemoryContextSwitchTo(oldcontext);
		disable_timeout(STATEMENT_TIMEOUT, true);

		ErrorData  *edata = CopyErrorData();
		inc_errcode_count(edata->sqlerrcode);


		/* Attempt to recover using the subtransaction */
		FlushErrorState();
		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldcontext);
		CurrentResourceOwner = oldowner;
		SPI_restore_connection();

		
		/* INTERNAL_ERROR is definitely a bug. The others debatable but in
		 * particular we're interested in infinite recursion caught by
		 * check_for_stack_depth() which shows up as STATEMENT_TOO_COMPLEX
		 * which is in the PROGRAM_LIMIT_EXCEEDED category
		 */
		int errcategory = ERRCODE_TO_CATEGORY(edata->sqlerrcode);
		int regerrcode = ... // elided for space
		
		if (errcategory == ERRCODE_PROGRAM_LIMIT_EXCEEDED ||
			errcategory == ERRCODE_INSUFFICIENT_RESOURCES ||
			errcategory == ERRCODE_INTERNAL_ERROR ||
			(edata->sqlerrcode == ERRCODE_INVALID_REGULAR_EXPRESSION &&
			 (regerrcode == REG_ESPACE || regerrcode == REG_ASSERT ||
			  regerrcode == REG_INVARG || regerrcode == REG_MIXED  ||
			  regerrcode == REG_ECOLORS)))
			{
				char errorname[80];
				sprintf(errorname, "error-%s", unpack_sql_state(edata->sqlerrcode));
				fprintf(stderr, "Calling errocallback for %s (%s)\n",
						errorname, edata->message);    
				errorcallback(errorname);
				FreeErrorData(edata);
			}
		else
			FreeErrorData(edata);
	}
	PG_END_TRY();

	pfree(arg);

	/* Every power of two executions print progress */
	if ((n_execs & (n_execs-1)) == 0) {
		static int  old_n_execs;
		fprintf(stderr, "FuzzOne n=%lu  success=%lu  fail=%lu  null=%lu\n", n_execs, n_success, n_fail, n_null);
		list_errcode_counts();
		old_n_execs = n_execs;
	}
}



