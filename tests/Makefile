# Host tests for the Crafti audio engine.
#
# These compile the same sources the calculator target uses. Only the output
# backend differs: the GPIO4 code runs against its simulated register file, so
# its register sequence, modulator and teardown are exercised on the host.
#
#   make -C tests        build and run everything
#   make -C tests clean

CXX ?= g++
ROOT := ..
COMMON := -std=c++11 -O1 -g -I$(ROOT) -I. -Wall -W
AUDIO_SOURCES := $(ROOT)/audio_manager.cpp $(ROOT)/audio_pack.cpp \
                 $(ROOT)/audio_nspire_gpio4.cpp $(ROOT)/audio_output.cpp

BINS := build/audio_pack_test build/audio_gpio4_test \
        build/audio_manager_test build/audio_output_test \
        build/livestock_test build/villagegen_test \
        build/worldclock_test build/survival_test \
        build/blockdata_test build/biomegen_test build/weather_test

.PHONY: all run clean

all: run

run: $(BINS)
	@for bin in $(BINS); do echo "--- $$bin"; ./$$bin || exit 1; done
	@echo "all tests passed"

build:
	@mkdir -p build

build/audio_pack_test: audio_pack_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_pack_test.cc $(AUDIO_SOURCES)

build/audio_gpio4_test: audio_gpio4_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_gpio4_test.cc $(AUDIO_SOURCES)

build/audio_manager_test: audio_manager_test.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -DCRAFTI_NO_SDL -o $@ audio_manager_test.cc $(AUDIO_SOURCES)

# This one needs the SDL backend, so it uses the fake SDL in tests/fake_sdl.
build/audio_output_test: audio_output_test.cc fake_sdl_audio.cc $(AUDIO_SOURCES) | build
	$(CXX) $(COMMON) -Ifake_sdl -o $@ audio_output_test.cc fake_sdl_audio.cc $(AUDIO_SOURCES)

# Pure livestock tables: no engine sources, just the spec and its real ids.
build/livestock_test: livestock_test.cc $(ROOT)/livestockspecies.cpp | build
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

clean:
	rm -rf build
