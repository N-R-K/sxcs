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
WGCC   = -Wlogical-op -Wcast-align=strict
WGCC  += -fanalyzer
WCLANG = -Weverything
WCLANG += -Wno-padded -Wno-comma -Wno-unused-macros
WCLANG += -Wno-implicit-fallthrough -Wno-unreachable-code-break -Wno-unreachable-code-return
WFLAGS = -std=c89 -Wall -Wextra -Wpedantic \
         -Wshadow -Wvla -Wpointer-arith -Wwrite-strings -Wfloat-equal \
         -Wcast-align -Wcast-qual -Wbad-function-cast \
         -Wstrict-overflow=2 -Wunreachable-code -Wformat=2 \
         -Wundef -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes \
         $$(test "$(CC)" = "gcc" && printf "%s " $(WGCC)) \
         $$(test "$(CC)" = "clang" && printf "%s " $(WCLANG)) \

# CPPCHECK
CPPCHECK      = $$(command -v cppcheck 2>/dev/null || printf ":")
CPPCHECK_ARGS = --std=c89 --quiet --inline-suppr \
                --enable=performance,portability,style \
                --max-ctu-depth=8 -j8 \
                --suppress=syntaxError --suppress=internalAstError \

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
	$(CPPCHECK) $(CPPCHECK_ARGS) $<
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

run:
	tcc $(CPPFLAGS) -DDFLAGS="$(_DFLAGS)" -DDEBUG $(LDLIBS) -b -run $(BIN).c

clean:
	rm -f *.o $(OBJS) $(BIN) $(BIN)-debug version.h

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BIN) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

