#include "stdafx.h"
#include "locale_service.h"
#include "constants.h"
#include "banword.h"
#include "utils.h"
#include "mob_manager.h"
#include "config.h"
#include "skill_power.h"

using namespace std;

extern set<string> 	g_setQuestObjectDir;

string g_stServiceName;
string g_stServiceBasePath = "locale/romania";
string g_stServiceMapPath = "locale/romania/map";
string g_stQuestDir = "locale/romania/quest";
string g_stLocale = "latin2";

BYTE PK_PROTECT_LEVEL = 15;

int (*check_name) (const char * str) = NULL;

int is_twobyte_euckr(const char * str)
{
	return ishan(*str);
}

int check_name_independent(const char * str)
{
	if (CBanwordManager::instance().CheckString(str, strlen(str)))
		return 0;

	char szTmp[256];
	str_lower(str, szTmp, sizeof(szTmp));

	return 1;
}

int check_name_alphabet(const char * str)
{
	const char*	tmp;

	if (!str || !*str)
		return 0;

	if (strlen(str) < 2)
		return 0;

	for (tmp = str; *tmp; ++tmp)
	{
		if (isdigit(*tmp) || isalpha(*tmp))
			continue;
		else
			return 0;
	}

	return check_name_independent(str);
}

static void __CheckPlayerSlot(const std::string& service_name)
{
	if (PLAYER_PER_ACCOUNT != 4)
	{
		printf("<ERROR> PLAYER_PER_ACCOUNT = %d\n", PLAYER_PER_ACCOUNT);
		exit(0);
	}
}

bool LocaleService_Init(const std::string& c_rstServiceName)
{
	if (!g_stServiceName.empty())
	{
		sys_err("ALREADY exist service");
		return false;
	}

	g_stServiceName = c_rstServiceName;

	g_setQuestObjectDir.clear();
	g_setQuestObjectDir.insert("locale/romania/quest/object");


	fprintf(stdout, "Setting Locale \"%s\" (Path: %s)\n", g_stServiceName.c_str(), g_stServiceBasePath.c_str());

	__CheckPlayerSlot(g_stServiceName);
	
	return true;
}

void LocaleService_TransferDefaultSetting()
{
	check_name = check_name_alphabet;
	
	if (!CTableBySkill::instance().Check())
		exit(1);

	if (!aiPercentByDeltaLevForBoss)
		aiPercentByDeltaLevForBoss = aiPercentByDeltaLevForBoss_euckr;

	if (!aiPercentByDeltaLev)
		aiPercentByDeltaLev = aiPercentByDeltaLev_euckr;

	if (!aiChainLightningCountBySkillLevel)
		aiChainLightningCountBySkillLevel = aiChainLightningCountBySkillLevel_euckr;
}

const std::string& LocaleService_GetBasePath()
{
	return g_stServiceBasePath;
}

const std::string& LocaleService_GetMapPath()
{
	return g_stServiceMapPath;
}

const std::string& LocaleService_GetQuestPath()
{
	return g_stQuestDir;
}
