#include "stdafx.h"
#include "constants.h"
#include "BattleArena.h"
#include "start_position.h"
#include "char_manager.h"
#include "char.h"
#include "sectree_manager.h"
#include "regen.h"
#include "questmanager.h"

extern int passes_per_sec;

CBattleArena::CBattleArena() : m_pEvent(NULL), m_status(STATUS_CLOSE), m_nMapIndex(0), m_nEmpire(0), m_bForceEnd(false) {}

bool CBattleArena::IsRunning()
{
	return m_status == STATUS_CLOSE ? false : true;
}

bool CBattleArena::IsBattleArenaMap(int nMapIndex)
{
	return (nMapIndex == nBATTLE_ARENA_MAP[1] || nMapIndex == nBATTLE_ARENA_MAP[2] || nMapIndex == nBATTLE_ARENA_MAP[3]);
}

struct FWarpToHome
{
	void operator() (LPENTITY ent)
	{
		if (ent->IsType(ENTITY_CHARACTER))
		{
			LPCHARACTER lpChar = (LPCHARACTER)ent;

			if (lpChar->IsPC())
			{
				if (lpChar->IsGM()) 
					return;

				int nEmpire, nMapIndex, x, y;

				nEmpire = lpChar->GetEmpire();
				nMapIndex = EMPIRE_START_MAP(nEmpire);
				x = EMPIRE_START_X(nEmpire);
				y = EMPIRE_START_Y(nEmpire);

				lpChar->WarpSet(x, y, nMapIndex);
			}
		}
	}
};

void CBattleArena::SetStatus(BATTLEARENA_STATUS status)
{
	m_status = status;
}

EVENTINFO(SBattleArenaInfo)
{
	int nEmpire;
	int nMapIndex;
	int state;
	int wait_count;

	SBattleArenaInfo() : nEmpire(0), nMapIndex(0), state(0), wait_count(0) {}
};

EVENTFUNC(battle_arena_event)
{
	SBattleArenaInfo * pInfo = dynamic_cast<SBattleArenaInfo *>(event->info );
	if (!pInfo)
		return 0;
	else
	{
		switch (pInfo->state)
		{
			case 0:
				{
					++pInfo->state;
					BroadcastNotice("Cibirichi");
				}
				return PASSES_PER_SEC(240);

			case 1:
				{
					++pInfo->state;
					BroadcastNotice("Cibirichi2");
				}
				return PASSES_PER_SEC(60);

			case 2:
				{
					++pInfo->state;
					pInfo->wait_count = 0;

					quest::CQuestManager::instance().RequestSetEventFlag("battle_arena", 0);
					BroadcastNotice("Cibirichi3");

					LPSECTREE_MAP sectree = SECTREE_MANAGER::instance().GetMap(pInfo->nMapIndex);
					if (sectree)
					{
						std::string strMap = LocaleService_GetMapPath();
						if (pInfo->nEmpire > 0)
						{
							strMap += strRegen[pInfo->nEmpire];
							regen_do(strMap.c_str(), pInfo->nMapIndex, sectree->m_setting.iBaseX, sectree->m_setting.iBaseY, NULL, false);
						}
					}
				}
				return PASSES_PER_SEC(300);

			case 3:
				{
					if (SECTREE_MANAGER::instance().GetMonsterCountInMap(pInfo->nMapIndex) <= 0)
					{
						pInfo->state = 6;
						SendNoticeMap("Cibirichi1", pInfo->nMapIndex, false);
					}
					else
					{
						pInfo->wait_count++;

						if (pInfo->wait_count >= 5)
						{
							pInfo->state++;
							SendNoticeMap("Cibirichi4", pInfo->nMapIndex, false);
						}
						else
							CBattleArena::instance().SpawnRandomStone();
					}
				}
				return PASSES_PER_SEC(300); 
				
			case 4:
				{
					pInfo->state++;
					SendNoticeMap("Cibirichi5", pInfo->nMapIndex, false);
					SendNoticeMap("Cibirichi6", pInfo->nMapIndex, false);

					SECTREE_MANAGER::instance().PurgeMonstersInMap(pInfo->nMapIndex);
				}
				return PASSES_PER_SEC(30);

			case 5:
				{
					LPSECTREE_MAP sectree = SECTREE_MANAGER::instance().GetMap(pInfo->nMapIndex);
					if (sectree)
					{
						struct FWarpToHome f;
						sectree->for_each(f);
					}

					CBattleArena::instance().End();
				}
				return 0;

			case 6:
				{
					pInfo->state++;
					pInfo->wait_count = 0;

					SendNoticeMap("Cibirichi7", pInfo->nMapIndex, false);
					SendNoticeMap("Cibirichi8", pInfo->nMapIndex, false);

					CBattleArena::instance().SpawnLastBoss();
				}
				return PASSES_PER_SEC(300);

			case 7:
				{
					if (SECTREE_MANAGER::instance().GetMonsterCountInMap(pInfo->nMapIndex) <= 0)
					{
						SendNoticeMap("Cibirichi9", pInfo->nMapIndex, false);
						SendNoticeMap("Cibirichi6", pInfo->nMapIndex, false);

						pInfo->state = 5;

						return PASSES_PER_SEC(60);
					}

					pInfo->wait_count++;

					if (pInfo->wait_count >= 6)
					{
						SendNoticeMap("Cibirichi10", pInfo->nMapIndex, false);
						SendNoticeMap("Cibirichi6", pInfo->nMapIndex, false);

						SECTREE_MANAGER::instance().PurgeMonstersInMap(pInfo->nMapIndex);
						SECTREE_MANAGER::instance().PurgeStonesInMap(pInfo->nMapIndex);

						pInfo->state = 5;

						return PASSES_PER_SEC(60);
					}
					else
						CBattleArena::instance().SpawnRandomStone();
				}
				return PASSES_PER_SEC(300);
		}
	}
	
	return 0;
}

bool CBattleArena::Start(int nEmpire)
{
	if (m_status != STATUS_CLOSE) 
		return false;

	if (nEmpire < 1 || nEmpire > 3) 
		return false;

	m_nMapIndex = nBATTLE_ARENA_MAP[nEmpire];
	m_nEmpire = nEmpire;

	char szBuf[1024];
	snprintf(szBuf, sizeof(szBuf), "%s Cibirichi11", EMPIRE_NAME(m_nEmpire));
	BroadcastNotice(szBuf);
	BroadcastNotice("Cibirichi12");

	if (m_pEvent)
		event_cancel(&m_pEvent);

	SBattleArenaInfo* info = AllocEventInfo<SBattleArenaInfo>();

	info->nMapIndex = m_nMapIndex;
	info->nEmpire = m_nEmpire;
	info->state = 0;
	info->wait_count = 0;

	m_pEvent = event_create(battle_arena_event, info, PASSES_PER_SEC(300));

	SetStatus(STATUS_BATTLE);

	quest::CQuestManager::instance().RequestSetEventFlag("battle_arena", m_nMapIndex);

	return true;
}

void CBattleArena::End()
{
	if (m_pEvent)
		event_cancel(&m_pEvent);

	m_bForceEnd = false;
	m_nMapIndex = 0;
	m_nEmpire = 0;
	m_status = STATUS_CLOSE;

	quest::CQuestManager::instance().RequestSetEventFlag("battle_arena", 0);
}

void CBattleArena::ForceEnd()
{
	if (m_bForceEnd) 
		return;

	m_bForceEnd = true;

	if (m_pEvent)
		event_cancel(&m_pEvent);

	SBattleArenaInfo* info = AllocEventInfo<SBattleArenaInfo>();

	info->nMapIndex = m_nMapIndex;
	info->nEmpire = m_nEmpire;
	info->state = 3;

	event_create(battle_arena_event, info, PASSES_PER_SEC(5));
}

void CBattleArena::SpawnRandomStone()
{
	static const DWORD vnum[7] = { 8012, 8013, 8014, 8024, 8025, 8026, 8027 };

	static const int region_info[3][4] = {
	   	{688900, 247600, 692700, 250000},
	   	{740200, 251000, 744200, 247700},
	   	{791400, 250900, 795900, 250400} };

	int idx = m_nMapIndex - 190;
	if (idx < 0 || idx >= 3) 
		return;

	CHARACTER_MANAGER::instance().SpawnMobRange(vnum[number(0, 6)], m_nMapIndex, region_info[idx][0], region_info[idx][1], region_info[idx][2], region_info[idx][3], false, true);
}

void CBattleArena::SpawnLastBoss()
{
	static const DWORD vnum = 8616;

	static const int position[3][2] = {
		{ 691000, 248900 },
		{ 742300, 249000 },
		{ 793600, 249300 } };

	int idx = m_nMapIndex - 190;
	if ( idx < 0 || idx >= 3 ) 
		return;

	CHARACTER_MANAGER::instance().SpawnMob(vnum, m_nMapIndex, position[idx][0], position[idx][1], 0);
}
