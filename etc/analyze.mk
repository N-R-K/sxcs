# convenient makefile to run some static analyzers:
#	$ make -f etc/analyze.mk

nproc != nproc
MAKEFLAGS := -j$(nproc)

cppcheck_flags = \
	--std=c89 --quiet --inline-suppr \
	--enable=performance,portability,style \
	--max-ctu-depth=16

analyze: analyze-gcc analyze-cppcheck analyze-clang-tidy analyze-clang-weverything
analyze-gcc:
	gcc sxcs.c -o /dev/null -c -std=c89 -Wall -Wextra -Wpedantic -fanalyzer \
		-Ofast -fwhole-program
analyze-cppcheck:
	cppcheck sxcs.c ${cppcheck_flags} \
		--suppress=normalCheckLevelMaxBranches
analyze-cppcheck-exhaustive:
	cppcheck sxcs.c ${cppcheck_flags} -j ${nproc} \
		--force --check-level=exhaustive
analyze-clang-tidy:
	clang-tidy --quiet --config-file=./etc/.clang-tidy sxcs.c -- -std=c89
analyze-clang-weverything:
	clang sxcs.c -o /dev/null -c -std=c89 -Ofast -Weverything \
		-Wno-unreachable-code-break -Wno-string-conversion \
		-Wno-unused-macros -Wno-comma -Wno-padded \
		-Wno-disabled-macro-expansion -Wno-unsafe-buffer-usage \
		-Wno-sign-conversion

.PHONY: analyze
