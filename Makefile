# debug
_DFLAGS      ?= -g -fsanitize=address,undefined,leak
# optimizations
O_BASIC       = -pipe -march=native -Ofast
O_LTO         = -flto=auto -fuse-linker-plugin
O_GRAPHITE    = -fgraphite-identity -floop-nest-optimize
O_IPAPTA      = -fipa-pta
O_BUILTIN     = -fbuiltin
O_SEMINTERPOS = -fno-semantic-interposition
O_NOCOMMON    = -fno-common
O_NOPLT       = -fno-plt
O_NOPIE       = -no-pie
O_NOSSP       = -fno-stack-protector
OFLAGS = $(O_BASIC) $(O_LTO) $(O_GRAPHITE) $(O_IPAPTA) \
         $(O_SEMINTERPOS) $(O_NOCOMMON) $(O_NOPLT) \
         $(O_NOPIE) $(O_NOSSP) $(O_BUILTIN) \

# fallback if $(CC) != "gcc"
O_FALLBACK    = -O3

# warnings
STD    = c89
WGCC   = -Wlogical-op -Wcast-align=strict
WGCC  += -fanalyzer
WCLANG = -Weverything
WCLANG += -Wno-padded -Wno-comma -Wno-unused-macros
WCLANG += -Wno-implicit-fallthrough -Wno-unreachable-code-break -Wno-unreachable-code-return
WFLAGS = -std=$(STD) -Wall -Wextra -Wpedantic \
         -Wshadow -Wvla -Wpointer-arith -Wwrite-strings -Wfloat-equal \
         -Wcast-align -Wcast-qual -Wbad-function-cast \
         -Wstrict-overflow=2 -Wunreachable-code -Wformat=2 \
         -Wundef -Wstrict-prototypes -Wmissing-declarations \
         -Wmissing-prototypes -Wold-style-definition  \
         $$(test "$(CC)" = "gcc" && printf "%s " $(WGCC)) \
         $$(test "$(CC)" = "clang" && printf "%s " $(WCLANG)) \

# CPPCHECK
CPPCHECK      = $$(command -v cppcheck 2>/dev/null || printf ":")
CPPCHECK_ARGS = --std=$(STD) --quiet --inline-suppr --force \
                --enable=performance,portability,style \
                --max-ctu-depth=8 -j8 \
                --suppress=syntaxError --suppress=internalAstError \

CTIDY      = $$(command -v clang-tidy 2>/dev/null || printf ":")
CTIDY_ARGS = --quiet --warnings-as-errors="*" \
             --checks="$$(sed '/^\#/d' .clangtidychecks | paste -d ',' -s)"

# libs
X11_LIBS  = -lX11 -lXcursor
FEAT_CPP  = -D_POSIX_C_SOURCE=200112L

# Cool stuff
CC       ?= cc
CFLAGS   ?= $$(test "$(CC)" = "gcc" && printf "%s " $(OFLAGS) || printf "%s " $(O_FALLBACK))
CFLAGS   += $(WFLAGS) $(DFLAGS)
CPPFLAGS  = $(DEBUG_CPP) $(PROGNAME) $(FEAT_CPP)
LDFLAGS  ?= $(CFLAGS)
LDLIBS    = $(X11_LIBS)
STRIP    ?= strip

PREFIX   ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man
PROGNAME  = -DPROGNAME=\"$(BIN)\"
VERSION   = Beta


BIN  = sxcs
OBJS = sxcs.o

.PHONY: clean
.SUFFIXES:
.SUFFIXES: .c .o

all: $(BIN)
$(OBJS): Makefile config.h version.h

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)
	$(STRIP) $@

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

config.h:
	cp config.def.h config.h

version.h: Makefile .git/index
	v="$$(git describe --tags --dirty 2>/dev/null || true)"; \
	echo "#define VERSION \"$${v:-$(VERSION)}\"" >$@

.git/index:

debug:
	make BIN="$(BIN)-debug" DFLAGS="$(_DFLAGS)" DEBUG_CPP="-DDEBUG" STRIP=":" all

analyze:
	make clean; make CC="clang" OFLAGS="-march=native -Ofast -flto"
	find . -type f -name '*.c' -print | xargs -I{} $(CPPCHECK) $(CPPCHECK_ARGS) {}
	find . -type f -name '*.c' -print | xargs -I{} $(CTIDY) $(CTIDY_ARGS) {} "--" -std=$(STD) $$(make CC=clang dump_cppflags)

run:
	tcc $(CPPFLAGS) -DDFLAGS="$(_DFLAGS)" -DDEBUG $(LDLIBS) -b -run $(BIN).c

dump_cppflags:
	@echo $(CPPFLAGS)

clean:
	rm -f *.o $(OBJS) $(BIN) $(BIN)-debug version.h

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BIN) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp $(BIN).1 $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

