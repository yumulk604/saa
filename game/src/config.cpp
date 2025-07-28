#include "stdafx.h"
#include <sstream>
#include <ifaddrs.h>
#include "constants.h"
#include "utils.h"
#include "log.h"
#include "desc.h"
#include "desc_manager.h"
#include "item_manager.h"
#include "p2p.h"
#include "char.h"
#include "war_map.h"
#include "locale_service.h"
#include "config.h"
#include "db.h"
#include "skill_power.h"

using std::string;

BYTE	g_bChannel = 1;
WORD	mother_port = 21000;
int		passes_per_sec = 25;
WORD	db_port = 11000;
WORD	p2p_port = 13000;
char	db_addr[ADDRESS_MAX_LEN + 1] = "localhost\0";
int		save_event_second_cycle = passes_per_sec * 120;
int		ping_event_second_cycle = passes_per_sec * 60;
bool	g_bNoMoreClient = false;
bool	g_bNoRegen = false;

// TRAFFIC_PROFILER
bool		g_bTrafficProfileOn = false;
DWORD		g_dwTrafficProfileFlushCycle = 3600;
// END_OF_TRAFFIC_PROFILER

bool		guild_mark_server = true;
BYTE		guild_mark_min_level = 3;
bool		no_wander = false;
int		g_iUserLimit = 2000;

char		g_szPublicIP[16] = "0";
char		g_szInternalIP[16] = "0";
bool		g_bSkillDisable = false;
int			g_iFullUserCount = 1200;
int			g_iBusyUserCount = 650;
bool		g_bEmpireWhisper = true;
BYTE		g_bAuthServer = false;

string	g_stClientVersion = "1215955206";

string	g_stAuthMasterIP;
WORD g_wAuthMasterPort = 0;

static std::set<DWORD> s_set_dwFileCRC;
static std::set<DWORD> s_set_dwProcessCRC;

string g_stHostname = "";

std::set<string> g_setQuestObjectDir;

string g_stBlockDate = "30000705";

extern string g_stLocale;

int SPEEDHACK_LIMIT_COUNT   = 50;
int SPEEDHACK_LIMIT_BONUS   = 80;
int g_iSyncHackLimitCount = 20;

int VIEW_RANGE = 5000;
int VIEW_BONUS_RANGE = 500;

string g_strWebMallURL = "www.site.ro";

unsigned int g_uiSpamBlockDuration = 60 * 15;
unsigned int g_uiSpamBlockScore = 100;
unsigned int g_uiSpamReloadCycle = 60 * 10;

bool		g_bCheckMultiHack = true;

int			g_iSpamBlockMaxLevel = 30;

void		LoadStateUserCount();
void		LoadValidCRCList();

int gPlayerMaxLevel = 99;

bool g_BlockCharCreation = false;

bool is_string_true(const char * string)
{
	bool	result = 0;
	if (isnhdigit(*string))
	{
		str_to_number(result, string);
		return result > 0 ? true : false;
	}
	else if (LOWER(*string) == 't')
		return true;
	else
		return false;
}

static std::set<int> s_set_map_allows;

bool map_allow_find(int index)
{
	if (g_bAuthServer)
		return false;

	if (s_set_map_allows.find(index) == s_set_map_allows.end())
		return false;

	return true;
}

void map_allow_log()
{
	std::set<int>::iterator i;

	for (i = s_set_map_allows.begin(); i != s_set_map_allows.end(); ++i)
		sys_log(0, "MAP_ALLOW: %d", *i);
}

void map_allow_add(int index)
{
	if (map_allow_find(index))
	{
		fprintf(stdout, "!!! FATAL ERROR !!! multiple MAP_ALLOW setting!!\n");
		exit(1);
	}

	s_set_map_allows.insert(index);
}

void map_allow_copy(long * pl, int size)
{
	int iCount = 0;
	std::set<int>::iterator it = s_set_map_allows.begin();

	while (it != s_set_map_allows.end())
	{
		int i = *(it++);
		*(pl++) = i;

		if (++iCount > size)
			break;
	}
}

bool GetIPInfo()
{
	struct ifaddrs* ifaddrp = NULL;

	if (0 != getifaddrs(&ifaddrp))
		return false;

	for( struct ifaddrs* ifap=ifaddrp ; NULL != ifap ; ifap = ifap->ifa_next )
	{
		struct sockaddr_in * sai = (struct sockaddr_in *) ifap->ifa_addr;

		if (!ifap->ifa_netmask ||  // ignore if no netmask
				sai->sin_addr.s_addr == 0 || // ignore if address is 0.0.0.0
				sai->sin_addr.s_addr == 16777343) // ignore if address is 127.0.0.1
			continue;

		char * netip = inet_ntoa(sai->sin_addr);

#define CIBIRICHI_TEST
#ifdef CIBIRICHI_TEST
		if (!strncmp(netip, "999.999", 7))
#else
		if (!strncmp(netip, "192.168", 7)) // ignore if address is starting with 192
#endif
		{
			strlcpy(g_szInternalIP, netip, sizeof(g_szInternalIP));
			fprintf(stderr, "INTERNAL_IP: %s interface %s\n", netip, ifap->ifa_name);
		}
		else if (!strncmp(netip, "10.", 3))
		{
			strlcpy(g_szInternalIP, netip, sizeof(g_szInternalIP));
			fprintf(stderr, "INTERNAL_IP: %s interface %s\n", netip, ifap->ifa_name);
		}
		else if (g_szPublicIP[0] == '0')
		{
			strlcpy(g_szPublicIP, netip, sizeof(g_szPublicIP));
			fprintf(stderr, "PUBLIC_IP: %s interface %s\n", netip, ifap->ifa_name);
		}
	}

	freeifaddrs(ifaddrp);

	if (g_szPublicIP[0] != '0')
		return true;
	else
		return false;
}

void config_init(const string& st_localeServiceName)
{
	FILE	*fp;

	char	buf[256];
	char	token_string[256];
	char	value_string[256];

	// LOCALE_SERVICE
	string	st_configFileName;

	st_configFileName.reserve(32);
	st_configFileName = "CONFIG";

	if (!st_localeServiceName.empty())
	{
		st_configFileName += ".";
		st_configFileName += st_localeServiceName;
	}
	// END_OF_LOCALE_SERVICE

	if (!(fp = fopen(st_configFileName.c_str(), "r")))
	{
		fprintf(stderr, "Can not open [%s]\n", st_configFileName.c_str());
		exit(1);
	}

	if (!GetIPInfo())
	{
		fprintf(stderr, "Can not get public ip address\n");
		exit(1);
	}

	char db_host[2][64], db_user[2][64], db_pwd[2][64], db_db[2][64];
	int mysql_db_port[2];

	for (int n = 0; n < 2; ++n)
	{
		*db_host[n]	= '\0';
		*db_user[n] = '\0';
		*db_pwd[n]= '\0';
		*db_db[n]= '\0';
		mysql_db_port[n] = 0;
	}

	char log_host[64], log_user[64], log_pwd[64], log_db[64];
	int log_port = 0;

	*log_host = '\0';
	*log_user = '\0';
	*log_pwd = '\0';
	*log_db = '\0';

	bool isCommonSQL = false;	
	bool isPlayerSQL = false;

	FILE* fpOnlyForDB;

	if (!(fpOnlyForDB = fopen(st_configFileName.c_str(), "r")))
	{
		fprintf(stderr, "Can not open [%s]\n", st_configFileName.c_str());
		exit(1);
	}

	while (fgets(buf, 256, fpOnlyForDB))
	{
		parse_token(buf, token_string, value_string);

		TOKEN("BLOCK_LOGIN")
		{
			g_stBlockDate = value_string;
		}

		TOKEN("hostname")
		{
			g_stHostname = value_string;
			fprintf(stdout, "HOSTNAME: %s\n", g_stHostname.c_str());
			continue;
		}

		TOKEN("channel")
		{
			str_to_number(g_bChannel, value_string);
			continue;
		}

		TOKEN("player_sql")
		{
			const char * line = two_arguments(value_string, db_host[0], sizeof(db_host[0]), db_user[0], sizeof(db_user[0]));
			line = two_arguments(line, db_pwd[0], sizeof(db_pwd[0]), db_db[0], sizeof(db_db[0]));

			if (0 != line[0])
			{
				char buf[256];
				one_argument(line, buf, sizeof(buf));
				str_to_number(mysql_db_port[0], buf);
			}

			if (!*db_host[0] || !*db_user[0] || !*db_pwd[0] || !*db_db[0])
			{
				fprintf(stderr, "PLAYER_SQL syntax: logsql <host user password db>\n");
				exit(1);
			}

			char buf[1024];
			snprintf(buf, sizeof(buf), "PLAYER_SQL: %s %s %s %s %d", db_host[0], db_user[0], db_pwd[0], db_db[0], mysql_db_port[0]);
			isPlayerSQL = true;
			continue;
		}

		TOKEN("common_sql")
		{
			const char * line = two_arguments(value_string, db_host[1], sizeof(db_host[1]), db_user[1], sizeof(db_user[1]));
			line = two_arguments(line, db_pwd[1], sizeof(db_pwd[1]), db_db[1], sizeof(db_db[1]));

			if (0 != line[0])
			{
				char buf[256];
				one_argument(line, buf, sizeof(buf));
				str_to_number(mysql_db_port[1], buf);
			}

			if (!*db_host[1] || !*db_user[1] || !*db_pwd[1] || !*db_db[1])
			{
				fprintf(stderr, "COMMON_SQL syntax: logsql <host user password db>\n");
				exit(1);
			}

			char buf[1024];
			snprintf(buf, sizeof(buf), "COMMON_SQL: %s %s %s %s %d", db_host[1], db_user[1], db_pwd[1], db_db[1], mysql_db_port[1]);
			isCommonSQL = true;
			continue;
		}

		TOKEN("log_sql")
		{
			const char * line = two_arguments(value_string, log_host, sizeof(log_host), log_user, sizeof(log_user));
			line = two_arguments(line, log_pwd, sizeof(log_pwd), log_db, sizeof(log_db));

			if (0 != line[0])
			{
				char buf[256];
				one_argument(line, buf, sizeof(buf));
				str_to_number(log_port, buf);
			}

			if (!*log_host || !*log_user || !*log_pwd || !*log_db)
			{
				fprintf(stderr, "LOG_SQL syntax: logsql <host user password db>\n");
				exit(1);
			}

			char buf[1024];
			snprintf(buf, sizeof(buf), "LOG_SQL: %s %s %s %s %d", log_host, log_user, log_pwd, log_db, log_port);
			continue;
		}
	}

	fclose(fpOnlyForDB);

	// CONFIG_SQL_INFO_ERROR
	if (!isCommonSQL)
	{
		puts("LOAD_COMMON_SQL_INFO_FAILURE:");
		puts("");
		puts("CONFIG:");
		puts("------------------------------------------------");
		puts("COMMON_SQL: HOST USER PASSWORD DATABASE");
		puts("");
		exit(1);
	}

	if (!isPlayerSQL)
	{
		puts("LOAD_PLAYER_SQL_INFO_FAILURE:");
		puts("");
		puts("CONFIG:");
		puts("------------------------------------------------");
		puts("PLAYER_SQL: HOST USER PASSWORD DATABASE");
		puts("");
		exit(1);
	}

	// Common DB °¡ Locale Á¤º¸¸¦ °¡Áö°í ÀÖ±â ¶§¹®¿¡ °¡Àå ¸ÕÀú Á¢¼ÓÇØ¾ß ÇÑ´Ù.
	AccountDB::instance().Connect(db_host[1], mysql_db_port[1], db_user[1], db_pwd[1], db_db[1]);

	if (false == AccountDB::instance().IsConnected())
	{
		fprintf(stderr, "cannot start server while no common sql connected\n");
		exit(1);
	}

	fprintf(stdout, "CommonSQL connected\n");

	// ·ÎÄÉÀÏ Á¤º¸¸¦ °¡Á®¿ÀÀÚ 
	// <°æ°í> Äõ¸®¹®¿¡ Àý´ë Á¶°Ç¹®(WHERE) ´ÞÁö ¸¶¼¼¿ä. (´Ù¸¥ Áö¿ª¿¡¼­ ¹®Á¦°¡ »ý±æ¼ö ÀÖ½À´Ï´Ù)
	{
		char szQuery[512];
		snprintf(szQuery, sizeof(szQuery), "SELECT mKey, mValue FROM locale");

		std::auto_ptr<SQLMsg> pMsg(AccountDB::instance().DirectQuery(szQuery));

		if (pMsg->Get()->uiNumRows == 0)
		{
			fprintf(stderr, "COMMON_SQL: DirectQuery failed : %s\n", szQuery);
			exit(1);
		}

		MYSQL_ROW row; 

		while (NULL != (row = mysql_fetch_row(pMsg->Get()->pSQLResult)))
		{
			// ·ÎÄÉÀÏ ¼¼ÆÃ
			if (strcasecmp(row[0], "LOCALE") == 0)
			{
				if (LocaleService_Init(row[1]) == false)
				{
					fprintf(stderr, "COMMON_SQL: invalid locale key %s\n", row[1]);
					exit(1);
				}
			}
		}
	}

	fprintf(stdout, "Setting DB to locale %s\n", g_stLocale.c_str());

	AccountDB::instance().SetLocale(g_stLocale);

	AccountDB::instance().ConnectAsync(db_host[1], mysql_db_port[1], db_user[1], db_pwd[1], db_db[1], g_stLocale.c_str());

	// Player DB Á¢¼Ó
	DBManager::instance().Connect(db_host[0], mysql_db_port[0], db_user[0], db_pwd[0], db_db[0]);

	if (!DBManager::instance().IsConnected())
	{
		fprintf(stderr, "PlayerSQL.ConnectError\n");
		exit(1);
	}

	fprintf(stdout, "PlayerSQL connected\n");

	if (!g_bAuthServer)
	{
		LogManager::instance().Connect(log_host, log_port, log_user, log_pwd, log_db);

		if (!LogManager::instance().IsConnected())
		{
			fprintf(stderr, "LogSQL.ConnectError\n");
			exit(1);
		}

		fprintf(stdout, "LogSQL connected\n");

		LogManager::instance().BootLog(g_stHostname.c_str(), g_bChannel);
	}

	{
		char szQuery[256];
		snprintf(szQuery, sizeof(szQuery), "SELECT mValue FROM locale WHERE mKey='SKILL_POWER_BY_LEVEL'");
		std::auto_ptr<SQLMsg> pMsg(AccountDB::instance().DirectQuery(szQuery));

		if (pMsg->Get()->uiNumRows == 0)
		{
			fprintf(stderr, "[SKILL_PERCENT] Query failed: %s", szQuery);
			exit(1);
		}

		MYSQL_ROW row; 

		row = mysql_fetch_row(pMsg->Get()->pSQLResult);

		const char * p = row[0];
		int cnt = 0;
		char num[128];
		int aiBaseSkillPowerByLevelTable[SKILL_MAX_LEVEL+1];

		fprintf(stdout, "SKILL_POWER_BY_LEVEL %s\n", p);
		while (*p != '\0' && cnt < (SKILL_MAX_LEVEL + 1))
		{
			p = one_argument(p, num, sizeof(num));
			aiBaseSkillPowerByLevelTable[cnt++] = atoi(num);

			//fprintf(stdout, "%d %d\n", cnt - 1, aiBaseSkillPowerByLevelTable[cnt - 1]);
			if (*p == '\0')
			{
				if (cnt != (SKILL_MAX_LEVEL + 1))
				{
					fprintf(stderr, "[SKILL_PERCENT] locale table has not enough skill information! (count: %d query: %s)", cnt, szQuery);
					exit(1);
				}

				fprintf(stdout, "SKILL_POWER_BY_LEVEL: Done! (count %d)\n", cnt);
				break;
			}
		}

		// Á¾Á·º° ½ºÅ³ ¼¼ÆÃ
		for (int job = 0; job < JOB_MAX_NUM * 2; ++job)
		{
			snprintf(szQuery, sizeof(szQuery), "SELECT mValue from locale where mKey='SKILL_POWER_BY_LEVEL_TYPE%d' ORDER BY CAST(mValue AS unsigned)", job);
			std::auto_ptr<SQLMsg> pMsg(AccountDB::instance().DirectQuery(szQuery));

			// ¼¼ÆÃÀÌ ¾ÈµÇ¾îÀÖÀ¸¸é ±âº»Å×ÀÌºíÀ» »ç¿ëÇÑ´Ù.
			if (pMsg->Get()->uiNumRows == 0)
			{
				CTableBySkill::instance().SetSkillPowerByLevelFromType(job, aiBaseSkillPowerByLevelTable);
				continue;
			}

			row = mysql_fetch_row(pMsg->Get()->pSQLResult);
			cnt = 0;
			p = row[0];
			int aiSkillTable[SKILL_MAX_LEVEL + 1];

			fprintf(stdout, "SKILL_POWER_BY_JOB %d %s\n", job, p);
			while (*p != '\0' && cnt < (SKILL_MAX_LEVEL + 1))
			{			
				p = one_argument(p, num, sizeof(num));
				aiSkillTable[cnt++] = atoi(num);

				//fprintf(stdout, "%d %d\n", cnt - 1, aiBaseSkillPowerByLevelTable[cnt - 1]);
				if (*p == '\0')
				{
					if (cnt != (SKILL_MAX_LEVEL + 1))
					{
						fprintf(stderr, "[SKILL_PERCENT] locale table has not enough skill information! (count: %d query: %s)", cnt, szQuery);
						exit(1);
					}

					fprintf(stdout, "SKILL_POWER_BY_JOB: Done! (job: %d count: %d)\n", job, cnt);
					break;
				}
			}

			CTableBySkill::instance().SetSkillPowerByLevelFromType(job, aiSkillTable);
		}		
	}
	// END_SKILL_POWER_BY_LEVEL

	// LOG_KEEP_DAYS_EXTEND
	log_set_expiration_days(2);
	// END_OF_LOG_KEEP_DAYS_EXTEND

	while (fgets(buf, 256, fp))
	{
		parse_token(buf, token_string, value_string);

		TOKEN("empire_whisper")
		{
			bool b_value = 0;
			str_to_number(b_value, value_string);
			g_bEmpireWhisper = !!b_value;
			continue;
		}

		TOKEN("mark_server")
		{
			guild_mark_server = is_string_true(value_string);
			continue;
		}

		TOKEN("mark_min_level")
		{
			str_to_number(guild_mark_min_level, value_string);
			guild_mark_min_level = MINMAX(0, guild_mark_min_level, GUILD_MAX_LEVEL);
			continue;
		}

		TOKEN("port")
		{
			str_to_number(mother_port, value_string);
			continue;
		}

		TOKEN("log_keep_days")
		{
			int i = 0;
			str_to_number(i, value_string);
			log_set_expiration_days(MINMAX(1, i, 90));
			continue;
		}

		TOKEN("passes_per_sec")
		{
			str_to_number(passes_per_sec, value_string);
			continue;
		}

		TOKEN("p2p_port")
		{
			str_to_number(p2p_port, value_string);
			continue;
		}

		TOKEN("save_event_second_cycle")
		{
			int	cycle = 0;
			str_to_number(cycle, value_string);
			save_event_second_cycle = cycle * passes_per_sec;
			continue;
		}

		TOKEN("ping_event_second_cycle")
		{
			int	cycle = 0;
			str_to_number(cycle, value_string);
			ping_event_second_cycle = cycle * passes_per_sec;
			continue;
		}

		TOKEN("shutdowned")
		{
			g_bNoMoreClient = true;
			continue;
		}

		TOKEN("no_regen")
		{
			g_bNoRegen = true;
			continue;
		}

		TOKEN("traffic_profile")
		{
			g_bTrafficProfileOn = true;
			continue;
		}

		TOKEN("map_allow")
		{
			char * p = value_string;
			string stNum;

			for (; *p; p++)
			{   
				if (isnhspace(*p))
				{
					if (stNum.length())
					{
						int	index = 0;
						str_to_number(index, stNum.c_str());
						map_allow_add(index);
						stNum.clear();
					}
				}
				else
					stNum += *p;
			}

			if (stNum.length())
			{
				int	index = 0;
				str_to_number(index, stNum.c_str());
				map_allow_add(index);
			}

			fprintf(stdout, "MAP_ALLOW: ");
			for (std::set<int>::iterator i = s_set_map_allows.begin(); i != s_set_map_allows.end(); ++i)
				fprintf(stdout, "%d " , *i);

			continue;
		}

		TOKEN("auth_server")
		{
			char szIP[32];
			char szPort[32];

			two_arguments(value_string, szIP, sizeof(szIP), szPort, sizeof(szPort));

			if (!*szIP || (!*szPort && strcasecmp(szIP, "master")))
			{
				fprintf(stderr, "AUTH_SERVER: syntax error: <ip|master> <port>\n");
				exit(1);
			}

			g_bAuthServer = true;

			if (!strcasecmp(szIP, "master"))
				fprintf(stdout, "AUTH_SERVER: I am the master\n");
			else
			{
				g_stAuthMasterIP = szIP;
				str_to_number(g_wAuthMasterPort, szPort);

				fprintf(stdout, "AUTH_SERVER: master %s %u\n", g_stAuthMasterIP.c_str(), g_wAuthMasterPort);
			}
			continue;
		}

		TOKEN("synchack_limit_count")
		{
			str_to_number(g_iSyncHackLimitCount, value_string);
		}

		TOKEN("speedhack_limit_count")
		{
			str_to_number(SPEEDHACK_LIMIT_COUNT, value_string);
		}

		TOKEN("speedhack_limit_bonus")
		{
			str_to_number(SPEEDHACK_LIMIT_BONUS, value_string);
		}

		TOKEN("bind_ip")
		{
			strlcpy(g_szPublicIP, value_string, sizeof(g_szPublicIP));
		}

		TOKEN("view_range")
		{
			str_to_number(VIEW_RANGE, value_string);
		}

		TOKEN("spam_block_duration")
		{
			str_to_number(g_uiSpamBlockDuration, value_string);
		}

		TOKEN("spam_block_score")
		{
			str_to_number(g_uiSpamBlockScore, value_string);
			g_uiSpamBlockScore = MAX(1, g_uiSpamBlockScore);
		}

		TOKEN("spam_block_reload_cycle")
		{
			str_to_number(g_uiSpamReloadCycle, value_string);
			g_uiSpamReloadCycle = MAX(60, g_uiSpamReloadCycle);
		}

		TOKEN("check_multihack")
		{
			str_to_number(g_bCheckMultiHack, value_string);
		}

		TOKEN("spam_block_max_level")
		{
			str_to_number(g_iSpamBlockMaxLevel, value_string);
		}

		TOKEN("pk_protect_level")
		{
		    str_to_number(PK_PROTECT_LEVEL, value_string);
		    fprintf(stderr, "PK_PROTECT_LEVEL: %d", PK_PROTECT_LEVEL);
		}

		TOKEN("block_char_creation")
		{
			int tmp = 0;

			str_to_number(tmp, value_string);

			if (0 == tmp)
				g_BlockCharCreation = false;
			else
				g_BlockCharCreation = true;

			continue;
		}
	}

	if (0 == db_port)
	{
		fprintf(stderr, "DB_PORT not configured\n");
		exit(1);
	}

	if (0 == g_bChannel)
	{
		fprintf(stderr, "CHANNEL not configured\n");
		exit(1);
	}

	if (g_stHostname.empty())
	{
		fprintf(stderr, "HOSTNAME must be configured.\n");
		exit(1);
	}

	// LOCALE_SERVICE 
	LocaleService_TransferDefaultSetting();
	// END_OF_LOCALE_SERVICE

	fclose(fp);

	LoadValidCRCList();
	LoadStateUserCount();

	CWarMapManager::instance().LoadWarMapInfo(NULL);
}

void LoadValidCRCList()
{
	s_set_dwProcessCRC.clear();
	s_set_dwFileCRC.clear();

	FILE * fp;
	char buf[256];

	if ((fp = fopen("CRC", "r")))
	{
		while (fgets(buf, 256, fp))
		{
			if (!*buf)
				continue;

			DWORD dwValidClientProcessCRC;
			DWORD dwValidClientFileCRC;

			sscanf(buf, " %u %u ", &dwValidClientProcessCRC, &dwValidClientFileCRC);

			s_set_dwProcessCRC.insert(dwValidClientProcessCRC);
			s_set_dwFileCRC.insert(dwValidClientFileCRC);

			fprintf(stderr, "CLIENT_CRC: %u %u\n", dwValidClientProcessCRC, dwValidClientFileCRC);
		}

		fclose(fp);
	}
}

void CheckClientVersion()
{
	const DESC_MANAGER::DESC_SET & set = DESC_MANAGER::instance().GetClientSet();
	DESC_MANAGER::DESC_SET::const_iterator it = set.begin();

	while (it != set.end())
	{
		LPDESC d = *(it++);

		if (!d->GetCharacter())
			continue;


		int version = atoi(g_stClientVersion.c_str());
		int date	= atoi(d->GetClientVersion());

		if (version != date)
		{
			d->GetCharacter()->ChatPacket(CHAT_TYPE_NOTICE, "Versiunea clientului nu este compatibilã.");
			d->DelayedDisconnect(10);
		}
	}
}

void LoadStateUserCount()
{
	FILE * fp = fopen("state_user_count", "r");
	if (!fp)
		return;

	fscanf(fp, " %d %d ", &g_iFullUserCount, &g_iBusyUserCount);
	fclose(fp);
}

bool IsValidProcessCRC(DWORD dwCRC)
{
	return s_set_dwProcessCRC.find(dwCRC) != s_set_dwProcessCRC.end();
}

bool IsValidFileCRC(DWORD dwCRC)
{
	return s_set_dwFileCRC.find(dwCRC) != s_set_dwFileCRC.end();
}
