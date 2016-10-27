#include "postgres.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include <stdio.h>

/* Postgres SQL Function to elog an internal error */

PG_FUNCTION_INFO_V1(fuzz_fail);
Datum
fuzz_fail(PG_FUNCTION_ARGS)
{
	text *msgtext = PG_GETARG_TEXT_P(0);
	char *msgstr = text_to_cstring(msgtext);
	fprintf(stderr, "elogging internal error as requested (%s)\n", msgstr);
	elog(ERROR, "Internal Error requested (%s)", msgstr);
}
