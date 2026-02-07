CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin
CONFDIR  = /etc/nvfd
UNITDIR  = /etc/systemd/system

# NVIDIA CUDA paths (try standard locations)
CUDA_PATH ?= $(shell [ -d /usr/local/cuda ] && echo /usr/local/cuda || echo /usr)
CFLAGS  += -I$(CUDA_PATH)/include -Iinclude
LDFLAGS += -L$(CUDA_PATH)/lib64

LIBS     = -lnvidia-ml -ljansson -lncursesw

SRCDIR   = src
BUILDDIR = build

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
TARGET   = $(BUILDDIR)/nvfd

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

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

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/nvfd
	rm -f $(DESTDIR)$(UNITDIR)/nvfd.service
	@echo "Config files preserved in $(CONFDIR). Remove manually if desired."
