CC = g++

OBJDIR = obj
$(shell if [ ! -d $(OBJDIR) ]; then mkdir $(OBJDIR); fi)

CFLAGS = -g -Wall -O2 -m32 -pipe -fexceptions -D_THREAD_SAFE -DNDEBUG -mtune=i686 -fstack-protector-all

INCDIR = -I../../libdevil -I../../libmysql/7.x-5.1.35 -I../../../Extern/include/boost -I../../../Extern/include -I../../liblua/include -I/usr/local/include -L/usr/local/lib/mysql
LIBDIR = -L../../libdevil -L../../libmysql/7.x-5.1.35 -L../../../Extern/lib -L../../libthecore/lib -L../../libpoly -L../../libsql -L../../libgame/lib -L../../liblua/lib -L/usr/local/lib
LIBS = -pthread -lm -lmd -lIL -lpng -ltiff -lmng -llcms -ljpeg -lmysqlclient -L/usr/local/lib/mysql -lz -lcryptopp -lthecore -lpoly -llua -llualib -lsql -lgame

TARGET  = ../game

CFILE	= minilzo.c

CPPFILE = BattleArena.cpp FSM.cpp MarkConvert.cpp MarkImage.cpp MarkManager.cpp OXEvent.cpp TrafficProfiler.cpp ani.cpp\
		  arena.cpp banword.cpp battle.cpp blend_item.cpp block_country.cpp buffer_manager.cpp building.cpp castle.cpp\
		  char.cpp char_affect.cpp char_battle.cpp char_change_empire.cpp char_horse.cpp char_item.cpp char_manager.cpp\
		  char_quickslot.cpp char_resist.cpp char_skill.cpp char_state.cpp PetSystem.cpp cmd.cpp cmd_emotion.cpp cmd_general.cpp\
		  cmd_gm.cpp cmd_oxevent.cpp config.cpp constants.cpp crc32.cpp cube.cpp db.cpp desc.cpp\
		  desc_client.cpp desc_manager.cpp desc_p2p.cpp dungeon.cpp entity.cpp\
		  entity_view.cpp event.cpp event_queue.cpp exchange.cpp file_loader.cpp fishing.cpp gm.cpp guild.cpp\
		  guild_manager.cpp guild_war.cpp horse_rider.cpp horsename_manager.cpp input.cpp input_auth.cpp input_db.cpp\
		  input_login.cpp input_main.cpp input_p2p.cpp input_udp.cpp\
		  item.cpp item_addon.cpp item_attribute.cpp item_manager.cpp item_manager_idrange.cpp\
		  locale_service.cpp log.cpp login_data.cpp lzo_manager.cpp marriage.cpp\
		  messenger_manager.cpp mining.cpp mob_manager.cpp motion.cpp over9refine.cpp p2p.cpp packet_info.cpp\
		  party.cpp polymorph.cpp priv_manager.cpp pvp.cpp\
		  questevent.cpp questlua.cpp questlua_affect.cpp questlua_arena.cpp questlua_ba.cpp questlua_building.cpp\
		  questlua_danceevent.cpp questlua_dungeon.cpp questlua_forked.cpp questlua_game.cpp questlua_global.cpp\
		  questlua_guild.cpp questlua_horse.cpp questlua_pet.cpp questlua_item.cpp questlua_marriage.cpp\
		  questlua_npc.cpp questlua_oxevent.cpp questlua_party.cpp questlua_pc.cpp\
		  questlua_quest.cpp questlua_target.cpp questmanager.cpp questnpc.cpp questpc.cpp\
		  refine.cpp regen.cpp safebox.cpp sectree.cpp sectree_manager.cpp sequence.cpp shop.cpp\
		  skill.cpp start_position.cpp target.cpp text_file_loader.cpp trigger.cpp utils.cpp vector.cpp war_map.cpp\
		  wedding.cpp xmas_event.cpp version.cpp threeway_war.cpp map_location.cpp\
		  BlueDragon.cpp BlueDragon_Binder.cpp DragonLair.cpp questlua_dragonlair.cpp\
		  skill_power.cpp affect.cpp\
		  FileMonitor_FreeBSD.cpp ClientPackageCryptInfo.cpp cipher.cpp\
		  buff_on_attributes.cpp\
		  group_text_parse_tree.cpp\
		  shop_manager.cpp shopEx.cpp item_manager_read_tables.cpp


COBJS	= $(CFILE:%.c=$(OBJDIR)/%.o)
CPPOBJS	= $(CPPFILE:%.cpp=$(OBJDIR)/%.o)

MAINOBJ = $(OBJDIR)/main.o
MAINCPP = main.cpp

default: $(TARGET)

$(OBJDIR)/minilzo.o: minilzo.c
	@$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@
	@echo compile $<

$(OBJDIR)/version.o: version.cpp
	@$(CC) $(CFLAGS) -D__P4_VERSION__=\"metin2\" -c $< -o $@
	@echo compile $<

$(OBJDIR)/%.o: %.cpp
	@echo compile $<
	@$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

limit_time:
	@echo update limit time
	@python update_limit_time.py

$(TARGET): $(CPPOBJS) $(COBJS) $(MAINOBJ)
	@echo linking $(TARGET)
	@$(CC) $(CFLAGS) $(LIBDIR) $(COBJS) $(CPPOBJS) $(MAINOBJ) $(LIBS) -o $(TARGET)

clean:
	@rm -f $(COBJS) $(CPPOBJS) $(MAINOBJ)
	@rm -f ../game

tag:
	ctags *.cpp *.h *.c

dep:
	makedepend -f Depend $(INCDIR) -I/usr/include/c++/3.3 -I/usr/include/c++/4.2 -p$(OBJDIR)/ $(CPPFILE) $(CFILE) $(MAINCPP) 2> /dev/null > Depend

sinclude Depend
