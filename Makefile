# debug
_DFLAGS      ?= -g -fsanitize=address,undefined,leak
# optimizations
O_BASIC       = -pipe -march=native -Ofast
O_LTO         = -flto=auto -fuse-linker-plugin
O_GRAPHITE    = -fgraphite-identity -floop-nest-optimize
O_IPAPTA      = -fipa-pta
O_SEMINTERPOS = -fno-semantic-interposition
O_NOCOMMON    = -fno-common
O_NOPLT       = -fno-plt
O_NOPIE       = -no-pie
O_NOSSP       = -fno-stack-protector
OFLAGS = $(O_BASIC) $(O_LTO) $(O_GRAPHITE) $(O_IPAPTA) \
         $(O_SEMINTERPOS) $(O_NOCOMMON) $(O_NOPLT) \
         $(O_NOPIE) $(O_NOSSP) \

# warnings
WGCC   = -Wlogical-op
WGCC  += -fanalyzer
WCLANG = -Weverything
WCLANG += -Wno-padded -Wno-comma -Wno-missing-noreturn -Wno-unused-macros
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
FEAT_CPP  = -D_POSIX_C_SOURCE=200809L

# Cool stuff
CC       ?= cc
CFLAGS   ?= $$(test "$(CC)" = "gcc" && printf "%s " $(OFLAGS))
CFLAGS   += $(WFLAGS) $(DFLAGS)
CPPFLAGS  = $(DEBUG_CPP) $(PROGNAME)
LDFLAGS  ?= $(CFLAGS)
LDLIBS    = $(X11_LIBS)

PREFIX   ?= /usr/local
PROGNAME  = -DPROGNAME=\"$(BIN)\"


BIN  = sxcs
OBJS = sxcs.o

.PHONY: clean
.SUFFIXES:
.SUFFIXES: .c .o

all: $(BIN)
$(OBJS): Makefile config.h

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

.c.o:
	$(CPPCHECK) $(CPPCHECK_ARGS) $<
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

config.h:
	cp config.def.h config.h

debug:
	make BIN="$(BIN)-debug" DFLAGS="$(_DFLAGS)" DEBUG_CPP="-DDEBUG" all

clean:
	rm -f *.o $(OBJS) $(BIN) $(BIN)-debug

install: $(BIN)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BIN) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

