# Headless desktop harness for Muntcraft.
#
# Builds the real game (main.cpp, the tasks, nGL) against the fake SDL in
# tools/pcsim/SDL, so a script can run the game on a machine with no SDL
# installed and capture frames. Nothing here is part of the calculator build:
#
#   make -C tools/pcsim           build build/crafti-sim
#   sh tools/pcsim/play.sh        build it and run the scripted tour
#
# See tools/pcsim/README.md.

ROOT := ../..

CXX ?= g++
BUILD := build

# The fake SDL has to win over any real one, so its include directory is first.
# tests/fake_sdl is deliberately not on this path: that one is audio-only.
CXXFLAGS := -std=c++14 -O2 -g -I. -I$(ROOT) -I$(ROOT)/nGL -Wall -W -Wno-old-style-cast -Wno-unused-parameter -fno-strict-aliasing

GAME_SOURCES := $(notdir $(wildcard $(ROOT)/*.cpp) $(wildcard $(ROOT)/nGL/*.cpp))
GAME_OBJECTS := $(addprefix $(BUILD)/,$(GAME_SOURCES:.cpp=.o))
HARNESS_OBJECTS := $(BUILD)/pcsim.o $(BUILD)/probe.o

VPATH := $(ROOT):$(ROOT)/nGL:tools/pcsim

.PHONY: all clean

all: $(BUILD)/crafti-sim

$(BUILD)/crafti-sim: $(GAME_OBJECTS) $(HARNESS_OBJECTS)
	$(CXX) $^ -o $@ -lz -lm -rdynamic

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
