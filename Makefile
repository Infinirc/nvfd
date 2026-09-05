CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
CONFDIR  = /etc/nvfd
UNITDIR  = /etc/systemd/system

# NVML declarations are carried in include/nvml_api.h, so no CUDA toolkit is
# needed to build. Any NVML call that is not declared there is a hard error
# rather than an implicit declaration.
CFLAGS  += -Iinclude -Werror=implicit-function-declaration

# Link the driver's NVML by SONAME: libnvidia-ml.so.1 ships with every
# driver, whereas the unversioned libnvidia-ml.so symlink only comes with
# -dev packages or the CUDA toolkit. Some distributions keep it outside
# ld's default search path, so use the directory registered with ldconfig.
# Keep it out of LDFLAGS: that is a user variable, and a value passed on the
# command line or exported by a packaging system would replace it and drop -L.
LDCONFIG          ?= /sbin/ldconfig
NVML_CACHE_PATTERN = libnvidia-ml\.so\.1 \(libc6,
NVML_LIBDIR       ?= $(shell $(LDCONFIG) -p 2>/dev/null | awk '/$(NVML_CACHE_PATTERN)/{print $$NF; exit}' | xargs -r dirname)
NVML_LDFLAGS       = $(if $(NVML_LIBDIR),-L$(NVML_LIBDIR))
LIBS               = $(NVML_LDFLAGS) -l:libnvidia-ml.so.1 -ljansson -lncursesw

SRCDIR   = src
BUILDDIR = build

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
TARGET   = $(BUILDDIR)/nvfd

.PHONY: all clean check test install uninstall install-utils uninstall-utils

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

check: $(OBJS) test
	@echo "All source files compiled successfully."

test: $(BUILDDIR)/test_speed $(BUILDDIR)/test_config_migrate
	sh tests/test_makefile_nvml_detection.sh
	sh tests/test_nvml_api_declarations.sh
	sh tests/test_find_nvml.sh
	./$(BUILDDIR)/test_speed
	./$(BUILDDIR)/test_config_migrate

$(BUILDDIR)/test_speed: tests/test_speed.c src/speed.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/test_config_migrate: tests/test_config_migrate.c src/config.c src/speed.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -include tests/test_paths.h -o $@ $^ -ljansson

clean:
	rm -rf $(BUILDDIR)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/nvfd
	install -d $(DESTDIR)$(CONFDIR)
	@if [ ! -f $(DESTDIR)$(CONFDIR)/curve.json ]; then \
		install -m 644 config/default_curve.json $(DESTDIR)$(CONFDIR)/curve.json; \
	fi
	install -d $(DESTDIR)$(UNITDIR)
	install -m 644 systemd/nvfd.service $(DESTDIR)$(UNITDIR)/nvfd.service

install-utils:
	install -m 755 utils/nvfd-fan-control.sh $(DESTDIR)$(BINDIR)/
	install -m 644 utils/nvfd-fan-control.service $(DESTDIR)$(UNITDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/nvfd
	rm -f $(DESTDIR)$(UNITDIR)/nvfd.service
	rm -f $(DESTDIR)$(BINDIR)/nvfd-fan-control.sh
	rm -f $(DESTDIR)$(UNITDIR)/nvfd-fan-control.service
	@echo "Config files preserved in $(CONFDIR). Remove manually if desired."

uninstall-utils:
	rm -f $(DESTDIR)$(BINDIR)/nvfd-fan-control.sh
	rm -f $(DESTDIR)$(UNITDIR)/nvfd-fan-control.service
