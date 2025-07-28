#include "stdafx.h"
#include "Peer.h"
#include "DBManager.h"
#include "ClientManager.h"
#include "GuildManager.h"
#include "ItemAwardManager.h"
#include "HB.h"
#include "PrivManager.h"
#include "MoneyLog.h"
#include "Marriage.h"
#include "BlockCountry.h"
#include "ItemIDRangeManager.h"
#include <signal.h>

int Start();

std::string g_stLocaleNameColumn = "name";
std::string g_stLocale = "latin2";

bool g_bHotBackup = false;

int g_iPlayerCacheFlushSeconds = 60*7;
int g_iItemCacheFlushSeconds = 60*5;
int g_iLogoutSeconds = 60*10;

int g_log = 1;

// MYSHOP_PRICE_LIST
int g_iItemPriceListTableCacheFlushSeconds = 540;
// END_OF_MYSHOP_PRICE_LIST

#ifdef __FreeBSD__
extern const char * _malloc_options;
#endif

extern void WriteVersion();

void emergency_sig(int sig)
{
	if (sig == SIGSEGV)
		sys_log(0, "SIGNAL: SIGSEGV");
	else if (sig == SIGUSR1)
		sys_log(0, "SIGNAL: SIGUSR1");

	if (sig == SIGSEGV)
		abort();
}

int main()
{
	WriteVersion();

#ifdef __FreeBSD__
	_malloc_options = "A";
#endif

	CNetPoller poller;
	CDBManager DBManager; 
	CClientManager ClientManager;
	PlayerHB player_hb;
	CGuildManager GuildManager;
	CPrivManager PrivManager;
	CMoneyLog MoneyLog;
	ItemAwardManager ItemAwardManager;
	marriage::CManager MarriageManager;
	CBlockCountry	BlockCountry;
	CItemIDRangeManager ItemIDRangeManager;
	
	if (!Start())
		return 1;

	GuildManager.Initialize();
	MarriageManager.Initialize();
	BlockCountry.Load();
	ItemIDRangeManager.Build();
	sys_log(0, "Metin2DBCacheServer Start\n");

	CClientManager::instance().MainLoop();

	signal_timer_disable();

	DBManager.Quit();
	int iCount;

	while (1)
	{
		iCount = 0;

		iCount += CDBManager::instance().CountReturnQuery(SQL_PLAYER);
		iCount += CDBManager::instance().CountAsyncQuery(SQL_PLAYER);

		if (iCount == 0)
			break;

		usleep(1000);
		sys_log(0, "WAITING_QUERY_COUNT %d", iCount);
	}

	return 1;
}

void emptybeat(LPHEART heart, int pulse)
{
	if (!(pulse % heart->passes_per_sec)) {}
}

int Start()
{
	fprintf(stderr, "Real Server\n");

	g_log = 1;
	fprintf(stderr, "Log On");

	log_set_expiration_days(3);
	thecore_init(50, emptybeat); // 50 = client heart beat 
	signal_timer_enable(60);

	CClientManager::instance().SetPlayerIDStart(1);

	int iRetry = 5;
	do
	{
		if (CDBManager::instance().Connect(SQL_PLAYER, "localhost", 0, "player", "root", "aKc~tG!M3?5MF5#Y"))
		{
			sys_log(0, "   OK");
			break;
		}

		sys_log(0, "   failed, retrying in 5 seconds");
		fprintf(stderr, "   failed, retrying in 5 seconds");
		sleep(5);
	} while (iRetry--);
	fprintf(stderr, "Success PLAYER\n");

	iRetry = 5;
	do
	{
		if (CDBManager::instance().Connect(SQL_ACCOUNT, "localhost", 0, "account", "root", "aKc~tG!M3?5MF5#Y"))
		{
			sys_log(0, "   OK");
			break;
		}

		sys_log(0, "   failed, retrying in 5 seconds");
		fprintf(stderr, "   failed, retrying in 5 seconds");
		sleep(5);
	} while (iRetry--);
	fprintf(stderr, "Success ACCOUNT\n");

	iRetry = 5;
	do
	{
		if (CDBManager::instance().Connect(SQL_COMMON, "localhost", 0, "common", "root", "aKc~tG!M3?5MF5#Y"))
		{
			sys_log(0, "   OK");
			break;
		}

		sys_log(0, "   failed, retrying in 5 seconds");
		fprintf(stderr, "   failed, retrying in 5 seconds");
		sleep(5);
	} while (iRetry--);
	fprintf(stderr, "Success COMMON\n");

	iRetry = 5;
	do
	{
		if (CDBManager::instance().Connect(SQL_HOTBACKUP, "localhost", 0, "hotbackup", "root", "aKc~tG!M3?5MF5#Y"))
		{
			sys_log(0, "   OK");
			break;
		}

		sys_log(0, "   failed, retrying in 5 seconds");
		fprintf(stderr, "   failed, retrying in 5 seconds");
		sleep(5);
	}
	while (iRetry--);
	fprintf(stderr, "Success HOTBACKUP\n");
	
	if (!CNetPoller::instance().Create())
	{
		sys_err("Cannot create network poller");
		return false;
	}

	sys_log(0, "ClientManager initialization.. ");

	if (!CClientManager::instance().Initialize())
	{
		sys_log(0, "   failed"); 
		return false;
	}

	sys_log(0, "   OK");

	if (!PlayerHB::instance().Initialize())
	{
		sys_err("cannot initialize player hotbackup");
		return false;
	}

	signal(SIGUSR1, emergency_sig);
	signal(SIGSEGV, emergency_sig);
	return true;
}
