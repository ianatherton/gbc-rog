# GBDK-2020 root (default: ./gbdk in project dir)
ifndef GBDK_HOME
	GBDK_HOME = ./gbdk
endif

LCC       = $(GBDK_HOME)/bin/lcc
PNG2ASSET = $(GBDK_HOME)/bin/png2asset

# Build GBC ROM; add "gb" to also build DMG-compatible .gb
TARGETS = gbc

# Platform-specific LCC flags: -Wm-yc = GB + GBC, -Wm-yC = GBC-only
LCCFLAGS_gb  =
LCCFLAGS_gbc = -Wm-yC -Wl-yt0x1B -Wl-yo32 -autobank -Wm-yoA -Wb-ext=.rel # -yC: CGB-only — explored bits use SVBK WRAM banking (lighting.c)

LCCFLAGS += $(LCCFLAGS_$(EXT))
LCCFLAGS += -Wl-j
LCCFLAGS += -Wf-MMD -Wf-Wp-MP
CFLAGS   += -Wf-MMD -Wf-Wp-MP
CFLAGS   += -Wf--opt-code-speed

ifdef GBDK_DEBUG
	LCCFLAGS += -debug -v
endif

PROJECTNAME = gbc-rog
SRCDIR      = src
OBJDIR      = obj/$(EXT)
BINDIR      = build/$(EXT)
MKDIRS      = $(OBJDIR) $(BINDIR)

BINS        = $(BINDIR)/$(PROJECTNAME).$(EXT)
CSOURCES    = $(foreach dir,$(SRCDIR),$(notdir $(wildcard $(dir)/*.c)))
OBJS        = $(CSOURCES:%.c=$(OBJDIR)/%.o)
DEPS        = $(OBJS:%.o=%.d)

TILESET_PNG = res/tileset.png
TILESET_C   = src/tileset.c

# Per-biome enemy sprite sheets (enemies only; shared BG stays in tileset.png).
# Each PNG holds that biome's enemy frames row-major; biome_load_active() uploads
# them into the ENEMY_SCRATCH VRAM region on floor entry. One bank per sheet (the
# biome's own bank). Add a biome: new res/enemies_<biome>.png + a rule below.
ENEMIES_MINIBOSS_PNG = res/enemies_miniboss.png
ENEMIES_MINIBOSS_C   = src/enemies_miniboss.c
BOSSES_PNG = res/bosses.png       # indexed 24x64 (3 cols x 8 rows): sphinx body x2 frames + wings x2 frames
BOSSES_C   = src/bosses.c

-include $(DEPS)

all: assets $(TARGETS)

.PHONY: sameboy
sameboy:
	bash "$(CURDIR)/emu/build-sameboy.sh"

assets: $(TILESET_C) $(ENEMIES_MINIBOSS_C) $(BOSSES_C)

$(TILESET_C): $(TILESET_PNG)
	$(PNG2ASSET) $< -o $@ -map -keep_duplicate_tiles -noflip
	@sed -i '/#pragma bank /d' $@
	@sed -i '2a #pragma bank 1' $@

# Indexed source PNG carries the 4-gray palette in tileset order, so -keep_palette_order
# reproduces the same 2bpp indices (idx0 transparent .. idx3 body) as the old baked art.
$(ENEMIES_MINIBOSS_C): $(ENEMIES_MINIBOSS_PNG)
	$(PNG2ASSET) $< -o $@ -map -keep_duplicate_tiles -noflip -keep_palette_order
	@sed -i '/#pragma bank /d' $@
	@sed -i '2a #pragma bank 27' $@

# Sphinx boss (BIOME_BOSS2): tiles row-major, 3 per row → tile N = row*3 + col (see TILE_SPHINX_* in defs.h).
$(BOSSES_C): $(BOSSES_PNG)
	$(PNG2ASSET) $< -o $@ -map -keep_duplicate_tiles -noflip -keep_palette_order
	@sed -i '/#pragma bank /d' $@
	@sed -i '2a #pragma bank 24' $@

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
