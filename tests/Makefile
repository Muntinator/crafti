# Host tests for the Crafti audio engine.
#
# These compile the same sources the calculator target uses. Only the output
# backend differs: the UART code runs against its simulated register file, so
# its register sequence, modulator and teardown are exercised on the host.
#
#   make -C tests        build and run everything
#   make -C tests clean

CXX ?= g++
ROOT := ..
COMMON := -std=c++11 -O1 -g -I$(ROOT) -I. -Wall -W
AUDIO_SOURCES := $(ROOT)/audio_manager.cpp $(ROOT)/audio_pack.cpp \
                 $(ROOT)/audio_nspire_tx.cpp $(ROOT)/audio_nspire_gpio.cpp \
                 $(ROOT)/audio_output.cpp

BINS := build/audio_pack_test build/audio_tx_test build/audio_gpio_test \
        build/audio_manager_test build/audio_output_test \
        build/livestock_test build/villagegen_test \
        build/worldclock_test build/survival_test \
        build/blockdata_test build/biomegen_test build/weather_test \
        build/itemrules_test build/cheststore_test \
        build/structuregen_test build/blocklight_test \
        build/command_test build/bed_test build/snowcover_test \
        build/enchanting_test build/menuui_test build/menuui_wide_test

.PHONY: all run clean

all: run

run: $(BINS)
	@for bin in $(BINS); do echo "--- $$bin"; ./$$bin || exit 1; done
	@echo "all tests passed"

build:
	@mkdir -p build

build/audio_pack_test: audio_pack_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_pack_test.cc $(AUDIO_SOURCES)

build/audio_tx_test: audio_tx_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_tx_test.cc $(AUDIO_SOURCES)

build/audio_gpio_test: audio_gpio_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_gpio_test.cc $(AUDIO_SOURCES)

build/audio_manager_test: audio_manager_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_manager_test.cc $(AUDIO_SOURCES)

# This one needs the SDL backend, so it uses the fake SDL in tests/fake_sdl.
build/audio_output_test: audio_output_test.cc fake_sdl_audio.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -Ifake_sdl -o $@ audio_output_test.cc fake_sdl_audio.cc $(AUDIO_SOURCES)

# Pure livestock tables: no engine sources, just the spec and its real ids.
# mobmodel.h is listed because the drawn-geometry checks below ask the
# renderer through `angleInDrawnFrame`, and this Makefile does no header
# dependency tracking -- without it here a change to that rule would silently
# leave a stale binary passing.
build/livestock_test: livestock_test.cc $(ROOT)/livestockspecies.cpp $(ROOT)/mobmodel.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ livestock_test.cc $(ROOT)/livestockspecies.cpp

# The village generator is engine-independent by design, so it is tested whole.
build/villagegen_test: villagegen_test.cc $(ROOT)/villagegen.cpp $(ROOT)/villagegen.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ villagegen_test.cc $(ROOT)/villagegen.cpp

# The day/night clock is pure math (no nGL), so it needs no engine sources.
build/worldclock_test: worldclock_test.cc $(ROOT)/worldclock.cpp $(ROOT)/worldclock.h | build
	$(CXX) $(COMMON) -o $@ worldclock_test.cc $(ROOT)/worldclock.cpp

# Survival is also pure rules; it needs the real item ids only to validate the
# food table. terrain.h pulls in nGL's texturetools.h for the block typedefs.
build/survival_test: survival_test.cc $(ROOT)/survival.cpp $(ROOT)/survival.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ survival_test.cc $(ROOT)/survival.cpp

# The BLOCK_WDATA packing is header-only, so this pins the encoding that the
# inventory, the item icons and the block renderers all depend on.
build/blockdata_test: blockdata_test.cc $(ROOT)/terrain.h $(ROOT)/textures/items.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ blockdata_test.cc

# World generation (biomes, mountains, rivers, caves, ravines) is engine-free by
# design, so the whole module is tested without nGL or the block tables.
build/biomegen_test: biomegen_test.cc $(ROOT)/biomegen.cpp $(ROOT)/biomegen.h | build
	$(CXX) $(COMMON) -o $@ biomegen_test.cc $(ROOT)/biomegen.cpp

# The weather is a pure function of the world seed and the world clock's ticks,
# so it needs no engine sources either.
build/weather_test: weather_test.cc $(ROOT)/weather.cpp $(ROOT)/weather.h | build
	$(CXX) $(COMMON) -o $@ weather_test.cc $(ROOT)/weather.cpp

# Enchantments: the registry, the table's offers, the anvil's combining rules
# and the numbers the effects are worth. Engine-free; the item and block ids it
# keeps as numbers are checked against textures/items.h and terrain.h here, which
# is why the texture headers have to be on the include path.
build/enchanting_test: enchanting_test.cc $(ROOT)/enchanting.cpp $(ROOT)/enchanting.h \
                       $(ROOT)/terrain.h $(ROOT)/textures/items.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ enchanting_test.cc $(ROOT)/enchanting.cpp

# Snow cover: the depth a step grows and melts, what a layer can rest on, and
# where a step's columns are. Engine-free except that the block ids it keeps as
# numbers are checked against the real ones here.
build/snowcover_test: snowcover_test.cc $(ROOT)/snowcover.cpp $(ROOT)/snowcover.h $(ROOT)/terrain.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ snowcover_test.cc $(ROOT)/snowcover.cpp

# Durability, armour and the lifetime of a dropped stack: pure rules over the
# real item ids, so it needs the item atlas header and terrain.h's packing only.
build/itemrules_test: itemrules_test.cc $(ROOT)/itemrules.cpp $(ROOT)/itemrules.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ itemrules_test.cc $(ROOT)/itemrules.cpp

# Chest contents, including double chests and the save-file round trip. Pure,
# except that it shares the item stacking rules with itemrules.cpp.
build/cheststore_test: cheststore_test.cc $(ROOT)/cheststore.cpp $(ROOT)/cheststore.h $(ROOT)/itemrules.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ cheststore_test.cc $(ROOT)/cheststore.cpp $(ROOT)/itemrules.cpp

# Per-block light: the sky depth falloff, the shading curve, the emitter table
# and the light lattice. Engine-free, but the emitter ids it keeps as numbers are
# checked against terrain.h here.
build/blocklight_test: blocklight_test.cc $(ROOT)/blocklight.cpp $(ROOT)/blocklight.h $(ROOT)/terrain.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ blocklight_test.cc $(ROOT)/blocklight.cpp

# Structures (dungeons, ruins, temples) are engine-free like the village and
# biome generators, but the test also checks the block and item ids it writes
# against the real headers, so it needs terrain.h and the item atlas.
build/structuregen_test: structuregen_test.cc $(ROOT)/structuregen.cpp $(ROOT)/structuregen.h $(ROOT)/biomegen.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ structuregen_test.cc $(ROOT)/structuregen.cpp

# The debug command language: tokeniser, numbers, name folding, the time and
# weather spellings and the /fill cap. Engine-free, so it is tested whole; the
# named times it shares with the clock and the weather names it shares with the
# weather states come from those modules, which is why both are linked in -- the
# test checks that /weather's names and weather.h's agree state for state.
build/command_test: command_test.cc $(ROOT)/command.cpp $(ROOT)/command.h \
                    $(ROOT)/worldclock.h $(ROOT)/weather.h $(ROOT)/weather.cpp | build
	$(CXX) $(COMMON) -o $@ command_test.cc $(ROOT)/command.cpp $(ROOT)/weather.cpp

# The bed's two halves, its facing, the sleep decision and the respawn point.
# Pure rules plus the clock it measures a night against, so worldclock.cpp is
# linked in rather than the whole engine.
build/bed_test: bed_test.cc $(ROOT)/bed.cpp $(ROOT)/bed.h \
                $(ROOT)/worldclock.cpp $(ROOT)/worldclock.h $(ROOT)/terrain.h | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ bed_test.cc $(ROOT)/bed.cpp $(ROOT)/worldclock.cpp

# The front-end layout is the one part of the menus a host can check: the labels
# against the boxes they are drawn in. It is built twice, because the calculator
# screen (320x240, _TINSPIRE) is half the desktop's in each direction and a label
# that fits one need not fit the other.
# The front-end's artwork is generated from the vanilla sheets, so the test has to
# be rebuilt when one of those headers is regenerated: the geometry it checks is
# derived from the images' own sizes.
MENUUI_TEXTURES := $(ROOT)/textures/menu_button.h $(ROOT)/textures/menu_background.h \
                   $(ROOT)/textures/title_logo.h \
                   $(ROOT)/textures/edition.h $(ROOT)/textures/slider_handle.h \
                   $(ROOT)/textures/checkbox.h $(ROOT)/textures/font_bmp.h \
                   $(ROOT)/textures/font_dat.h $(ROOT)/textures/font_bmp_wide.h \
                   $(ROOT)/textures/font_dat_wide.h

build/menuui_test: menuui_test.cc $(ROOT)/menuui.cpp $(ROOT)/menuui.h $(ROOT)/font.cpp $(MENUUI_TEXTURES) | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -D_TINSPIRE -o $@ menuui_test.cc $(ROOT)/menuui.cpp $(ROOT)/font.cpp

build/menuui_wide_test: menuui_test.cc $(ROOT)/menuui.cpp $(ROOT)/menuui.h $(ROOT)/font.cpp $(MENUUI_TEXTURES) | build
	$(CXX) $(COMMON) -I$(ROOT)/nGL -o $@ menuui_test.cc $(ROOT)/menuui.cpp $(ROOT)/font.cpp

clean:
	rm -rf build
