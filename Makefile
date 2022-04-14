# debug
_DFLAGS      ?= -g -fsanitize=address,undefined,leak
# optimizations
# TODO: make this "portable." currently, lot of the flags are gcc specific
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
# WCLANG = -Weverything
WFLAGS = -std=c89 -Wall -Wextra -Wpedantic \
         -Wshadow -Wvla -Wpointer-arith -Wwrite-strings -Wfloat-equal \
         -Wcast-align -Wcast-qual -Wbad-function-cast \
         -Wstrict-overflow=4 -Wunreachable-code -Wformat=2 \
         -Wundef -Wstrict-prototypes -Wmissing-declarations -Wmissing-prototypes \
         $(WGCC) $(WCLANG) \

# CPPCHECK
CPPCHECK      = $$(command -v cppcheck 2>/dev/null || printf ":")
CPPCHECK_ARGS = --std=c89 --quiet --inline-suppr \
                --enable=performance,portability,style \
                --max-ctu-depth=8 -j8 \
                --suppress=syntaxError --suppress=internalAstError \

# libs
X11_LIBS  = -lX11

# Cool stuff
CC       ?= gcc
CFLAGS   ?= $(OFLAGS)
CFLAGS   += $(WFLAGS) $(DFLAGS)
CPPFLAGS  = $(DEBUG_CPP)
LDFLAGS  ?= $(CFLAGS)
LDLIBS    = $(X11_LIBS)

BIN  = sxcp
OBJS = sxcp.o util.o

.PHONY: clean
.SUFFIXES:
.SUFFIXES: .c .o

all: $(BIN)
$(OBJS): Makefile util.h

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

.c.o:
	$(CPPCHECK) $(CPPCHECK_ARGS) $<
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

debug:
	make BIN="$(BIN)-debug" DFLAGS="$(_DFLAGS)" DEBUG_CPP="-DDEBUG" all

clean:
	rm -f *.o $(OBJS) $(BIN) $(BIN)-debug

