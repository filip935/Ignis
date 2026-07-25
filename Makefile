CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Os -g
LDFLAGS_CLI = -s
LDFLAGS_GUI = -s

SRCDIR = src
OBJDIR = obj

CLI_SRCS = $(shell find $(SRCDIR) -name '*.c' ! -path '*/gui/*' ! -path '*/gui.c')
CLI_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CLI_SRCS))

CORE_SRCS = $(filter-out $(SRCDIR)/main.c,$(CLI_SRCS))
CORE_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CORE_SRCS))

GUI_SRCS = $(shell find $(SRCDIR)/gui -name '*.c' 2>/dev/null)
GUI_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(GUI_SRCS))

CLI_TARGET = winflash
GUI_TARGET = winflash-gui

HAVE_GUI := $(shell pkg-config --exists gtk4 2>/dev/null && echo yes || echo no)

.PHONY: all clean gui

BOOTDATA = src/core/bootdata.h

$(BOOTDATA): tools/genboot.sh
	@bash tools/genboot.sh src/core

all: $(BOOTDATA) $(CLI_TARGET)
	@if [ "$(HAVE_GUI)" = "no" ]; then \
		echo "Note: Install libgtk-4-dev for GUI build: make gui"; \
	fi

gui:
	@if [ "$(HAVE_GUI)" = "no" ]; then \
		echo "Error: GTK4 development headers not found."; \
		echo "Install them with: sudo apt install libgtk-4-dev"; \
		exit 1; \
	fi
	@$(MAKE) $(GUI_TARGET)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c $< -o $@

$(CLI_TARGET): $(CLI_OBJS)
	$(CC) $(CLI_OBJS) -o $@ $(LDFLAGS_CLI)
	@echo "CLI build complete: ./$@"

$(GUI_TARGET): CFLAGS += $(shell pkg-config --cflags gtk4)
$(GUI_TARGET): LDFLAGS_GUI += $(shell pkg-config --libs gtk4)
$(GUI_TARGET): $(CORE_OBJS) $(GUI_OBJS)
	$(CC) $(CORE_OBJS) $(GUI_OBJS) -o $@ $(LDFLAGS_GUI)
	@echo "GUI build complete: ./$@"

clean:
	rm -rf $(OBJDIR) $(CLI_TARGET) $(GUI_TARGET)
