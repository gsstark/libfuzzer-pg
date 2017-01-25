#!/usr/bin/python

import os
import psycopg2
import sys
import pprint
import itertools

# xml requires xml support
testable_types = ['text','cstring','bytea','name','json','jsonb']
testable_types_sql = '[' + ','.join(["'%s'" % t for t in testable_types]) + ']'

functions_query = (
    "SELECT proname, proargtypes::regtype[]::text[]"
    "  FROM pg_proc"
    " WHERE (provolatile = 'i' OR provolatile = 's')"
    "   AND      proargtypes::regtype[] && array["+testable_types_sql+"]::regtype[]"
    "   AND  NOT proargtypes::regtype[] && array['internal']::regtype[]"
    "   AND provariadic = 0"
    "   AND  NOT proisagg"
    "   AND  NOT proiswindow"
    )

conn_string = "host='/tmp'"

# These are just too slow to fuzz
problem_functions = [
    'ts_debug',
    'database_to_xmlschema',
    'database_to_xml',
    'database_to_xml_and_xmlschema'
    'nameregexeq',
    'nameregexne',
    'textregexeq',
    'textregexne',
    'texticregexeq',
    'texticregexne',
    'nameicregexeq',
    'nameicregexne',
    'bpcharicregexeq',
    'bpcharicregexne',
    'bpcharregexeq',
    'bpcharregexne',
    'regexp_replace',
    'regexp_replace',
    'regexp_match',
    'regexp_match',
    'regexp_matches',
    'regexp_matches',
    'regexp_split_to_table',
    'regexp_split_to_table',
    'regexp_split_to_array',
    'regexp_split_to_array',
    'regexeqsel',
    'icregexeqsel',
    'regexnesel',
    'icregexnesel',
    'regexeqjoinsel',
    'icregexeqjoinsel',
    'regexnejoinsel',
    'icregexnejoinsel',
]

dummy_args = {
#    '"any"' : '',
#    'anyarray' : '',
#    'anyelement' : '',
#    'anynonarray' : '',
#    'refcursor' : '',
#    'xml' : '',

    'abstime' : ['0'],
    'bigint' : ['0'],
    'boolean' : ['f'],
    'bytea' : [''],
    'character' : [''],
    'date' : ['2000-01-01'],
    'double precision' : ['0.0'],
    'integer' : ['0'],
    'interval' : ['1 second'],
    'json' : ['{}'],
    'jsonb' : ['{}'],
    'name' : [''],
    'numeric' : ['0'],
    'oid' : ['23'],
    'real' : ['0.0'],
    'regclass' : ['pg_proc'],
    'regconfig' : ['3748'],
    'regdictionary' : ['37650'],
    'reltime' : ['0'],
    'smallint' : ['0'],
    'text' : [''],
    'text[]' : ['{}'],
    'time with time zone' : ['12:00:00'],
    'time without time zone' : ['12:00:00'],
    'timestamp with time zone' : ['2000-01-01 12:00:00'],
    'timestamp without time zone' : ['2000-01-01 12:00:00'],
    'tsquery' : [''],
    'tsvector' : [''],
}

def fuzz(proname, proargs, arg_to_test):
    arglists = []
    for i in range(0,len(proargs)):
        arg = proargs[i]
        if i == arg_to_test:
            arglists.append(["$1::%s" % proargs[i]])
        elif arg not in dummy_args:
            return
        else:
            arglists.append(["'%s'::%s" % (d,proargs[i]) for d in dummy_args[proargs[i]]])
    for args in itertools.product(*arglists):
        query = 'select "%s"(%s)' % (proname, ', '.join(args))
        print(query)

    # Need a fresh connection due to fuzzer being only capable of running once
    with psycopg2.connect(conn_string) as connection:
        with connection.cursor() as cur:
            cur.execute("set max_stack_depth='7680kB'")
            try:
                cur.execute("select fuzz(100000, '%s')" % query.replace("'", "''"))
            except psycopg2.Error as e:
                print e.pgcode
                print e.pgerror
                pass

def main():
    with psycopg2.connect(conn_string) as connection:
        with connection.cursor() as cur:
            cur.execute(functions_query)
            functions = cur.fetchall()
    for f in functions:
        (proname,proargs) = f
        if proname in problem_functions:
            continue
        for i in range(0,len(proargs)):
            if proargs[i] in testable_types:
                fuzz(proname, proargs, i)

if __name__ == "__main__":
    main()
