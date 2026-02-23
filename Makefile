# GBDK-2020 root (default: ./gbdk in project dir)
ifndef GBDK_HOME
	GBDK_HOME = ./gbdk
endif

LCC = $(GBDK_HOME)/bin/lcc

# Build GBC ROM; add "gb" to also build DMG-compatible .gb
TARGETS = gbc

# Platform-specific LCC flags: -Wm-yc = GB + GBC, -Wm-yC = GBC-only
LCCFLAGS_gb  =
LCCFLAGS_gbc = -Wm-yc

LCCFLAGS += $(LCCFLAGS_$(EXT))
LCCFLAGS += -Wl-j
LCCFLAGS += -Wf-MMD -Wf-Wp-MP
CFLAGS   += -Wf-MMD -Wf-Wp-MP

ifdef GBDK_DEBUG
	LCCFLAGS += -debug -v
endif

PROJECTNAME = gbc-rog
SRCDIR     = src
OBJDIR     = obj/$(EXT)
BINDIR     = build/$(EXT)
MKDIRS     = $(OBJDIR) $(BINDIR)

BINS       = $(BINDIR)/$(PROJECTNAME).$(EXT)
CSOURCES   = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.c)))
OBJS       = $(CSOURCES:%.c=$(OBJDIR)/%.o)
DEPS       = $(OBJS:%.o=%.d)

-include $(DEPS)

all: $(TARGETS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(LCC) $(CFLAGS) -c -o $@ $<

$(BINS): $(OBJS)
	$(LCC) $(LCCFLAGS) $(CFLAGS) -o $(BINDIR)/$(PROJECTNAME).$(EXT) $(OBJS)

clean:
	@for target in $(TARGETS); do $(MAKE) $$target-clean; done

include Makefile.targets

ifneq ($(strip $(EXT)),)
$(info $(shell mkdir -p $(MKDIRS)))
endif
