# libnet (PL/I-centric) — thin C bridge + PL/I library, targeting pli-llvm.
#
# Requires pli-llvm's build artifacts (projects/plic/pli-llvm/build). Override
# PLI_LLVM if checked out elsewhere.
#
# Design: the C bridge is the ONLY compiled C; all connection logic lives in
# %included PL/I (net_base.inc / net_server.inc), so it compiles inline into
# each program. libnet.a is just the C bridge; programs do `%include net;`
# and link -lnet + libpli.a.
#
# NOTE: `make all` succeeds today (it only builds the C bridge). Compiling a
# *program* blocks on the pli-llvm wishlist (see WISHLIST.md): CONTROLLED
# storage, variable-length SUBSTR, char(*), BASED(P) on params, based-member
# RETURN. `make example` shows exactly where.

PLI_LLVM ?= ../plic/pli-llvm/build
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

INC     = -I include
OBJS    = c_bridge.o
BUILD  ?= .build
DIST_INC = dist/net.inc
INC_SRCS = include/net.inc include/net_base.inc include/net_server.inc \
           include/net_errors.inc include/errno.inc include/c_bridge.inc \
           include/type_defs.inc

.PHONY: all clean install uninstall test example

all: libnet.a $(DIST_INC)

c_bridge.o: source/c_bridge.c
	$(CC) $(CFLAGS) -c $< -o $@

libnet.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(DIST_INC): $(INC_SRCS)
	mkdir -p dist
	> $@
	for f in $^; do \
	  sed '/^[[:space:]]*%include/d' $$f >> $@; \
	done

# Compile one program (PL/I) and link the C bridge + runtime.
build-prog: libnet.a
	@test -n "$(SRC)" || { echo "usage: make build-prog SRC=examples/foo.pli [OUT=foo]"; exit 1; }
	$(PLIC) $(PLIFLAGS) -c $(SRC) $(INC) -o $(OUT).o
	$(CC) -o $(OUT) $(OUT).o libnet.a $(RTLIB)

# Diagnostic: attempt to compile each example and show the wishlist gap.
EXAMPLES = examples/echo_server.pli examples/client.pli examples/resolve.pli
example: libnet.a
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

# `make test` runs the C bridge regression test (works today). The PL/I
# program path additionally requires the pli-llvm wishlist (see WISHLIST.md).
test: all
	@mkdir -p $(BUILD)
	@$(CC) $(CFLAGS) source/c_bridge.c tests/c_bridge.c -o $(BUILD)/cb_test && \
	  $(BUILD)/cb_test

clean:
	rm -f $(OBJS) libnet.a
	rm -rf dist $(BUILD)
