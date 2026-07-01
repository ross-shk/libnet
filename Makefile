PLIC      ?= plic
CC        ?= gcc
AR        ?= ar
PLIFLAGS  ?= -C -dELF -O
CFLAGS    ?= -m32
LDFLAGS   ?= -m32 -no-pie -z muldefs
LIBS      ?= -lprf
ALT_DIR   ?= /usr/lib/pli-1.4.1/lib/alt
PREFIX    ?= /usr/local
INCDIR    ?= $(PREFIX)/include
LIBDIR    ?= $(PREFIX)/lib
PKGDIR    ?= $(LIBDIR)/pkgconfig

INC        = -i include
OBJS       = net_bridge.o net.o net_server.o
DIST_INC   = dist/net.inc
DIST_PC    = dist/net.pc
TEST_SRCS  = $(wildcard tests/*.pli)

.PHONY: all install uninstall clean distclean test test-client-server

all: libnet.a $(DIST_INC) $(DIST_PC)

net_bridge.o: source/net_bridge.c
	$(CC) $(CFLAGS) -c $< -o $@

net.o: source/net.pli include/net_bridge.inc include/net_errors.inc include/type_defs.inc
	$(PLIC) $(PLIFLAGS) $< $(INC) -o $@

net_server.o: source/net_server.pli include/net_bridge.inc include/net_errors.inc include/type_defs.inc
	$(PLIC) $(PLIFLAGS) $< $(INC) -o $@

libnet.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

$(DIST_INC): include/type_defs.inc include/net_bridge.inc include/net_errors.inc include/net_base.inc include/net_server.inc
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
	echo 'Libs: -L$${libdir} -lnet -lprf $${altdir}/fhs.o $${altdir}/ghs.o' >> $@
	echo 'Cflags: -i$${includedir}' >> $@

test: libnet.a
	@failed=0; total=0; \
	for src in $(TEST_SRCS); do \
	  name=$$(basename $$src .pli); \
	  case $$name in \
	    dial_test|readall_test) continue ;; \
	  esac; \
	  total=$$((total + 1)); \
	  printf "  %-28s " "$$name"; \
	  plic $(PLIFLAGS) $$src $(INC) -o $${src%.pli}.o && \
	  gcc $(LDFLAGS) -o $${src%.pli} $${src%.pli}.o $(OBJS) $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o && \
	  ./$${src%.pli} > /dev/null 2>&1 && \
	  echo "PASS" || { echo "FAIL"; failed=$$((failed + 1)); }; \
	done; \
	echo ""; \
	echo "$$total tests, $$((total - failed)) passed, $$failed failed"; \
	[ $$failed -eq 0 ]

test-client-server: libnet.a
	@cd tests/client_server && \
	  plic $(PLIFLAGS) server_app.pli -i ../../include -o server_app.o && \
	  plic $(PLIFLAGS) client_app.pli -i ../../include -o client_app.o && \
	  gcc $(LDFLAGS) -o server_app server_app.o ../../$(OBJS) $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o && \
	  gcc $(LDFLAGS) -o client_app client_app.o ../../$(OBJS) $(LIBS) $(ALT_DIR)/fhs.o $(ALT_DIR)/ghs.o && \
	  ./server_app & pid=$$!; sleep 1; \
	  ./client_app; r=$$?; kill $$pid 2>/dev/null; exit $$r

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
	rm -f $(OBJS) libnet.a
	rm -rf dist
	rm -f tests/*.o tests/*.map tests/client_server/*.o tests/client_server/*.map
	rm -f tests/resolve tests/test_connect tests/test_errors tests/use_socket tests/readme_usage
	rm -f tests/client_server/server_app tests/client_server/client_app

distclean: clean uninstall
