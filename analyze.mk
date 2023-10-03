# convenient makefile to run some static analyzers:
#	$ make -f analyze.mk

nproc != nproc
MAKEFLAGS := -j$(nproc)

analyze: analyze-gcc analyze-cppcheck analyze-clang-tidy analyze-clang-weverything
analyze-gcc:
	gcc sxcs.c -o /dev/null -c -std=c89 -Wall -Wextra -Wpedantic -fanalyzer \
		-Ofast -fwhole-program
analyze-cppcheck:
	cppcheck sxcs.c --std=c89 --quiet --inline-suppr --force \
		--enable=performance,portability,style --max-ctu-depth=16
analyze-clang-tidy:
	clang-tidy sxcs.c --quiet -- -std=c89
analyze-clang-weverything:
	clang sxcs.c -o /dev/null -c -std=c89 -Ofast -Weverything \
		-Wno-unreachable-code-break -Wno-string-conversion \
		-Wno-unused-macros -Wno-comma -Wno-padded \
		-Wno-disabled-macro-expansion -Wno-unsafe-buffer-usage

.PHONY: analyze
