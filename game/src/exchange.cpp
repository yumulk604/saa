#include "stdafx.h"
#include "../../libgame/include/grid.h"
#include "utils.h"
#include "desc.h"
#include "desc_client.h"
#include "char.h"
#include "item.h"
#include "item_manager.h"
#include "packet.h"
#include "log.h"
#include "db.h"
#include "locale_service.h"
#include "../../common/length.h"
#include "exchange.h"

void exchange_packet(LPCHARACTER ch, BYTE sub_header, bool is_me, DWORD arg1, TItemPos arg2, DWORD arg3, void * pvData = NULL);

// ±³È¯ ÆÐÅ¶
void exchange_packet(LPCHARACTER ch, BYTE sub_header, bool is_me, DWORD arg1, TItemPos arg2, DWORD arg3, void * pvData)
{
	if (!ch->GetDesc())
		return;

	struct packet_exchange pack_exchg;

	pack_exchg.header 		= HEADER_GC_EXCHANGE;
	pack_exchg.sub_header 	= sub_header;
	pack_exchg.is_me		= is_me;
	pack_exchg.arg1		= arg1;
	pack_exchg.arg2		= arg2;
	pack_exchg.arg3		= arg3;

	if (sub_header == EXCHANGE_SUBHEADER_GC_ITEM_ADD && pvData)
	{
		thecore_memcpy(&pack_exchg.alSockets, ((LPITEM) pvData)->GetSockets(), sizeof(pack_exchg.alSockets));
		thecore_memcpy(&pack_exchg.aAttr, ((LPITEM) pvData)->GetAttributes(), sizeof(pack_exchg.aAttr));
	}
	else
	{
		memset(&pack_exchg.alSockets, 0, sizeof(pack_exchg.alSockets));
		memset(&pack_exchg.aAttr, 0, sizeof(pack_exchg.aAttr));
	}

	ch->GetDesc()->Packet(&pack_exchg, sizeof(pack_exchg));
}

// ±³È¯À» ½ÃÀÛ
bool CHARACTER::ExchangeStart(LPCHARACTER victim)
{
	if (this == victim)
		return false;

	if (IsObserverMode())
	{
		ChatPacket(CHAT_TYPE_INFO, "Eºti observator.");
		return false;
	}

	if (victim->IsNPC())
		return false;

	//PREVENT_TRADE_WINDOW
	if ( IsOpenSafebox() || GetShopOwner() || GetMyShop() || IsCubeOpen())
	{
		ChatPacket( CHAT_TYPE_INFO, "O altã operaþiune are loc." );
		return false;
	}

	if ( victim->IsOpenSafebox() || victim->GetShopOwner() || victim->GetMyShop() || victim->IsCubeOpen() )
	{
		ChatPacket( CHAT_TYPE_INFO, "O altã operaþiune are loc în cazul celuilalt jucãtor." );
		return false;
	}
	//END_PREVENT_TRADE_WINDOW
	int iDist = DISTANCE_APPROX(GetX() - victim->GetX(), GetY() - victim->GetY());

	if (iDist >= EXCHANGE_MAX_DISTANCE)
		return false;

	if (GetExchange())
		return false;

	if (victim->GetExchange())
	{
		exchange_packet(this, EXCHANGE_SUBHEADER_GC_ALREADY, 0, 0, NPOS, 0);
		return false;
	}

	if (victim->IsBlockMode(BLOCK_EXCHANGE))
	{
		ChatPacket(CHAT_TYPE_INFO, "Jucãtorul a dezactivat opþiunea.");
		return false;
	}

	SetExchange(M2_NEW CExchange(this));
	victim->SetExchange(M2_NEW CExchange(victim));

	victim->GetExchange()->SetCompany(GetExchange());
	GetExchange()->SetCompany(victim->GetExchange());

	//
	SetExchangeTime();
	victim->SetExchangeTime();

	exchange_packet(victim, EXCHANGE_SUBHEADER_GC_START, 0, GetVID(), NPOS, 0);
	exchange_packet(this, EXCHANGE_SUBHEADER_GC_START, 0, victim->GetVID(), NPOS, 0);

	return true;
}

CExchange::CExchange(LPCHARACTER pOwner)
{
	m_pCompany = NULL;

	m_bAccept = false;

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		m_apItems[i] = NULL;
		m_aItemPos[i] = NPOS;
		m_abItemDisplayPos[i] = 0;
	}

	m_lGold = 0;

	m_pOwner = pOwner;
	pOwner->SetExchange(this);

	m_pGrid = M2_NEW CGrid(4,3);
}

CExchange::~CExchange()
{
	M2_DELETE(m_pGrid);
}

bool CExchange::AddItem(TItemPos item_pos, BYTE display_pos)
{
	assert(m_pOwner != NULL && GetCompany());

	if (!item_pos.IsValidItemPosition())
		return false;

	// Àåºñ´Â ±³È¯ÇÒ ¼ö ¾øÀ½
	if (item_pos.IsEquipPosition())
		return false;

	LPITEM item;

	if (!(item = m_pOwner->GetItem(item_pos)))
		return false;

	if (IS_SET(item->GetAntiFlag(), ITEM_ANTIFLAG_GIVE))
	{
		m_pOwner->ChatPacket(CHAT_TYPE_INFO, "Obiectul nu poate fi negociat.");
		return false;
	}

	if (true == item->isLocked())
	{
		return false;
	}

	if (item->IsExchanging())
	{
		sys_log(0, "EXCHANGE under exchanging");
		return false;
	}

	if (!m_pGrid->IsEmpty(display_pos, 1, item->GetSize()))
	{
		sys_log(0, "EXCHANGE not empty item_pos %d %d %d", display_pos, 1, item->GetSize());
		return false;
	}

	Accept(false);
	GetCompany()->Accept(false);

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (m_apItems[i])
			continue;

		m_apItems[i]		= item;
		m_aItemPos[i]		= item_pos;
		m_abItemDisplayPos[i]	= display_pos;
		m_pGrid->Put(display_pos, 1, item->GetSize());

		item->SetExchanging(true);

		exchange_packet(m_pOwner, 
				EXCHANGE_SUBHEADER_GC_ITEM_ADD,
				true,
				item->GetVnum(),
				TItemPos(RESERVED_WINDOW, display_pos),
				item->GetCount(),
				item);

		exchange_packet(GetCompany()->GetOwner(),
				EXCHANGE_SUBHEADER_GC_ITEM_ADD, 
				false, 
				item->GetVnum(),
				TItemPos(RESERVED_WINDOW, display_pos),
				item->GetCount(),
				item);

		sys_log(0, "EXCHANGE AddItem success %s pos(%d, %d) %d", item->GetName(), item_pos.window_type, item_pos.cell, display_pos);

		return true;
	}

	// Ãß°¡ÇÒ °ø°£ÀÌ ¾øÀ½
	return false;
}

bool CExchange::RemoveItem(BYTE pos)
{
	if (pos >= EXCHANGE_ITEM_MAX_NUM)
		return false;

	if (!m_apItems[pos])
		return false;

	TItemPos PosOfInventory = m_aItemPos[pos];
	m_apItems[pos]->SetExchanging(false);

	m_pGrid->Get(m_abItemDisplayPos[pos], 1, m_apItems[pos]->GetSize());

	exchange_packet(GetOwner(),	EXCHANGE_SUBHEADER_GC_ITEM_DEL, true, pos, NPOS, 0);
	exchange_packet(GetCompany()->GetOwner(), EXCHANGE_SUBHEADER_GC_ITEM_DEL, false, pos, PosOfInventory, 0);

	Accept(false);
	GetCompany()->Accept(false);

	m_apItems[pos]	    = NULL;
	m_aItemPos[pos]	    = NPOS;
	m_abItemDisplayPos[pos] = 0;
	return true;
}

bool CExchange::AddGold(long gold)
{
	if (gold <= 0)
		return false;

	if (GetOwner()->GetGold() < gold)
	{
		// °¡Áö°í ÀÖ´Â µ·ÀÌ ºÎÁ·.
		exchange_packet(GetOwner(), EXCHANGE_SUBHEADER_GC_LESS_GOLD, 0, 0, NPOS, 0);
		return false;
	}

	if ( m_lGold > 0 )
	{
		return false;
	}

	Accept(false);
	GetCompany()->Accept(false);

	m_lGold = gold;

	exchange_packet(GetOwner(), EXCHANGE_SUBHEADER_GC_GOLD_ADD, true, m_lGold, NPOS, 0);
	exchange_packet(GetCompany()->GetOwner(), EXCHANGE_SUBHEADER_GC_GOLD_ADD, false, m_lGold, NPOS, 0);
	return true;
}

// µ·ÀÌ ÃæºÐÈ÷ ÀÖ´ÂÁö, ±³È¯ÇÏ·Á´Â ¾ÆÀÌÅÛÀÌ ½ÇÁ¦·Î ÀÖ´ÂÁö È®ÀÎ ÇÑ´Ù.
bool CExchange::Check(int * piItemCount)
{
	if (GetOwner()->GetGold() < m_lGold)
		return false;

	int item_count = 0;

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!m_apItems[i])
			continue;

		if (!m_aItemPos[i].IsValidItemPosition())
			return false;

		if (m_apItems[i] != GetOwner()->GetItem(m_aItemPos[i]))
			return false;

		++item_count;
	}

	*piItemCount = item_count;
	return true;
}

bool CExchange::CheckSpace()
{
	static CGrid s_grid1(5, INVENTORY_MAX_NUM/5 / 3); // inven page 1
	static CGrid s_grid2(5, INVENTORY_MAX_NUM/5 / 3); // inven page 2
	static CGrid s_grid3(5, INVENTORY_MAX_NUM/5 / 3); // inven page 3

	s_grid1.Clear();
	s_grid2.Clear();
	s_grid3.Clear();

	LPCHARACTER	victim = GetCompany()->GetOwner();
	LPITEM item;

	int i;
	const int perPageSlotCount = INVENTORY_MAX_NUM / 3;

	for (i = 0; i < INVENTORY_MAX_NUM; ++i) 
	{
		if (!(item = victim->GetInventoryItem(i)))
			continue;

		BYTE itemSize = item->GetSize();

		if (i < perPageSlotCount)
			s_grid1.Put(i, 1, itemSize);
		else if (i < perPageSlotCount * 2)
			s_grid2.Put(i - perPageSlotCount, 1, itemSize);
		else
			s_grid3.Put(i - perPageSlotCount * 2, 1, itemSize);
	}
	
	for (i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!(item = m_apItems[i]))
			continue;

		int iPos = s_grid1.FindBlank(1, item->GetSize());

		if (iPos >= 0)
			s_grid1.Put(iPos, 1, item->GetSize());
		else
		{
			iPos = s_grid2.FindBlank(1, item->GetSize());

			if (iPos >= 0)
				s_grid2.Put(iPos, 1, item->GetSize());
			else
			{
				iPos = s_grid3.FindBlank(1, item->GetSize());

				if (iPos >= 0)
					s_grid3.Put(iPos, 1, item->GetSize());
				else
					return false;
			}
		}
	}

	return true;
}

bool CExchange::Done()
{
	int		empty_pos, i;
	LPITEM	item;

	LPCHARACTER	victim = GetCompany()->GetOwner();

	for (i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
	{
		if (!(item = m_apItems[i]))
			continue;

		empty_pos = victim->GetEmptyInventory(item->GetSize());

		if (empty_pos < 0)
		{
			sys_err("Exchange::Done : Cannot find blank position in inventory %s <-> %s item %s", 
					m_pOwner->GetName(), victim->GetName(), item->GetName());
			continue;
		}

		assert(empty_pos >= 0);

		m_pOwner->SyncQuickslot(QUICKSLOT_TYPE_ITEM, item->GetCell(), 255);

		item->RemoveFromCharacter();
		item->AddToCharacter(victim, TItemPos(INVENTORY, empty_pos));
		ITEM_MANAGER::instance().FlushDelayedSave(item);

		item->SetExchanging(false);
		{
			char exchange_buf[51];

			snprintf(exchange_buf, sizeof(exchange_buf), "%s %u %u", item->GetName(), GetOwner()->GetPlayerID(), item->GetCount());
			LogManager::instance().ItemLog(victim, item, "EXCHANGE_TAKE", exchange_buf);

			snprintf(exchange_buf, sizeof(exchange_buf), "%s %u %u", item->GetName(), victim->GetPlayerID(), item->GetCount());
			LogManager::instance().ItemLog(GetOwner(), item, "EXCHANGE_GIVE", exchange_buf);

			if (item->GetVnum() >= 80003 && item->GetVnum() <= 80007)
			{
				LogManager::instance().GoldBarLog(victim->GetPlayerID(), item->GetID(), EXCHANGE_TAKE, "");
				LogManager::instance().GoldBarLog(GetOwner()->GetPlayerID(), item->GetID(), EXCHANGE_GIVE, "");
			}
		}

		m_apItems[i] = NULL;
	}

	if (m_lGold)
	{
		GetOwner()->PointChange(POINT_GOLD, -m_lGold, true);
		victim->PointChange(POINT_GOLD, m_lGold, true);

		if (m_lGold > 1000)
		{
			char exchange_buf[51];
			snprintf(exchange_buf, sizeof(exchange_buf), "%u %s", GetOwner()->GetPlayerID(), GetOwner()->GetName());
			LogManager::instance().CharLog(victim, m_lGold, "EXCHANGE_GOLD_TAKE", exchange_buf);

			snprintf(exchange_buf, sizeof(exchange_buf), "%u %s", victim->GetPlayerID(), victim->GetName());
			LogManager::instance().CharLog(GetOwner(), m_lGold, "EXCHANGE_GOLD_GIVE", exchange_buf);
		}
	}

	m_pGrid->Clear();
	return true;
}

// ±³È¯À» µ¿ÀÇ
bool CExchange::Accept(bool bAccept)
{
	if (m_bAccept == bAccept)
		return true;

	m_bAccept = bAccept;

	// µÑ ´Ù µ¿ÀÇ ÇßÀ¸¹Ç·Î ±³È¯ ¼º¸³
	if (m_bAccept && GetCompany()->m_bAccept)
	{
		int	iItemCount;

		LPCHARACTER victim = GetCompany()->GetOwner();

		//PREVENT_PORTAL_AFTER_EXCHANGE
		GetOwner()->SetExchangeTime();
		victim->SetExchangeTime();		
		//END_PREVENT_PORTAL_AFTER_EXCHANGE

		if (!Check(&iItemCount))
		{
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Yang insuficient.");
			victim->ChatPacket(CHAT_TYPE_INFO, "Cibirichi34");
			goto EXCHANGE_END;
		}

		if (!CheckSpace())
		{
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Nu existã spaþiu disponibil.");
			victim->ChatPacket(CHAT_TYPE_INFO, "Loc insuficient.");
			goto EXCHANGE_END;
		}

		if (!GetCompany()->Check(&iItemCount))
		{
			victim->ChatPacket(CHAT_TYPE_INFO, "Yang insuficient.");
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Cibirichi34");
			goto EXCHANGE_END;
		}

		if (!GetCompany()->CheckSpace())
		{
			victim->ChatPacket(CHAT_TYPE_INFO, "Nu existã spaþiu disponibil.");
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Loc insuficient.");
			goto EXCHANGE_END;
		}

		if (db_clientdesc->GetSocket() == INVALID_SOCKET)
		{
			sys_err("Cannot use exchange feature while DB cache connection is dead.");
			victim->ChatPacket(CHAT_TYPE_INFO, "Unknown error");
			GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Unknown error");
			goto EXCHANGE_END;
		}

		if (Done())
		{
			if (m_lGold)
				GetOwner()->Save();

			if (GetCompany()->Done())
			{
				if (GetCompany()->m_lGold)
					victim->Save();

				GetOwner()->ChatPacket(CHAT_TYPE_INFO, "Comerþul cu %s a fost fãcut cu succes.", victim->GetName());
				victim->ChatPacket(CHAT_TYPE_INFO, "Comerþul cu %s a fost fãcut cu succes.", GetOwner()->GetName());
			}
		}

EXCHANGE_END:
		Cancel();
		return false;
	}
	else
	{
		exchange_packet(GetOwner(), EXCHANGE_SUBHEADER_GC_ACCEPT, true, m_bAccept, NPOS, 0);
		exchange_packet(GetCompany()->GetOwner(), EXCHANGE_SUBHEADER_GC_ACCEPT, false, m_bAccept, NPOS, 0);
		return true;
	}
}

void CExchange::Cancel()
{
	exchange_packet(GetOwner(), EXCHANGE_SUBHEADER_GC_END, 0, 0, NPOS, 0);
	GetOwner()->SetExchange(NULL);

	for (int i = 0; i < EXCHANGE_ITEM_MAX_NUM; ++i)
		if (m_apItems[i])
			m_apItems[i]->SetExchanging(false);

	if (GetCompany())
	{
		GetCompany()->SetCompany(NULL);
		GetCompany()->Cancel();
	}

	M2_DELETE(this);
}
