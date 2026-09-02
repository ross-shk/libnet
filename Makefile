PLIC      ?= plic
CC        ?= gcc
AR        ?= ar
PLIFLAGS  ?= -C -dELF -O
CFLAGS    ?= -m32
LDFLAGS   ?= -m32 -no-pie -z muldefs
LIBS      ?= -lprf
ALT_DIR   ?= /usr/lib/pli/alt
PREFIX    ?= /usr/local
INCDIR    ?= $(PREFIX)/include
LIBDIR    ?= $(PREFIX)/lib
PKGDIR    ?= $(LIBDIR)/pkgconfig

# Docker – run toolchain via ghcr.io/ross-shk/pli on hosts without plic (e.g. macOS)
# Lets you run `make` / `make test` locally without copying to a VM.
DOCKER         ?= docker
DOCKER_IMAGE   ?= ghcr.io/ross-shk/pli:latest
DOCKER_PLATFORM ?= linux/386
DOCKER_RUN     := $(DOCKER) run --rm --platform $(DOCKER_PLATFORM) -v $(CURDIR):/workspace -w /workspace $(DOCKER_IMAGE)
USE_DOCKER     ?= 0
# Auto-enable on macOS where plic is not installed (no VM copy needed)
ifeq ($(origin USE_DOCKER),file)
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    ifeq (,$(shell which plic 2>/dev/null))
      USE_DOCKER := 1
    endif
  endif
endif
ifeq ($(USE_DOCKER),1)
  RUN := $(DOCKER_RUN)
else
  RUN :=
endif

INC        = -i include
OBJS       = c_bridge.o net.o net_server.o
DIST_INC   = dist/net.inc
DIST_PC    = dist/net.pc
TEST_SRCS  = $(filter-out tests/server.pli,$(wildcard tests/*.pli))
TEST_SERVER = tests/server

.PHONY: all install uninstall clean distclean test

all: libnet.a $(DIST_INC) $(DIST_PC)

c_bridge.o: source/c_bridge.c
	$(RUN) $(CC) $(CFLAGS) -c $< -o $@

net.o: source/net.pli include/c_bridge.inc include/net_errors.inc include/net_helpers.inc include/type_defs.inc
	$(RUN) $(PLIC) $(PLIFLAGS) $< $(INC) -o $@

net_server.o: source/net_server.pli include/c_bridge.inc include/net_errors.inc include/net_helpers.inc include/type_defs.inc
	$(RUN) $(PLIC) $(PLIFLAGS) $< $(INC) -o $@

libnet.a: $(OBJS)
	$(RUN) $(AR) rcs $@ $(OBJS)
	rm -f *.o
	rm -f *.lst

$(TEST_SERVER): tests/server.pli libnet.a
	$(RUN) plic $(PLIFLAGS) $< $(INC) -o $@.o; rc=$$?; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 4 ]; then exit $$rc; fi; \
	$(RUN) gcc $(LDFLAGS) -o $@ $@.o libnet.a $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o

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
	echo 'altdir=$(ALT_DIR)' >> $@
	echo '' >> $@
	echo 'Name: net' >> $@
	echo 'Description: PL/I socket library with C bridge' >> $@
	echo 'Version: 1.0.0' >> $@
	echo 'Libs: $${altdir}/fhs.o $${altdir}/ghs.o -L$${libdir} -lnet -lprf' >> $@
	echo 'Cflags: -i$${includedir}' >> $@

test: libnet.a $(TEST_SERVER)
ifeq ($(USE_DOCKER),1)
	@failed=0; total=0; \
	for src in $(TEST_SRCS); do \
	  name=$$(basename $$src .pli); \
	  total=$$((total+1)); \
	  printf "  %-28s " "$$name"; \
	  $(RUN) plic $(PLIFLAGS) $$src $(INC) -o $${src%.pli}.o; rc=$$?; \
	  if [ $$rc -ne 0 ] && [ $$rc -ne 4 ]; then echo "COMPILE FAIL"; failed=$$((failed+1)); continue; fi; \
	  $(RUN) gcc $(LDFLAGS) -o $${src%.pli} $${src%.pli}.o libnet.a $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o || { echo "LINK FAIL"; failed=$$((failed+1)); continue; }; \
	  if $(DOCKER_RUN) bash -c './tests/server & pid=$$!; sleep 0.7; ./'$${src%.pli}'; rc=$$?; kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true; exit $$rc' > /tmp/$$name.out 2>&1; then \
	    echo "PASS"; \
	  else \
	    echo "FAIL"; cat /tmp/$$name.out; failed=$$((failed+1)); \
	  fi; \
	done; \
	echo ""; echo "$$total tests, $$((total - failed)) passed, $$failed failed"; [ $$failed -eq 0 ]
else
	@failed=0; total=0; \
	for src in $(TEST_SRCS); do \
	  name=$$(basename $$src .pli); \
	  total=$$((total+1)); \
	  printf "  %-28s " "$$name"; \
	  plic $(PLIFLAGS) $$src $(INC) -o $${src%.pli}.o; rc=$$?; \
	  if [ $$rc -ne 0 ] && [ $$rc -ne 4 ]; then echo "COMPILE FAIL"; failed=$$((failed+1)); continue; fi; \
	  gcc $(LDFLAGS) -o $${src%.pli} $${src%.pli}.o libnet.a $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o || { echo "LINK FAIL"; failed=$$((failed+1)); continue; }; \
	  ./tests/server > /tmp/$$name.server.out 2>&1 & pid=$$!; sleep 0.7; \
	  ./$${src%.pli} > /tmp/$$name.out 2>&1; rc=$$?; \
	  kill $$pid 2>/dev/null || true; wait $$pid 2>/dev/null || true; \
	  if [ $$rc -eq 0 ]; then echo "PASS"; else echo "FAIL"; cat /tmp/$$name.out; cat /tmp/$$name.server.out; failed=$$((failed+1)); fi; \
	done; \
	echo ""; echo "$$total tests, $$((total - failed)) passed, $$failed failed"; [ $$failed -eq 0 ]
endif

install: libnet.a $(DIST_INC) $(DIST_PC)
ifeq ($(USE_DOCKER),1)
	@if [ "$(PREFIX)" = "/usr/local" ] && [ -z "$(DESTDIR)" ]; then \
	  echo "ERROR: USE_DOCKER=1 – refusing 'make install' to host /usr/local" >&2; \
	  echo "  On a Docker host the built libnet.a is linux/386 and not usable on macOS." >&2; \
	  echo "  Use a host-visible prefix:" >&2; \
	  echo "    make install PREFIX=\$$PWD/local" >&2; \
	  echo "    make install PREFIX=\$$HOME/.local" >&2; \
	  echo "  Or stage via DESTDIR:" >&2; \
	  echo "    make install DESTDIR=\$$PWD/out PREFIX=/usr/local" >&2; \
	  echo "  Override with 'make install USE_DOCKER=0 PREFIX=/usr/local' inside VM." >&2; \
	  exit 1; \
	fi
endif
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
	rm -f tests/server tests/http_get tests/echo tests/resolve_dial tests/close_shutdown tests/timeout tests/ephemeral tests/send_recv tests/poll
	rm -f tests/server.o tests/http_get.o tests/echo.o tests/resolve_dial.o tests/close_shutdown.o tests/timeout.o tests/ephemeral.o tests/send_recv.o tests/poll.o

distclean: clean uninstall
