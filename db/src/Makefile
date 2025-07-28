CC = g++

OBJDIR = obj
$(shell if [ ! -d $(OBJDIR) ]; then mkdir $(OBJDIR); fi)

CFLAGS = -g -Wall -O2 -pipe -fno-rtti -fno-exceptions -pthread -D_THREAD_SAFE -mtune=i686

INCDIR = -I../../boost -I../../libmysql/7.x-5.1.35
LIBDIR = -L../../libthecore/lib -L../../libsql -L../../libpoly -L../../libgame/lib -L/usr/local/lib/mysql -L../../libmysql/7.x-5.1.35
LIBS = -lthecore -lmysqlclient -lsql -lpoly -lgame -lm -lz

TARGET = ../db

SRCS =	NetBase.cpp Peer.cpp PeerBase.cpp Main.cpp Lock.cpp DBManager.cpp \
		Cache.cpp LoginData.cpp ClientManager.cpp ClientManagerPlayer.cpp ClientManagerLogin.cpp \
		ClientManagerBoot.cpp ClientManagerParty.cpp ClientManagerGuild.cpp GuildManager.cpp HB.cpp \
		PrivManager.cpp MoneyLog.cpp ItemAwardManager.cpp ClientManagerEventFlag.cpp Marriage.cpp \
		BlockCountry.cpp ItemIDRangeManager.cpp ClientManagerHorseName.cpp version.cpp \
		ProtoReader.cpp CsvReader.cpp 

OBJS = $(SRCS:%.cpp=$(OBJDIR)/%.o)

default: $(TARGET) 

$(TARGET): $(OBJS)
	@echo linking
	@$(CC) $(CFLAGS) $(LIBDIR) $(OBJS) $(LIBS) -o $(TARGET)
	@touch version.cpp

$(OBJDIR)/%.o: %.cpp
	@echo compile $<
	@$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

$(OBJDIR)/version.o: version.cpp
	@$(CC) $(CFLAGS) -D__USER__=\"$(USER)\" -D__HOSTNAME__=\"$(HOSTNAME)\" -D__PWD__=\"$(PWD)\" -c $< -o $@
	@echo compile $<

$(OBJDIR):
	@mkdir $(OBJDIR)

clean:
	@rm -f $(OBJS) ../db

dep:
	@touch Depend
	makedepend -fDepend $(INCDIR) -I/usr/include/c++/3.3 -I/usr/include/c++/4.2 -p$(OBJDIR)/ $(SRCS) 2> /dev/null


sinclude Depend
