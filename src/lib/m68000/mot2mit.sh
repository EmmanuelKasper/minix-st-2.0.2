#!/bin/sh

sed <$1	-e 's/\!.*$//g'			\
	-e 's/\.define/.globl/g'		\
	-e 's/\.extern/.globl/g'		\
	-e 's/\.sect.*\.sect.*$//g'		\
	-e 's/\.sect.*\.rom//g'			\
	-e 's/\.sect.*\.bss//g'			\
	-e 's/\.sect.*\.end//g'			\
	-e 's/\.sect//g'			\
	-e 's/[^a-zA-Z0-9_]\(cmp\)\.b.*\((a[0-6])+,(a[0-6])+\)/\1mb \2/g' \
	-e 's/[^a-zA-Z0-9_]\(cmp\)\([^.m]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(tst\)\([^.]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(or\)\([^.]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(and\)\([^.]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(add\)\([^.]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(sub\)\([^.]\)/\1w\2/g'	\
	-e 's/[^a-zA-Z0-9_]\(move\)\([^.m]\)/\1w\2/g'	\
	-e 's/\(.*\)\.\([blws][ 	]\)/\1\2/g'	\
	-e 's/\(jsr\)\(.*\)$/jbsr	\2/g'	\
	-e 's/-(sp)/a7@-/g'			\
	-e 's/(sp)+/a7@+/g'			\
	-e 's/-(\(a[0-7]\))/\1@-/g'		\
	-e 's/(\(a[0-7]\))+/\1@+/g'		\
	-e 's/\([-0-9][0-9]*[+-][0-9][0-9]*[+-][0-9][0-9]*[+-][0-9][0-9]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-0-9][0-9]*[+-][0-9][0-9]*[+-][0-9][0-9]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-0-9][0-9]*[+-][0-9][0-9]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-0-9][0-9]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-a-zA-Z0-9_][a-zA-Z0-9_]*[+-][0-9][0-9]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-a-zA-Z0-9_][a-zA-Z0-9_]*\)(\(a[0-7]\))/\2@(\1)/g'	\
	-e 's/\([-a-zA-Z0-9_][a-zA-Z0-9_]*\)(\(sp\))/\2@(\1)/g'		\
	-e 's/(sp),\(.*\)/a7@,\1/g'		\
	-e 's/(\(a[0-9]\))/\1@/g'		\
	-e 's/\.data4\(.*\)$/\.long	\1/g'	\
	-e 's/\(sp\),\(.*\)/a7,\2/g'		\
	-e 's/\(.*\),\(sp\)/\1,a7/g'		\
	-e 's/'\''\(.*\)'\''/"\1"/g'
