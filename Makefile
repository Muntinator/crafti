GCC = nspire-gcc
GPP = nspire-g++
LD = nspire-ld
GENZEHN = genzehn
MAKEPRG = make-prg
OPTIMIZE ?= fast
NDLSSDK ?=
ZLIB_PREFIX ?=
NDLESS_INCLUDES =
NDLESS_LIBS =
ifneq ($(strip $(NDLSSDK)),)
NDLESS_INCLUDES += -I$(NDLSSDK)/include -I$(NDLSSDK)/include/freetype2
endif
ifneq ($(strip $(ZLIB_PREFIX)),)
NDLESS_INCLUDES += -I$(ZLIB_PREFIX)/include
NDLESS_LIBS += -L$(ZLIB_PREFIX)/lib
endif
CFLAGS = -O$(OPTIMIZE) -I nGL -I . $(NDLESS_INCLUDES) -Wall -W -marm -ffast-math -mcpu=arm926ej-s -fno-math-errno -fomit-frame-pointer -flto -fgcse-sm -fgcse-las -funsafe-loop-optimizations -fno-fat-lto-objects -frename-registers -fprefetch-loop-arrays -mno-thumb-interwork -ffunction-sections -fdata-sections -DNDEBUG -D_TINSPIRE
GCCFLAGS = -O$(OPTIMIZE) -I nGL -I . $(NDLESS_INCLUDES) -Wall -W -marm -ffast-math -mcpu=arm926ej-s -fno-math-errno -fomit-frame-pointer -flto -fno-rtti -fgcse-sm -fgcse-las -funsafe-loop-optimizations -fno-fat-lto-objects -frename-registers -fprefetch-loop-arrays -Wold-style-cast -mno-thumb-interwork -ffunction-sections -fdata-sections -fno-exceptions -DNDEBUG -D_TINSPIRE
LDFLAGS = $(NDLESS_LIBS) -lm -lz -Wl,--gc-sections
ZEHNFLAGS = --name "Crafti" --version 13 --author "Fabian Vogt" --notice "3D Minecraft" --compress
EXE = crafti
# The game is every source in the tree except tools/, which holds the host-side
# development tools (the headless harness and the emulator shim). They are
# ordinary .cpp files, so an unqualified find would compile them into the
# calculator build and break it.
GAME_SOURCES = $(shell find . -not -path './tools/*' -a \( -name \*.c -o -name \*.cpp -o -name \*.S \))
OBJS = $(patsubst %.c, %.o, $(filter %.c,$(GAME_SOURCES)))
OBJS := $(filter-out ./syscalls.o,$(OBJS))
OBJS += $(patsubst %.cpp, %.o, $(filter %.cpp,$(GAME_SOURCES)))
OBJS += $(patsubst %.S, %.o, $(filter %.S,$(GAME_SOURCES)))

all: $(EXE).tns

%.o: %.c
	@echo Compiling $<...
	@$(GCC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo Compiling $<...
	@$(GPP) -std=c++11 $(GCCFLAGS) -c $< -o $@

$(EXE).elf: $(OBJS)
	+$(LD) $^ -o $@ $(LDFLAGS)

$(EXE).tns: $(EXE).elf
	+$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	+$(MAKEPRG) $@.zehn $@
	+latest_tns=$$(ls -t $(EXE)*.tns 2>/dev/null | head -n1); \
	if [ -n "$$latest_tns" ] && [ "$$latest_tns" != "$@" ]; then mv -f "$$latest_tns" "$@"; fi
	+rm $@.zehn

.PHONY: clean
clean:
	rm -f `find . -name \*.o`
	rm -f $(EXE).tns $(EXE).elf
