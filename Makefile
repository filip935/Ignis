CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Os -g
LDFLAGS_GUI = -s

SRCDIR = src
OBJDIR = obj

CORE_SRCS = $(shell find $(SRCDIR) -name '*.c' ! -path '*/gui/*' ! -path '*/gui.c' ! -path '*/main.c')
CORE_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CORE_SRCS))

GUI_SRCS = $(shell find $(SRCDIR)/gui -name '*.c' 2>/dev/null)
GUI_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(GUI_SRCS))

GUI_TARGET = nihilflash-gui

HAVE_GUI := $(shell pkg-config --exists gtk4 2>/dev/null && echo yes || echo no)

.PHONY: all clean

BOOTDATA = src/core/bootdata.h

$(BOOTDATA): tools/genboot.sh
	@bash tools/genboot.sh src/core

all: $(BOOTDATA) $(GUI_TARGET)
	@true

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(GUI_TARGET): CFLAGS += $(shell pkg-config --cflags gtk4)
$(GUI_TARGET): LDFLAGS_GUI += $(shell pkg-config --libs gtk4)
$(GUI_TARGET): $(CORE_OBJS) $(GUI_OBJS)
	$(CC) $(CORE_OBJS) $(GUI_OBJS) -o $@ $(LDFLAGS_GUI)
	@echo "Build complete: ./$@"

clean:
	rm -rf $(OBJDIR) $(GUI_TARGET)
