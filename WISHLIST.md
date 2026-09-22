# pli-llvm wishlist — features libnet is designed against

This file lists the pli-llvm features that libnet's PL/I-centric design is
written as if they already exist. The implementation uses them freely; when a
feature is not yet served by `pli-llvm/build/plic`, the build fails until the
compiler catches up. Each item is grounded in a verified gap (probed against
`projects/plic/pli-llvm`, Sep 2026).

## Design premise

libnet is a **thin-C / PL/I-heavy** socket library:

- **minimal C** (`source/c_bridge.c`): only the raw syscalls PL/I cannot
  perform, reached through pli-llvm's by-value C FFI.
- **most processing in PL/I**: the connection pool, receive buffering,
  delimiter scanning, retry loops, timeout and error policy, and all protocol
  framing live in the PL/I library using `CONTROLLED` storage + pointer
  handles.

Callers hold only a `POINTER` handle; the connection structure layout is
private to the library.

---

## 1. `SUBSTR(s, i, n)` with a runtime/variable length `n` — CRITICAL

- **Why**: the heart of receive-buffer processing. A socket read returns a
  runtime byte count `n`; the PL/I layer must then slice `SUBSTR(buf, 1, n)`
  to hand back exactly the received bytes. Without a variable length, no
  "process exactly N received bytes" logic is expressible.
- **Status today**: `SUBSTR` length must be a constant
  (`sema.cpp:3562` — "SUBSTR length must be a constant in this stage").
- **Note**: `SUBSTR` with a constant length, and the `SUBSTR` pseudo-variable
  on the left of `=`, are already served.

## 2. `CONTROLLED` / `CTL` storage class

- **Why**: the connection pool. `DCL 1 conn_rec CONTROLLED` + `ALLOCATE
  conn_rec` (implicit, **no `SET` required**) + `FREE conn_rec` (LIFO pop)
  gives library-owned, last-in-first-out connection storage with no caller
  bookkeeping — the idiomatic PL/I "task context" pattern for a fat-runtime
  library.
- **Status today**: `CONTROLLED`/`CTL` are reserved keywords but rejected
  (`parser.cpp:1570-71`); `ALLOCATE` without `SET` is diagnosed
  (`parser.cpp:2812` — "ALLOCATE requires the SET option").
- **Workaround until it lands**: `BASED(P)` + `ALLOCATE .. SET(p)` + a
  library free-list approximates the pool (verified working), but loses the
  implicit LIFO lifetime.

## 3. `char(*)` / adjustable-length procedure parameters

- **Why**: `net_read(handle, buffer)` should take a caller-sized buffer rather
  than force a fixed `char(n)` plus a separate `buflen` argument.
- **Status today**: `char(*)` string lengths are "not implemented in this
  stage" (`rule (18)`); dynamic `char(n)` declaration lengths also fail.
- **Workaround until it lands**: fixed `char(MAX)` buffers with an explicit
  `buflen` parameter.

## 4. `BASED`/`CONTROLLED` member in a `RETURN` expression

- **Why**: accessors should be able to write `return h -> fd;` (or
  `return c.fd;` for a based view) instead of copying a based member to a
  scalar out-parameter first.
- **Status today**: `return c.fd;` is rejected (`rule (81)` — based-qualified
  members cannot appear in a `RETURN` expression; verified).
- **Workaround until it lands**: copy the member to a scalar and return that,
  or use an out-parameter (`outv = c.fd;` — verified working).

## 5. `BASED(P)` directly on a procedure `POINTER` parameter

- **Why**: every library method takes a `POINTER` handle; it should be able to
  declare its based view directly on that parameter, e.g.
  `open_it: procedure(cp); declare 1 c based(cp);`.
- **Status today**: `based(cp)` where `cp` is a procedure parameter is
  rejected ("BASED base 'CP' is not a POINTER variable in this scope").
- **Workaround until it lands**: copy the parameter into a local pointer first
  (`q = cp; declare 1 c based(q);` — verified working).

---

## Already served (NOT on the wishlist)

Verified against the current `pli-llvm/build/plic`; libnet relies on these and
needs no compiler work:

- by-value C FFI: `OPTIONS(LINKAGE(SYSTEM))` / `OPTIONS(BYVALUE)` for scalar
  `fixed bin(31)`/`float` params and scalar returns;
- `POINTER` params by value (`addr(x)`), `ADDR`, `NULL()`, pointer assignment,
  pointer equality;
- `BASED(P)` + `ALLOCATE .. SET(p)` + `FREE` + `P -> member`;
- string builtins: `SUBSTR` (const len), `INDEX`, `VERIFY`, `TRANSLATE`,
  `TRIM`, `TALLY`, `UPPERCASE`, `LENGTH`, `MAXLENGTH`, `REPEAT`, `REVERSE`;
- CHARACTER structure members; hex `'...'X` literals;
- loops: `DO WHILE/UNTIL`, `DO i = 1 TO n`, `LEAVE`, `ITERATE`, `SELECT`;
- conditions: `SIGNAL condition(...) SET ONCODE`, `ON`, `REVERT`,
  `oncode()`.

## C-bridge production bindings (C-side, not PL/I-wishlist)

These are additions to `source/c_bridge.c` — pure C, reached via the already-
served by-value FFI, so they need no pli-llvm compiler work. All are C-tested
(see `tests/c_bridge.c` smoke test):

- socket options by libnet code: `netc_sockopt` (`NETOPT_REUSEADDR` /
  `NETOPT_KEEPALIVE` / `NETOPT_NODELAY`), `netc_setlinger` (SO_LINGER);
- `netc_getsockname` (bound local address);
- `netc_resolve` (host ↔ dotted quad) and `netc_strerror` (errno text);
- `netc_connect_nb` / `netc_getsockerr` (nonblocking connect, EINPROGRESS,
  SO_ERROR);
- `netc_sendto` / `netc_recvfrom` (UDP datagrams).


## Non-goals

libnet does not require: `AREA`/`OFFSET`, `BIT(n>1)` stream I/O, `GENERIC`,
`LABEL`/`CELL`, record/KEYED I/O, or tasks/events.
