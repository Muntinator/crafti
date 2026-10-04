# The headers the engine embeds. Everything here is an official Minecraft sheet
# converted to RGB565 (the GUI, world and boot-screen textures) except the two
# that are still the game's own: the block outline and the pre-vanilla
# terrain/terrain2 fallbacks.
OBJS := font_bmp.h font_dat.h font_bmp_wide.h font_dat_wide.h \
        icons.h inventory.h inv_selection.h menu_button.h menu_background.h \
        checkbox.h slider_handle.h \
        title_logo.h edition.h title_backdrop.h \
        inventory2.h crafting_table.h furnace.h chest_top.h chest_player.h \
        creative_window.h creative_tabs.h creative_scroll.h armor_slots.h part_fire.h \
        loading.h terrain.h terrain2.h \
        items_texture.h terrain3.h \
        chicken.h creeper.h cow.h pig.h sheep.h horse.h villager.h \
        wolf.h mooshroom.h donkey.h steve.h

all: $(OBJS)

# The GUI is the official 1.17.1 art under gui/ (in the vanilla asset tree's own
# layout) cut into the pieces the engine blits: the HUD sheet, the hotbar, the
# selected-slot frame, the menu buttons and their dirt backdrop, the player,
# crafting-table, furnace and chest windows, and the torch flame.
GUI := ../tools/textures/gen_gui_textures.py
GUI_SOURCES := $(wildcard gui/*.png) $(wildcard gui/container/*.png) \
               $(wildcard gui/container/creative_inventory/*.png) \
               $(wildcard gui/title/*.png) $(wildcard gui/title/background/*.png) \
               $(wildcard font/*.png) particle/flame.png \
               $(wildcard item/empty_armor_slot_*.png)
icons.h inventory.h inv_selection.h menu_button.h menu_background.h \
checkbox.h slider_handle.h \
title_logo.h edition.h inventory2.h crafting_table.h furnace.h chest_top.h \
chest_player.h creative_window.h creative_tabs.h creative_scroll.h \
armor_slots.h part_fire.h: $(GUI) $(GUI_SOURCES)
	python3 $(GUI)

# The font replaced the game's own bitmap: the atlas is the vanilla 8-pixel one
# and the metrics table beside it is derived from it, so both are generated and
# neither is edited by hand. It is the same script as the GUI because it is the
# same source tree.
font_bmp.h font_dat.h font_bmp_wide.h font_dat_wide.h: $(GUI) $(wildcard font/*.png)
	python3 $(GUI)

# The title screen's panorama is pre-rendered here as well: the calculator cannot
# hold six cubemap faces.
title_backdrop.h: $(GUI) $(wildcard gui/title/background/*.png)
	python3 $(GUI)

# The mob skins are the official Minecraft ones, kept under entity/ in the vanilla
# asset tree's own layout; one script converts them all to RGB565 headers.
MOBS := ../tools/textures/gen_entity_textures.py
creeper.h cow.h pig.h sheep.h chicken.h horse.h villager.h \
wolf.h mooshroom.h donkey.h steve.h: $(MOBS)
	python3 $(MOBS)

# The block atlas is the official Minecraft block textures arranged into the grid
# the engine samples; the one script writes both the PNG the desktop build loads
# and the header the calculator falls back on.
BLOCKS := ../tools/textures/gen_block_textures.py
BLOCK_SOURCES := $(wildcard block/*.png) entity/chest/normal.png \
                 entity/bed/red.png item/cake.png
terrain3.h terrain3.png: $(BLOCKS) $(BLOCK_SOURCES)
	python3 $(BLOCKS)

# The item atlas is the official Minecraft item textures arranged by the
# ItemTexture enum in items.h; the one script writes both the PNG (for review and
# for the desktop screenshot tests) and the header the game embeds.
ITEMS := ../tools/textures/gen_item_textures.py
ITEM_SOURCES := $(wildcard item/*.png) entity/bed/red.png
items_texture.h items_texture.png: $(ITEMS) $(ITEM_SOURCES)
	python3 $(ITEMS)

# The boot screen is the first thing drawn: main.cpp hands its bitmap straight to
# nGL as the framebuffer, before any task exists. It used to be Muntcraft's own
# painted splash and went through the ConvertImg rule below. It is now vanilla's
# own loading screen -- the dirt tile behind the wordmark -- assembled here from
# the same two GUI files the menu already cuts up, because a full 320x240 frame
# is a composition rather than a one-to-one conversion.
LOADING := ../tools/textures/gen_loading_textures.py
LOADING_SOURCES := gui/options_background.png gui/title/minecraft.png
loading.h loading.png: $(LOADING) $(LOADING_SOURCES)
	python3 $(LOADING)

# The remaining sheets are the game's own images, converted one to one: the two
# pre-vanilla 512x512 terrain fallbacks that terrain.cpp still falls back to.
# blockselection is no longer here -- the block outline is vanilla's own GL_LINES
# wireframe and binds no texture at all. They are checked in, so this rule only
# runs when one of them is replaced by hand.
%.h: %.png
	ConvertImg --format=ngl $^ $@

%.h: %.bmp
	ConvertImg --format=ngl $^ $@


clean:
	rm -f $(OBJS)
