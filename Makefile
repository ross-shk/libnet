# libnet (PL/I-centric) — thin C bridge + compiled PL/I module, targeting pli-llvm.
#
# Requires pli-llvm's build artifacts (projects/plic/pli-llvm/build). Override
# PLI_LLVM if checked out elsewhere.
#
# Design: the C bridge (source/c_bridge.c) is the ONLY C. All connection logic
# lives in a single compiled PL/I module (source/net.pli), archived into
# libnet.a alongside the C bridge. Programs do `%include net;` (an interface
# include: constants + conditions + external ENTRY declarations), then link
# -lnet + libpli.a. Nothing is %included inline into the program — the library
# is a real linked module, as if pli-llvm fully supports external PL/I
# modules, CONTROLLED storage, char(*), BASED on params, and based-member
# access.
#
# NOTE: `make test` (the C bridge) passes today. `make all` builds the C bridge
# and then compiles source/net.pli, which blocks on the pli-llvm wishlist for
# the features it uses (see WISHLIST.md). `make example` shows exactly where.

PLI_LLVM ?= /usr/local/bin
PLIC     ?= $(PLI_LLVM)/plic
RTLIB    ?= $(PLI_LLVM)/libpli.a
CC       ?= cc
AR       ?= ar
CFLAGS   ?= -O2 -Wall
PLIFLAGS ?=

PREFIX ?= /usr/local
INCDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
PKGDIR ?= $(LIBDIR)/pkgconfig

INC      = -I include
OBJS     = c_bridge.o net.o
BUILD   ?= .build
DIST_INC = dist/net.inc

INC_SRCS = include/net.inc include/type_defs.inc include/errno.inc

.PHONY: all clean install uninstall test example

all: libnet.a $(DIST_INC)

c_bridge.o: source/c_bridge.c
	$(CC) $(CFLAGS) -c $< -o $@

net.o: source/net.pli include/c_bridge.inc include/type_defs.inc include/errno.inc
	$(PLIC) $(PLIFLAGS) -c $< $(INC) -o $@

libnet.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(DIST_INC): $(INC_SRCS)
	mkdir -p dist
	> $@
	for f in $^; do \
	  sed '/^[[:space:]]*%include/d' $$f >> $@; \
	done

# Compile one program (PL/I) and link the C bridge + PL/I module + runtime.
build-prog: libnet.a
	@test -n "$(SRC)" || { echo "usage: make build-prog SRC=examples/foo.pli OUT=foo"; exit 1; }
	$(PLIC) $(PLIFLAGS) -c $(SRC) $(INC) -o $(OUT).o
	$(CC) -o $(OUT) $(OUT).o libnet.a $(RTLIB)

# Diagnostic: attempt to compile each example against the interface include
# and show the wishlist gap. The examples block where the program path needs
# pli-llvm features (see WISHLIST.md); the module build is a separate step.
EXAMPLES = examples/echo_server.pli examples/client.pli examples/resolve.pli examples/fetch.pli
example:
	@for e in $(EXAMPLES); do \
	  echo "== compiling $$e (expect wishlist gaps) =="; \
	  -$(PLIC) $(PLIFLAGS) -c $$e $(INC) -o /tmp/$$(basename $$e .pli).o; \
	done

install: libnet.a $(DIST_INC)
	install -d $(DESTDIR)$(INCDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PKGDIR)
	install -m 644 $(DIST_INC) $(DESTDIR)$(INCDIR)/net.inc
	install -m 644 libnet.a $(DESTDIR)$(LIBDIR)/

uninstall:
	rm -f $(DESTDIR)$(INCDIR)/net.inc $(DESTDIR)$(LIBDIR)/libnet.a

# `make test` runs the C bridge regression test. It builds the test directly
# from source/c_bridge.c so the working C path stays runnable even while the
# PL/I module (source/net.pli) blocks on the pli-llvm wishlist. The PL/I
# program path additionally requires that wishlist (see WISHLIST.md).
test:
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) source/c_bridge.c tests/c_bridge.c -o $(BUILD)/cb_test && \
	  $(BUILD)/cb_test

clean:
	rm -f $(OBJS) libnet.a
	rm -rf dist $(BUILD)
