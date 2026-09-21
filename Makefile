# pli-llvm toolchain (native, no Docker, no Iron Spring runtime).
# plic lives in pli-llvm/build; override PLI_LLVM if checked out elsewhere.
PLI_LLVM   ?= ../plic/pli-llvm/build
PLIC       ?= $(PLI_LLVM)/plic
RTLIB      ?= $(PLI_LLVM)/libpli.a
CC         ?= cc
AR         ?= ar
PLIFLAGS   ?=
CFLAGS     ?= -O2 -Wall
PREFIX     ?= /usr/local
INCDIR     ?= $(PREFIX)/include
LIBDIR     ?= $(PREFIX)/lib
PKGDIR     ?= $(LIBDIR)/pkgconfig

# NOTE: full `make` is currently blocked by pli-llvm language gaps
# (see build log: bare BASED conncb, ENTRY POINTER params,
# OPTIONS(linkage), SIGNAL SET ONCODE, DECLARE CONDITION, char(*),
# variable SUBSTR length, CHAR/FIXED conversions, struct-by-value).
# This Makefile now invokes pli-llvm so failures surface as
# pli-llvm diagnostics instead of a missing Iron Spring toolchain.

INC        = -I include
OBJS       = c_bridge.o net.o net_server.o
DIST_INC   = dist/net.inc
DIST_PC    = dist/net.pc
TEST_SRCS  = $(filter-out tests/server.pli,$(wildcard tests/*.pli))
TEST_SERVER = tests/server

.PHONY: all install uninstall clean distclean test

all: libnet.a $(DIST_INC) $(DIST_PC)

c_bridge.o: source/c_bridge.c
	$(CC) $(CFLAGS) -c $< -o $@

net.o: source/net.pli include/c_bridge.inc include/net_errors.inc include/net_helpers.inc include/type_defs.inc
	$(PLIC) $(PLIFLAGS) -c $< $(INC) -o $@

net_server.o: source/net_server.pli include/c_bridge.inc include/net_errors.inc include/net_helpers.inc include/type_defs.inc
	$(PLIC) $(PLIFLAGS) -c $< $(INC) -o $@

libnet.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)
	rm -f *.o

$(TEST_SERVER): tests/server.pli libnet.a
	$(PLIC) $(PLIFLAGS) -c $< $(INC) -o $@.o
	$(CC) -o $@ $@.o libnet.a $(RTLIB)

$(DIST_INC): include/type_defs.inc include/c_bridge.inc include/net_errors.inc include/net_base.inc include/net_server.inc
	mkdir -p dist
	> $@
	for f in $^; do \
	  sed '/^[[:space:]]*%include/d' $$f >> $@; \
	done

$(DIST_PC): Makefile
	mkdir -p dist
	echo 'prefix=$(PREFIX)' > $@
	echo 'exec_prefix=$${prefix}' >> $@
	echo 'libdir=$(LIBDIR)' >> $@
	echo 'includedir=$(INCDIR)' >> $@
	echo '' >> $@
	echo 'Name: net' >> $@
	echo 'Description: PL/I socket library with C bridge (pli-llvm)' >> $@
	echo 'Version: 1.0.0' >> $@
	echo 'Libs: -L$${libdir} -lnet' >> $@
	echo 'Cflags: -I$${includedir}' >> $@

test: libnet.a $(TEST_SERVER)
	@failed=0; total=0; \
	for src in $(TEST_SRCS); do \
	  name=$$(basename $$src .pli); \
	  total=$$((total+1)); \
	  printf "  %-28s " "$$name"; \
	  $(PLIC) $(PLIFLAGS) -c $$src $(INC) -o $${src%.pli}.o || { echo "COMPILE FAIL"; failed=$$((failed+1)); continue; }; \
	  $(CC) -o $${src%.pli} $${src%.pli}.o libnet.a $(RTLIB) || { echo "LINK FAIL"; failed=$$((failed+1)); continue; }; \
	  ./tests/server > /tmp/$$name.server.out 2>&1 & pid=$$!; sleep 0.7; \
	  ./$${src%.pli} > /tmp/$$name.out 2>&1; rc=$$?; \
	  kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true; \
	  if [ $$rc -eq 0 ]; then echo "PASS"; else echo "FAIL"; cat /tmp/$$name.out; cat /tmp/$$name.server.out; failed=$$((failed+1)); fi; \
	done; \
	echo ""; echo "$$total tests, $$((total - failed)) passed, $$failed failed"; [ $$failed -eq 0 ]

install: libnet.a $(DIST_INC) $(DIST_PC)
	install -d $(DESTDIR)$(INCDIR)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(PKGDIR)
	install -m 644 $(DIST_INC) $(DESTDIR)$(INCDIR)/
	install -m 644 libnet.a $(DESTDIR)$(LIBDIR)/
	install -m 644 $(DIST_PC) $(DESTDIR)$(PKGDIR)/

uninstall:
	rm -f $(DESTDIR)$(INCDIR)/net.inc
	rm -f $(DESTDIR)$(LIBDIR)/libnet.a
	rm -f $(DESTDIR)$(PKGDIR)/net.pc

clean:
	rm -f $(OBJS) libnet.a *.o *.lst *.map
	rm -rf dist
	rm -f tests/*.o tests/*.lst tests/*.map
	rm -f tests/server tests/http_get tests/echo tests/resolve_dial tests/close_shutdown tests/timeout tests/ephemeral tests/send_recv tests/poll tests/nonblocking
	rm -f tests/server.o tests/http_get.o tests/echo.o tests/resolve_dial.o tests/close_shutdown.o tests/timeout.o tests/ephemeral.o tests/send_recv.o tests/poll.o tests/nonblocking.o

distclean: clean uninstall
