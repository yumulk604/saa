#include "stdafx.h"
#include "utils.h"
#include "config.h"
#include "char.h"
#include "packet.h"
#include "desc_client.h"
#include "buffer_manager.h"
#include "char_manager.h"
#include "db.h"
#include "guild.h"
#include "guild_manager.h"
#include "affect.h"
#include "p2p.h"
#include "questmanager.h"
#include "building.h"
#include "locale_service.h"
#include "log.h"
#include "questmanager.h"

	SGuildMember::SGuildMember(LPCHARACTER ch, BYTE grade, DWORD offer_exp)
: pid(ch->GetPlayerID()), grade(grade), is_general(0), job(ch->GetJob()), level(ch->GetLevel()), offer_exp(offer_exp), name(ch->GetName())
{}
	SGuildMember::SGuildMember(DWORD pid, BYTE grade, BYTE is_general, BYTE job, BYTE level, DWORD offer_exp, char* name)
: pid(pid), grade(grade), is_general(is_general), job(job), level(level), offer_exp(offer_exp), name(name)
{}

namespace 
{
	struct FGuildNameSender
	{
		FGuildNameSender(DWORD id, const char* guild_name) : id(id), name(guild_name)
		{
			p.header = HEADER_GC_GUILD;
			p.subheader = GUILD_SUBHEADER_GC_GUILD_NAME;
			p.size = sizeof(p) + sizeof(DWORD) + GUILD_NAME_MAX_LEN;
		}

		void operator() (LPCHARACTER ch)
		{
			LPDESC d = ch->GetDesc();

			if (d)
			{
				d->BufferedPacket(&p, sizeof(p));
				d->BufferedPacket(&id, sizeof(id));
				d->Packet(name, GUILD_NAME_MAX_LEN);
			}
		}

		DWORD id;
		const char * name;
		TPacketGCGuild p;
	};
}

CGuild::CGuild(TGuildCreateParameter & cp)
{
	Initialize();

	m_general_count = 0;

	strlcpy(m_data.name, cp.name, sizeof(m_data.name));
	m_data.master_pid = cp.master->GetPlayerID();
	strlcpy(m_data.grade_array[0].grade_name, "Lider", sizeof(m_data.grade_array[0].grade_name));
	m_data.grade_array[0].auth_flag = GUILD_AUTH_ADD_MEMBER | GUILD_AUTH_REMOVE_MEMBER | GUILD_AUTH_NOTICE | GUILD_AUTH_USE_SKILL;

	for (int i = 1; i < GUILD_GRADE_COUNT; ++i)
	{
		strlcpy(m_data.grade_array[i].grade_name, "Membru", sizeof(m_data.grade_array[i].grade_name));
		m_data.grade_array[i].auth_flag = 0;
	}

	std::auto_ptr<SQLMsg> pmsg (DBManager::instance().DirectQuery(
				"INSERT INTO guild (name, master, sp, level, exp, skill_point, skill) "
				"VALUES('%s', %u, 1000, 1, 0, 0, '\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0')", 
				m_data.name, m_data.master_pid));

	m_data.guild_id = pmsg->Get()->uiInsertID;

	for (int i = 0; i < GUILD_GRADE_COUNT; ++i)
	{
		DBManager::instance().Query("INSERT INTO guild_grade VALUES(%u, %d, '%s', %d)", 
				m_data.guild_id, 
				i + 1, 
				m_data.grade_array[i].grade_name, 
				m_data.grade_array[i].auth_flag);
	}

	ComputeGuildPoints();
	m_data.power	= m_data.max_power;
	m_data.ladder_point	= 0;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_CREATE, 0, &m_data.guild_id, sizeof(DWORD));

	TPacketGuildSkillUpdate guild_skill;
	guild_skill.guild_id = m_data.guild_id;
	guild_skill.amount = 0;
	guild_skill.skill_point = 0;
	memset(guild_skill.skill_levels, 0, GUILD_SKILL_COUNT);

	db_clientdesc->DBPacket(HEADER_GD_GUILD_SKILL_UPDATE, 0, &guild_skill, sizeof(guild_skill));

	// TODO GUILD_NAME
	CHARACTER_MANAGER::instance().for_each_pc(FGuildNameSender(GetID(), GetName()));
	/*
	   TPacketDGGuildMember p;
	   memset(&p, 0, sizeof(p));
	   p.dwPID = cp.master->GetPlayerID();
	   p.bGrade = 15;
	   AddMember(&p);
	 */
	RequestAddMember(cp.master, GUILD_LEADER_GRADE);
}

void CGuild::Initialize()
{
	memset(&m_data, 0, sizeof(m_data));
	m_data.level = 1;

	for (int i = 0; i < GUILD_SKILL_COUNT; ++i)
		abSkillUsable[i] = true;
}

CGuild::~CGuild()
{
}

void CGuild::RequestAddMember(LPCHARACTER ch, int grade)
{
	if (ch->GetGuild())
		return;

	TPacketGDGuildAddMember gd;

	if (m_member.find(ch->GetPlayerID()) != m_member.end())
	{
		sys_err("Already a member in guild %s[%d]", ch->GetName(), ch->GetPlayerID());
		return;
	}

	gd.dwPID = ch->GetPlayerID();
	gd.dwGuild = GetID();
	gd.bGrade = grade;

	db_clientdesc->DBPacket(HEADER_GD_GUILD_ADD_MEMBER, 0, &gd, sizeof(TPacketGDGuildAddMember));
}

void CGuild::AddMember(TPacketDGGuildMember * p)
{
	TGuildMemberContainer::iterator it;

	if ((it = m_member.find(p->dwPID)) == m_member.end())
		m_member.insert(std::make_pair(p->dwPID, TGuildMember(p->dwPID, p->bGrade, p->isGeneral, p->bJob, p->bLevel, p->dwOffer, p->szName)));
	else
	{
		TGuildMember & r_gm = it->second;
		r_gm.pid = p->dwPID;
		r_gm.grade = p->bGrade;
		r_gm.job = p->bJob;
		r_gm.offer_exp = p->dwOffer;
		r_gm.is_general = p->isGeneral;
	}

	CGuildManager::instance().Link(p->dwPID, this);

	SendListOneToAll(p->dwPID);

	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(p->dwPID);

	sys_log(0, "GUILD: AddMember PID %u, grade %u, job %u, level %u, offer %u, name %s ptr %p",
			p->dwPID, p->bGrade, p->bJob, p->bLevel, p->dwOffer, p->szName, get_pointer(ch));

	if (ch)
		LoginMember(ch);
	else
		P2PLoginMember(p->dwPID);
}

bool CGuild::RequestRemoveMember(DWORD pid)
{
	TGuildMemberContainer::iterator it;

	if ((it = m_member.find(pid)) == m_member.end())
		return false;

	if (it->second.grade == GUILD_LEADER_GRADE)
		return false;

	TPacketGuild gd_guild;

	gd_guild.dwGuild = GetID();
	gd_guild.dwInfo = pid;

	db_clientdesc->DBPacket(HEADER_GD_GUILD_REMOVE_MEMBER, 0, &gd_guild, sizeof(TPacketGuild));
	return true;
}

bool CGuild::RemoveMember(DWORD pid)
{
	sys_log(0, "Receive Guild P2P RemoveMember");
	TGuildMemberContainer::iterator it;

	if ((it = m_member.find(pid)) == m_member.end())
		return false;

	if (it->second.grade == GUILD_LEADER_GRADE)
		return false;

	if (it->second.is_general)
		m_general_count--;

	m_member.erase(it);
	SendOnlineRemoveOnePacket(pid);

	CGuildManager::instance().Unlink(pid);

	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(pid);

	if (ch)
	{
		//GuildRemoveAffect(ch);
		m_memberOnline.erase(ch);
		ch->SetGuild(NULL);
	}

	return true;
}

void CGuild::P2PLoginMember(DWORD pid)
{
	if (m_member.find(pid) == m_member.end())
	{
		sys_err("GUILD [%d] is not a memeber of guild.", pid);
		return;
	}

	m_memberP2POnline.insert(pid);

	// Login event occur + Send List
	TGuildMemberOnlineContainer::iterator it;

	for (it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
		SendLoginPacket(*it, pid);
}

void CGuild::LoginMember(LPCHARACTER ch)
{
	if (m_member.find(ch->GetPlayerID()) == m_member.end())
	{
		sys_err("GUILD %s[%d] is not a memeber of guild.", ch->GetName(), ch->GetPlayerID());
		return;
	}

	ch->SetGuild(this);

	// Login event occur + Send List
	TGuildMemberOnlineContainer::iterator it;

	for (it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
		SendLoginPacket(*it, ch);

	m_memberOnline.insert(ch);

	SendAllGradePacket(ch);
	SendGuildInfoPacket(ch);
	SendListPacket(ch);
	SendSkillInfoPacket(ch);
	SendEnemyGuild(ch);

	//GuildUpdateAffect(ch);
}

void CGuild::P2PLogoutMember(DWORD pid)
{
	if (m_member.find(pid)==m_member.end())
	{
		sys_err("GUILD [%d] is not a memeber of guild.", pid);
		return;
	}

	m_memberP2POnline.erase(pid);

	// Logout event occur
	TGuildMemberOnlineContainer::iterator it;
	for (it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
	{
		SendLogoutPacket(*it, pid);
	}
}

void CGuild::LogoutMember(LPCHARACTER ch)
{
	if (m_member.find(ch->GetPlayerID())==m_member.end())
	{
		sys_err("GUILD %s[%d] is not a memeber of guild.", ch->GetName(), ch->GetPlayerID());
		return;
	}

	//GuildRemoveAffect(ch);

	//ch->SetGuild(NULL);
	m_memberOnline.erase(ch);

	// Logout event occur
	TGuildMemberOnlineContainer::iterator it;
	for (it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
	{
		SendLogoutPacket(*it, ch);
	}
}

void CGuild::SendOnlineRemoveOnePacket(DWORD pid)
{
	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+4;
	pack.subheader = GUILD_SUBHEADER_GC_REMOVE;

	TEMP_BUFFER buf;
	buf.write(&pack,sizeof(pack));
	buf.write(&pid, sizeof(pid));

	TGuildMemberOnlineContainer::iterator it;

	for (it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
	{
		LPDESC d = (*it)->GetDesc();

		if (d)
			d->Packet(buf.read_peek(), buf.size());
	}
}

void CGuild::SendAllGradePacket(LPCHARACTER ch)
{
	LPDESC d = ch->GetDesc();
	if (!d)
		return;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+1+GUILD_GRADE_COUNT*(sizeof(TGuildGrade)+1);
	pack.subheader = GUILD_SUBHEADER_GC_GRADE;

	TEMP_BUFFER buf;

	buf.write(&pack, sizeof(pack));
	BYTE n = 15;
	buf.write(&n, 1);

	for (int i=0;i<GUILD_GRADE_COUNT;i++)
	{
		BYTE j = i+1;
		buf.write(&j, 1);
		buf.write(&m_data.grade_array[i], sizeof(TGuildGrade));
	}

	d->Packet(buf.read_peek(), buf.size());
}

void CGuild::SendListOneToAll(LPCHARACTER ch)
{
	SendListOneToAll(ch->GetPlayerID());
}

void CGuild::SendListOneToAll(DWORD pid)
{

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(TPacketGCGuild);
	pack.subheader = GUILD_SUBHEADER_GC_LIST;

	pack.size += sizeof(TGuildMemberPacketData);

	char c[CHARACTER_NAME_MAX_LEN+1];
	memset(c, 0, sizeof(c));

	TGuildMemberContainer::iterator cit = m_member.find(pid);
	if (cit == m_member.end())
		return;

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it!= m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();
		if (!d) 
			continue;

		TEMP_BUFFER buf;

		buf.write(&pack, sizeof(pack));

		cit->second._dummy = 1;

		buf.write(&(cit->second), sizeof(DWORD) * 3 +1);
		buf.write(cit->second.name.c_str(), cit->second.name.length());
		buf.write(c, CHARACTER_NAME_MAX_LEN + 1 - cit->second.name.length());
		d->Packet(buf.read_peek(), buf.size());
	}
}

void CGuild::SendListPacket(LPCHARACTER ch)
{
	/*
	   List Packet

	   Header
	   Count (byte)
	   [
	   ...
	   name_flag 1 - ÀÌ¸§À» º¸³»´À³Ä ¾Èº¸³»´À³Ä
	   name CHARACTER_NAME_MAX_LEN+1
	   ] * Count

	 */
	LPDESC d;
	if (!(d=ch->GetDesc()))
		return;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(TPacketGCGuild);
	pack.subheader = GUILD_SUBHEADER_GC_LIST;

	pack.size += sizeof(TGuildMemberPacketData) * m_member.size();

	TEMP_BUFFER buf;

	buf.write(&pack,sizeof(pack));

	char c[CHARACTER_NAME_MAX_LEN+1];

	for (TGuildMemberContainer::iterator it = m_member.begin(); it != m_member.end(); ++it)
	{
		it->second._dummy = 1;

		buf.write(&(it->second), sizeof(DWORD)*3+1);

		strlcpy(c, it->second.name.c_str(), MIN(sizeof(c), it->second.name.length() + 1));

		buf.write(c, CHARACTER_NAME_MAX_LEN+1 );
	}

	d->Packet(buf.read_peek(), buf.size());

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		SendLoginPacket(ch, *it);
	}

	for (TGuildMemberP2POnlineContainer::iterator it = m_memberP2POnline.begin(); it != m_memberP2POnline.end(); ++it)
	{
		SendLoginPacket(ch, *it);
	}

}

void CGuild::SendLoginPacket(LPCHARACTER ch, LPCHARACTER chLogin)
{
	SendLoginPacket(ch, chLogin->GetPlayerID());
}

void CGuild::SendLoginPacket(LPCHARACTER ch, DWORD pid)
{
	/*
	   Login Packet
	   header 4
	   pid 4
	 */
	if (!ch->GetDesc())
		return;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+4;
	pack.subheader = GUILD_SUBHEADER_GC_LOGIN;

	TEMP_BUFFER buf;

	buf.write(&pack, sizeof(pack));

	buf.write(&pid, 4);

	ch->GetDesc()->Packet(buf.read_peek(), buf.size());
}

void CGuild::SendLogoutPacket(LPCHARACTER ch, LPCHARACTER chLogout)
{
	SendLogoutPacket(ch, chLogout->GetPlayerID());
}

void CGuild::SendLogoutPacket(LPCHARACTER ch, DWORD pid)
{
	/*
	   Logout Packet
	   header 4
	   pid 4
	 */
	if (!ch->GetDesc())
		return;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+4;
	pack.subheader = GUILD_SUBHEADER_GC_LOGOUT;

	TEMP_BUFFER buf;

	buf.write(&pack, sizeof(pack));
	buf.write(&pid, 4);

	ch->GetDesc()->Packet(buf.read_peek(), buf.size());
}

void CGuild::LoadGuildMemberData(SQLMsg* pmsg)
{
	if (pmsg->Get()->uiNumRows == 0)
		return;

	m_general_count = 0;

	m_member.clear();

	for (uint i = 0; i < pmsg->Get()->uiNumRows; ++i)
	{
		MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);

		DWORD pid = strtoul(row[0], (char**) NULL, 10);
		BYTE grade = (BYTE) strtoul(row[1], (char**) NULL, 10);
		BYTE is_general = 0;

		if (row[2] && *row[2] == '1')
			is_general = 1;

		DWORD offer = strtoul(row[3], (char**) NULL, 10);
		BYTE level = (BYTE)strtoul(row[4], (char**) NULL, 10);
		BYTE job = (BYTE)strtoul(row[5], (char**) NULL, 10);
		char * name = row[6];

		if (is_general)
			m_general_count++;

		m_member.insert(std::make_pair(pid, TGuildMember(pid, grade, is_general, job, level, offer, name)));
		CGuildManager::instance().Link(pid, this);
	}
}

void CGuild::LoadGuildGradeData(SQLMsg* pmsg)
{
	/*
    // 15°³ ¾Æ´Ò °¡´É¼º Á¸Àç
	if (pmsg->Get()->iNumRows != 15)
	{
		sys_err("Query failed: getting guild grade data. GuildID(%d)", GetID());
		return;
	}
	*/
	for (uint i = 0; i < pmsg->Get()->uiNumRows; ++i)
	{
		MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);
		BYTE grade = 0;
		str_to_number(grade, row[0]);
		char * name = row[1];
		DWORD auth = strtoul(row[2], NULL, 10);

		if (grade >= 1 && grade <= 15)
		{
			//sys_log(0, "GuildGradeLoad %s", name);
			strlcpy(m_data.grade_array[grade-1].grade_name, name, sizeof(m_data.grade_array[grade-1].grade_name));
			m_data.grade_array[grade-1].auth_flag = auth;
		}
	}
}
void CGuild::LoadGuildData(SQLMsg* pmsg)
{
	if (pmsg->Get()->uiNumRows == 0)
	{
		sys_err("Query failed: getting guild data %s", pmsg->stQuery.c_str());
		return;
	}

	MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);
	m_data.master_pid = strtoul(row[0], (char **)NULL, 10);
	m_data.level = (BYTE)strtoul(row[1], (char **)NULL, 10);
	m_data.exp = strtoul(row[2], (char **)NULL, 10);
	strlcpy(m_data.name, row[3], sizeof(m_data.name));

	m_data.skill_point = (BYTE) strtoul(row[4], (char **) NULL, 10);
	if (row[5])
		thecore_memcpy(m_data.abySkill, row[5], sizeof(BYTE) * GUILD_SKILL_COUNT);
	else
		memset(m_data.abySkill, 0, sizeof(BYTE) * GUILD_SKILL_COUNT);

	m_data.power = MAX(0, strtoul(row[6], (char **) NULL, 10));

	str_to_number(m_data.ladder_point, row[7]);

	if (m_data.ladder_point < 0)
		m_data.ladder_point = 0;

	str_to_number(m_data.win, row[8]);
	str_to_number(m_data.draw, row[9]);
	str_to_number(m_data.loss, row[10]);
	str_to_number(m_data.gold, row[11]);

	ComputeGuildPoints();
}

void CGuild::Load(DWORD guild_id)
{
	Initialize();

	m_data.guild_id = guild_id;

	DBManager::instance().FuncQuery(std::bind1st(std::mem_fun(&CGuild::LoadGuildData), this), 
			"SELECT master, level, exp, name, skill_point, skill, sp, ladder_point, win, draw, loss, gold FROM guild WHERE id = %u", m_data.guild_id);

	sys_log(0, "GUILD: loading guild id %12s %u", m_data.name, guild_id);

	DBManager::instance().FuncQuery(std::bind1st(std::mem_fun(&CGuild::LoadGuildGradeData), this), 
			"SELECT grade, name, auth+0 FROM guild_grade WHERE guild_id = %u", m_data.guild_id);

	DBManager::instance().FuncQuery(std::bind1st(std::mem_fun(&CGuild::LoadGuildMemberData), this), 
			"SELECT pid, grade, is_general, offer, level, job, name FROM guild_member, player WHERE guild_id = %u and pid = id", guild_id);
}

void CGuild::SaveLevel()
{
	DBManager::instance().Query("UPDATE guild SET level=%d, exp=%u, skill_point=%d WHERE id = %u", m_data.level,m_data.exp, m_data.skill_point,m_data.guild_id);
}

void CGuild::SendDBSkillUpdate(int amount)
{
	TPacketGuildSkillUpdate guild_skill;
	guild_skill.guild_id = m_data.guild_id;
	guild_skill.amount = amount;
	guild_skill.skill_point = m_data.skill_point;
	thecore_memcpy(guild_skill.skill_levels, m_data.abySkill, sizeof(BYTE) * GUILD_SKILL_COUNT);

	db_clientdesc->DBPacket(HEADER_GD_GUILD_SKILL_UPDATE, 0, &guild_skill, sizeof(guild_skill));
}

void CGuild::SaveSkill()
{
	char text[GUILD_SKILL_COUNT * 2 + 1];

	DBManager::instance().EscapeString(text, sizeof(text), (const char *) m_data.abySkill, sizeof(m_data.abySkill));
	DBManager::instance().Query("UPDATE guild SET sp = %d, skill_point=%d, skill='%s' WHERE id = %u",
			m_data.power, m_data.skill_point, text, m_data.guild_id);
}

TGuildMember* CGuild::GetMember(DWORD pid)
{
	TGuildMemberContainer::iterator it = m_member.find(pid);
	if (it==m_member.end())
		return NULL;

	return &it->second;
}

DWORD CGuild::GetMemberPID(const std::string& strName)
{
	for ( TGuildMemberContainer::iterator iter = m_member.begin();
			iter != m_member.end(); iter++ )
	{
		if ( iter->second.name == strName ) return iter->first;
	}

	return 0;
}

void CGuild::__P2PUpdateGrade(SQLMsg* pmsg)
{
	if (pmsg->Get()->uiNumRows)
	{
		MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);
		
		int grade = 0;
		const char* name = row[1];
		int auth = 0;

		str_to_number(grade, row[0]);
		str_to_number(auth, row[2]);

		if (grade <= 0)
			return;

		grade--;

		// µî±Þ ¸íÄªÀÌ ÇöÀç¿Í ´Ù¸£´Ù¸é ¾÷µ¥ÀÌÆ®
		if (0 != strcmp(m_data.grade_array[grade].grade_name, name))
		{
			strlcpy(m_data.grade_array[grade].grade_name, name, sizeof(m_data.grade_array[grade].grade_name));

			TPacketGCGuild pack;
			
			pack.header = HEADER_GC_GUILD;
			pack.size = sizeof(pack);
			pack.subheader = GUILD_SUBHEADER_GC_GRADE_NAME;

			TOneGradeNamePacket pack2;

			pack.size += sizeof(pack2);
			pack2.grade = grade + 1;
			strlcpy(pack2.grade_name, name, sizeof(pack2.grade_name));

			TEMP_BUFFER buf;

			buf.write(&pack,sizeof(pack));
			buf.write(&pack2,sizeof(pack2));

			for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it!=m_memberOnline.end(); ++it)
			{
				LPDESC d = (*it)->GetDesc();

				if (d)
					d->Packet(buf.read_peek(), buf.size());
			}
		}

		if (m_data.grade_array[grade].auth_flag != auth)
		{
			m_data.grade_array[grade].auth_flag = auth;

			TPacketGCGuild pack;
			pack.header = HEADER_GC_GUILD;
			pack.size = sizeof(pack);
			pack.subheader = GUILD_SUBHEADER_GC_GRADE_AUTH;

			TOneGradeAuthPacket pack2;
			pack.size+=sizeof(pack2);
			pack2.grade = grade+1;
			pack2.auth = auth;

			TEMP_BUFFER buf;
			buf.write(&pack,sizeof(pack));
			buf.write(&pack2,sizeof(pack2));

			for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it!=m_memberOnline.end(); ++it)
			{
				LPDESC d = (*it)->GetDesc();
				if (d)
				{
					d->Packet(buf.read_peek(), buf.size());
				}
			}
		}
	}
}

void CGuild::P2PChangeGrade(BYTE grade)
{
	DBManager::instance().FuncQuery(std::bind1st(std::mem_fun(&CGuild::__P2PUpdateGrade),this),
			"SELECT grade, name, auth+0 FROM guild_grade WHERE guild_id = %u and grade = %d", m_data.guild_id, grade);
}

namespace 
{
	struct FSendChangeGrade
	{
		BYTE grade;
		TPacketGuild p;

		FSendChangeGrade(DWORD guild_id, BYTE grade) : grade(grade)
		{
			p.dwGuild = guild_id;
			p.dwInfo = grade;
		}

		void operator()()
		{
			db_clientdesc->DBPacket(HEADER_GD_GUILD_CHANGE_GRADE, 0, &p, sizeof(p));
		}
	};
}

void CGuild::ChangeGradeName(BYTE grade, const char* grade_name)
{
	if (grade == 1)
		return;

	if (grade < 1 || grade > 15)
	{
		sys_err("Wrong guild grade value %d", grade);
		return;
	}

	if (strlen(grade_name) > GUILD_NAME_MAX_LEN)
		return;

	if (!*grade_name)
		return;

	char text[GUILD_NAME_MAX_LEN * 2 + 1];

	DBManager::instance().EscapeString(text, sizeof(text), grade_name, strlen(grade_name));
	DBManager::instance().FuncAfterQuery(FSendChangeGrade(GetID(), grade), "UPDATE guild_grade SET name = '%s' where guild_id = %u and grade = %d", text, m_data.guild_id, grade);

	grade--;
	strlcpy(m_data.grade_array[grade].grade_name, grade_name, sizeof(m_data.grade_array[grade].grade_name));

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack);
	pack.subheader = GUILD_SUBHEADER_GC_GRADE_NAME;

	TOneGradeNamePacket pack2;
	pack.size+=sizeof(pack2);
	pack2.grade = grade+1;
	strlcpy(pack2.grade_name,grade_name, sizeof(pack2.grade_name));

	TEMP_BUFFER buf;
	buf.write(&pack,sizeof(pack));
	buf.write(&pack2,sizeof(pack2));

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it!=m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();

		if (d)
			d->Packet(buf.read_peek(), buf.size());
	}
}

void CGuild::ChangeGradeAuth(BYTE grade, BYTE auth)
{
	if (grade == 1)
		return;

	if (grade < 1 || grade > 15)
	{
		sys_err("Wrong guild grade value %d", grade);
		return;
	}

	DBManager::instance().FuncAfterQuery(FSendChangeGrade(GetID(),grade), "UPDATE guild_grade SET auth = %d where guild_id = %u and grade = %d", auth, m_data.guild_id, grade);

	grade--;

	m_data.grade_array[grade].auth_flag=auth;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack);
	pack.subheader = GUILD_SUBHEADER_GC_GRADE_AUTH;

	TOneGradeAuthPacket pack2;
	pack.size += sizeof(pack2);
	pack2.grade = grade + 1;
	pack2.auth = auth;

	TEMP_BUFFER buf;
	buf.write(&pack, sizeof(pack));
	buf.write(&pack2, sizeof(pack2));

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();

		if (d)
			d->Packet(buf.read_peek(), buf.size());
	}
}

void CGuild::SendGuildInfoPacket(LPCHARACTER ch)
{
	LPDESC d = ch->GetDesc();

	if (!d)
		return;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(TPacketGCGuild) + sizeof(TPacketGCGuildInfo);
	pack.subheader = GUILD_SUBHEADER_GC_INFO;

	TPacketGCGuildInfo pack_sub;

	memset(&pack_sub, 0, sizeof(TPacketGCGuildInfo));
	pack_sub.member_count = GetMemberCount(); 
	pack_sub.max_member_count = GetMaxMemberCount();
	pack_sub.guild_id = m_data.guild_id;
	pack_sub.master_pid = m_data.master_pid;
	pack_sub.exp	= m_data.exp;
	pack_sub.level	= m_data.level;
	strlcpy(pack_sub.name, m_data.name, sizeof(pack_sub.name));
	pack_sub.gold	= m_data.gold;
	pack_sub.has_land	= HasLand();

	sys_log(0, "GMC guild_name %s", m_data.name);
	sys_log(0, "GMC master %d", m_data.master_pid);

	d->BufferedPacket(&pack, sizeof(TPacketGCGuild));
	d->Packet(&pack_sub, sizeof(TPacketGCGuildInfo));
}

bool CGuild::OfferExp(LPCHARACTER ch, int amount)
{
	TGuildMemberContainer::iterator cit = m_member.find(ch->GetPlayerID());

	if (cit == m_member.end())
		return false;

	if (m_data.exp+amount < m_data.exp)
		return false;

	if (amount < 0)
		return false;

	if (ch->GetExp() < (DWORD) amount)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Experienþã insuficientã.");
		return false;
	}

	if (ch->GetExp() - (DWORD) amount > ch->GetExp())
	{
		sys_err("Wrong guild offer amount %d by %s[%u]", amount, ch->GetName(), ch->GetPlayerID());
		return false;
	}

	if (ch->block_exp)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "Nu poþi dona experienþã cât timp aceasta este blocatã.");
		return false;
	}

	ch->PointChange(POINT_EXP, -amount);

	TPacketGuildExpUpdate guild_exp;
	guild_exp.guild_id = GetID();
	guild_exp.amount = amount / 100;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_EXP_UPDATE, 0, &guild_exp, sizeof(guild_exp));
	GuildPointChange(POINT_EXP, amount / 100, true);

	cit->second.offer_exp += amount / 100;
	cit->second._dummy = 0;

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();
		if (d)
		{
			pack.subheader = GUILD_SUBHEADER_GC_LIST;
			pack.size = sizeof(pack) + 13;
			d->BufferedPacket(&pack, sizeof(pack));
			d->Packet(&(cit->second), sizeof(DWORD) * 3 + 1);
		}
	}

	SaveMember(ch->GetPlayerID());

	TPacketGuildChangeMemberData gd_guild;

	gd_guild.guild_id = GetID();
	gd_guild.pid = ch->GetPlayerID();
	gd_guild.offer = cit->second.offer_exp;
	gd_guild.level = ch->GetLevel();
	gd_guild.grade = cit->second.grade;

	db_clientdesc->DBPacket(HEADER_GD_GUILD_CHANGE_MEMBER_DATA, 0, &gd_guild, sizeof(gd_guild));
	return true;
}

void CGuild::Disband()
{
	sys_log(0, "GUILD: Disband %s:%u", GetName(), GetID());

	//building::CLand* pLand = building::CManager::instance().FindLandByGuild(GetID());
	//if (pLand)
	//pLand->SetOwner(0);

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPCHARACTER ch = *it;
		ch->SetGuild(NULL);
		SendOnlineRemoveOnePacket(ch->GetPlayerID());
	}

	for (TGuildMemberContainer::iterator it = m_member.begin(); it != m_member.end(); ++it)
		CGuildManager::instance().Unlink(it->first);
}

void CGuild::RequestDisband(DWORD pid)
{
	if (m_data.master_pid != pid)
		return;

	TPacketGuild gd_guild;
	gd_guild.dwGuild = GetID();
	gd_guild.dwInfo = 0;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_DISBAND, 0, &gd_guild, sizeof(TPacketGuild));

	// LAND_CLEAR
	building::CManager::instance().ClearLandByGuildID(GetID());
	// END_LAND_CLEAR
}

void CGuild::AddComment(LPCHARACTER ch, const std::string& str)
{
	if (str.length() > GUILD_COMMENT_MAX_LEN || str.length() == 0)
		return;

    if (m_guildPostCommentPulse > thecore_pulse()) 
    {
        int deltaInSeconds = ((m_guildPostCommentPulse / PASSES_PER_SEC(1)) - (thecore_pulse() / PASSES_PER_SEC(1)));
        int minutes = deltaInSeconds / 60;
        int seconds = (deltaInSeconds - (minutes * 60));
 
		ch->ChatPacket(CHAT_TYPE_INFO, "Poti adauga un alt comentariu in %02d minute si %02d secunde.", minutes, seconds);
        return;
    }

	char text[GUILD_COMMENT_MAX_LEN * 2 + 1];
	DBManager::instance().EscapeString(text, sizeof(text), str.c_str(), str.length());

	DBManager::instance().FuncAfterQuery(void_bind(std::bind1st(std::mem_fun(&CGuild::RefreshCommentForce),this),ch->GetPlayerID()),
			"INSERT INTO guild_comment (guild_id, name, notice, content, time) VALUES(%u, '%s', %d, '%s', NOW())", 
			m_data.guild_id, ch->GetName(), (str[0] == '!') ? 1 : 0, text);

	m_guildPostCommentPulse = thecore_pulse() + PASSES_PER_SEC(10*60);
}

void CGuild::DeleteComment(LPCHARACTER ch, DWORD comment_id)
{
	SQLMsg * pmsg;

	if (GetMember(ch->GetPlayerID())->grade == GUILD_LEADER_GRADE)
		pmsg = DBManager::instance().DirectQuery("DELETE FROM guild_comment WHERE id = %u AND guild_id = %u", comment_id, m_data.guild_id);
	else
		pmsg = DBManager::instance().DirectQuery("DELETE FROM guild_comment WHERE id = %u AND guild_id = %u AND name = '%s'", comment_id, m_data.guild_id, ch->GetName());

	if (pmsg->Get()->uiAffectedRows == 0 || pmsg->Get()->uiAffectedRows == (uint32_t)-1)
		ch->ChatPacket(CHAT_TYPE_INFO, "Cibirichi41");
	else
		RefreshCommentForce(ch->GetPlayerID());

	M2_DELETE(pmsg);
}

void CGuild::RefreshComment(LPCHARACTER ch)
{
	RefreshCommentForce(ch->GetPlayerID());
}

void CGuild::RefreshCommentForce(DWORD player_id)
{
	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(player_id);
	if (!ch)
		return;

	std::auto_ptr<SQLMsg> pmsg (DBManager::instance().DirectQuery("SELECT id, name, content FROM guild_comment WHERE guild_id = %u ORDER BY notice DESC, id DESC LIMIT %d", m_data.guild_id, GUILD_COMMENT_MAX_COUNT));

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+1;
	pack.subheader = GUILD_SUBHEADER_GC_COMMENTS;

	BYTE count = pmsg->Get()->uiNumRows;

	LPDESC d = ch->GetDesc();

	if (!d) 
		return;

	pack.size += (sizeof(DWORD)+CHARACTER_NAME_MAX_LEN+1+GUILD_COMMENT_MAX_LEN+1)*(WORD)count;
	d->BufferedPacket(&pack,sizeof(pack));
	d->BufferedPacket(&count, 1);
	char szName[CHARACTER_NAME_MAX_LEN + 1];
	char szContent[GUILD_COMMENT_MAX_LEN + 1];
	memset(szName, 0, sizeof(szName));
	memset(szContent, 0, sizeof(szContent));

	for (uint i = 0; i < pmsg->Get()->uiNumRows; i++)
	{
		MYSQL_ROW row = mysql_fetch_row(pmsg->Get()->pSQLResult);
		DWORD id = strtoul(row[0], NULL, 10);

		strlcpy(szName, row[1], sizeof(szName));
		strlcpy(szContent, row[2], sizeof(szContent));

		d->BufferedPacket(&id, sizeof(id));
		d->BufferedPacket(szName, sizeof(szName));

		if (i == pmsg->Get()->uiNumRows - 1)
			d->Packet(szContent, sizeof(szContent)); // ¸¶Áö¸· ÁÙÀÌ¸é º¸³»±â
		else
			d->BufferedPacket(szContent, sizeof(szContent));
	}
}

bool CGuild::ChangeMemberGeneral(DWORD pid, BYTE is_general)
{
	if (is_general && GetGeneralCount() >= GetMaxGeneralCount())
		return false;

	TGuildMemberContainer::iterator it = m_member.find(pid);
	if (it == m_member.end())
	{
		return true;
	}

	is_general = is_general?1:0;

	if (it->second.is_general == is_general)
		return true;

	if (is_general)
		++m_general_count;
	else
		--m_general_count;

	it->second.is_general = is_general;

	TGuildMemberOnlineContainer::iterator itOnline = m_memberOnline.begin();

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+5;
	pack.subheader = GUILD_SUBHEADER_GC_CHANGE_MEMBER_GENERAL;

	while (itOnline != m_memberOnline.end())
	{
		LPDESC d = (*(itOnline++))->GetDesc();

		if (!d)
			continue;

		d->BufferedPacket(&pack, sizeof(pack));
		d->BufferedPacket(&pid, sizeof(pid));
		d->Packet(&is_general, sizeof(is_general));
	}

	SaveMember(pid);
	return true;
}

void CGuild::ChangeMemberGrade(DWORD pid, BYTE grade)
{
	if (grade == 1)
		return;

	TGuildMemberContainer::iterator it = m_member.find(pid);

	if (it == m_member.end())
		return;

	it->second.grade = grade;

	TGuildMemberOnlineContainer::iterator itOnline = m_memberOnline.begin();

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	pack.size = sizeof(pack)+5;
	pack.subheader = GUILD_SUBHEADER_GC_CHANGE_MEMBER_GRADE;

	while (itOnline != m_memberOnline.end())
	{
		LPDESC d = (*(itOnline++))->GetDesc();

		if (!d)
			continue;

		d->BufferedPacket(&pack, sizeof(pack));
		d->BufferedPacket(&pid, sizeof(pid));
		d->Packet(&grade, sizeof(grade));
	}

	SaveMember(pid);

	TPacketGuildChangeMemberData gd_guild;

	gd_guild.guild_id = GetID();
	gd_guild.pid = pid;
	gd_guild.offer = it->second.offer_exp;
	gd_guild.level = it->second.level;
	gd_guild.grade = grade;

	db_clientdesc->DBPacket(HEADER_GD_GUILD_CHANGE_MEMBER_DATA, 0, &gd_guild, sizeof(gd_guild));
}

void CGuild::SkillLevelUp(DWORD dwVnum)
{
	DWORD dwRealVnum = dwVnum - GUILD_SKILL_START;

	if (dwRealVnum >= GUILD_SKILL_COUNT)
		return;

	CSkillProto* pkSk = CSkillManager::instance().Get(dwVnum);

	if (!pkSk)
	{
		sys_err("There is no such guild skill by number %u", dwVnum);
		return;
	}

	if (m_data.abySkill[dwRealVnum] >= pkSk->bMaxLevel)
		return;

	if (m_data.skill_point <= 0)
		return;
	m_data.skill_point --;

	m_data.abySkill[dwRealVnum] ++;

	ComputeGuildPoints();
	SaveSkill();
	SendDBSkillUpdate();

	/*switch (dwVnum)
	  {
	  case GUILD_SKILL_GAHO:
	  {
	  TGuildMemberOnlineContainer::iterator it;

	  for (it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	  (*it)->PointChange(POINT_DEF_GRADE, 1);
	  }
	  break;
	  case GUILD_SKILL_HIM:
	  {
	  TGuildMemberOnlineContainer::iterator it;

	  for (it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	  (*it)->PointChange(POINT_ATT_GRADE, 1);
	  }
	  break;
	  }*/

	for_each(m_memberOnline.begin(), m_memberOnline.end(), std::bind1st(std::mem_fun_ref(&CGuild::SendSkillInfoPacket),*this));

	sys_log(0, "Guild SkillUp: %s %d level %d type %u", GetName(), pkSk->dwVnum, m_data.abySkill[dwRealVnum], pkSk->dwType);
}

void CGuild::UseSkill(DWORD dwVnum, LPCHARACTER ch, DWORD pid)
{
	LPCHARACTER victim = NULL;

	if (!GetMember(ch->GetPlayerID()) || !HasGradeAuth(GetMember(ch->GetPlayerID())->grade, GUILD_AUTH_USE_SKILL))
		return;

	sys_log(0,"GUILD_USE_SKILL : cname(%s), skill(%d)", ch ? ch->GetName() : "", dwVnum);

	DWORD dwRealVnum = dwVnum - GUILD_SKILL_START;

	if (!ch->CanMove())
		return;

	if (dwRealVnum >= GUILD_SKILL_COUNT)
		return;

	CSkillProto* pkSk = CSkillManager::instance().Get(dwVnum);

	if (!pkSk)
	{
		sys_err("There is no such guild skill by number %u", dwVnum);
		return;
	}

	if (m_data.abySkill[dwRealVnum] == 0)
		return;

	if ((pkSk->dwFlag & SKILL_FLAG_SELFONLY))
	{
		// ÀÌ¹Ì °É·Á ÀÖÀ¸¹Ç·Î »ç¿ëÇÏÁö ¾ÊÀ½.
		if (ch->FindAffect(pkSk->dwVnum))
			return;

		victim = ch;
	}

	if (ch->IsAffectFlag(AFF_REVIVE_INVISIBLE))
		ch->RemoveAffect(AFFECT_REVIVE_INVISIBLE);

	if (ch->IsAffectFlag(AFF_EUNHYUNG))
		ch->RemoveAffect(SKILL_EUNHYUNG);

	double k =1.0*m_data.abySkill[dwRealVnum]/pkSk->bMaxLevel;
	pkSk->kSPCostPoly.SetVar("k", k);
	int iNeededSP = (int) pkSk->kSPCostPoly.Eval();

	if (GetSP() < iNeededSP)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Mana insuficientã (actualã - %d, necesarã - %d)", GetSP(), iNeededSP);
		return;
	}

	pkSk->kCooldownPoly.SetVar("k", k);
	int iCooltime = (int) pkSk->kCooldownPoly.Eval();

	if (!abSkillUsable[dwRealVnum])
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Nu pot face asta.");
		return;
	}

	{
		TPacketGuildUseSkill p;
		p.dwGuild = GetID();
		p.dwSkillVnum = pkSk->dwVnum;
		p.dwCooltime = iCooltime;
		db_clientdesc->DBPacket(HEADER_GD_GUILD_USE_SKILL, 0, &p, sizeof(p));
	}
	abSkillUsable[dwRealVnum] = false;
	//abSkillUsed[dwRealVnum] = true;
	//adwSkillNextUseTime[dwRealVnum] = get_dword_time() + iCooltime * 1000;

	//PointChange(POINT_SP, -iNeededSP);
	//GuildPointChange(POINT_SP, -iNeededSP);

	switch (dwVnum)
	{
		case GUILD_SKILL_TELEPORT:
			// ÇöÀç ¼­¹ö¿¡ ÀÖ´Â »ç¶÷À» ¸ÕÀú ½Ãµµ.
			SendDBSkillUpdate(-iNeededSP);
			if ((victim = (CHARACTER_MANAGER::instance().FindByPID(pid))))
				ch->WarpSet(victim->GetX(), victim->GetY());
			else
			{
				if (m_memberP2POnline.find(pid) != m_memberP2POnline.end())
				{
					CCI * pcci = P2P_MANAGER::instance().FindByPID(pid);

					if (pcci->bChannel != g_bChannel)
						ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Jucãtorul este pe canalul %d în timp ce noi suntem pe canalul %d.", pcci->bChannel, g_bChannel);
					else
					{
						TPacketGGFindPosition p;
						p.header = HEADER_GG_FIND_POSITION;
						p.dwFromPID = ch->GetPlayerID();
						p.dwTargetPID = pid;
						pcci->pkDesc->Packet(&p, sizeof(TPacketGGFindPosition));
					}
				}
				else
					ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Jucãtorul nu este activ în acest moment.");
			}
			break;

			/*case GUILD_SKILL_ACCEL:
			  ch->RemoveAffect(dwVnum);
			  ch->AddAffect(dwVnum, POINT_MOV_SPEED, m_data.abySkill[dwRealVnum]*3, pkSk->dwAffectFlag, (int)pkSk->kDurationPoly.Eval(), 0, false);
			  ch->AddAffect(dwVnum, POINT_ATT_SPEED, m_data.abySkill[dwRealVnum]*3, pkSk->dwAffectFlag, (int)pkSk->kDurationPoly.Eval(), 0, false);
			  break;*/

		default:
			{
				if (!UnderAnyWar())
				{
					ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Puteþi face asta numai în timpul unui rãzboi.");
					return;
				}

				SendDBSkillUpdate(-iNeededSP);

				for (itertype(m_memberOnline) it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
				{
					LPCHARACTER victim = *it;
					victim->RemoveAffect(dwVnum);
					ch->ComputeSkill(dwVnum, victim, m_data.abySkill[dwRealVnum]);
				}
			}
			break;
			/*if (!victim)
			  return;

			  ch->ComputeSkill(dwVnum, victim, m_data.abySkill[dwRealVnum]);*/
	}
}

void CGuild::SendSkillInfoPacket(LPCHARACTER ch) const
{
	LPDESC d = ch->GetDesc();

	if (!d)
		return;

	TPacketGCGuild pack;

	pack.header		= HEADER_GC_GUILD;
	pack.size		= sizeof(pack) + 6 + GUILD_SKILL_COUNT;
	pack.subheader	= GUILD_SUBHEADER_GC_SKILL_INFO;

	d->BufferedPacket(&pack, sizeof(pack));
	d->BufferedPacket(&m_data.skill_point,	1);
	d->BufferedPacket(&m_data.abySkill,		GUILD_SKILL_COUNT);
	d->BufferedPacket(&m_data.power,		2);
	d->Packet(&m_data.max_power,	2);
}

void CGuild::ComputeGuildPoints()
{
	m_data.max_power = GUILD_BASE_POWER + (m_data.level-1) * GUILD_POWER_PER_LEVEL;

	m_data.power = MINMAX(0, m_data.power, m_data.max_power);
}

int CGuild::GetSkillLevel(DWORD vnum)
{
	DWORD dwRealVnum = vnum - GUILD_SKILL_START;

	if (dwRealVnum >= GUILD_SKILL_COUNT)
		return 0;

	return m_data.abySkill[dwRealVnum];
}

/*void CGuild::GuildUpdateAffect(LPCHARACTER ch)
  {
  if (GetSkillLevel(GUILD_SKILL_GAHO))
  ch->PointChange(POINT_DEF_GRADE, GetSkillLevel(GUILD_SKILL_GAHO));

  if (GetSkillLevel(GUILD_SKILL_HIM))
  ch->PointChange(POINT_ATT_GRADE, GetSkillLevel(GUILD_SKILL_HIM));
  }*/

/*void CGuild::GuildRemoveAffect(LPCHARACTER ch)
  {
  if (GetSkillLevel(GUILD_SKILL_GAHO))
  ch->PointChange(POINT_DEF_GRADE, -(int) GetSkillLevel(GUILD_SKILL_GAHO));

  if (GetSkillLevel(GUILD_SKILL_HIM))
  ch->PointChange(POINT_ATT_GRADE, -(int) GetSkillLevel(GUILD_SKILL_HIM));
  }*/

void CGuild::UpdateSkill(BYTE skill_point, BYTE* skill_levels)
{
	//int iDefMoreBonus = 0;
	//int iAttMoreBonus = 0;

	m_data.skill_point = skill_point;
	/*if (skill_levels[GUILD_SKILL_GAHO - GUILD_SKILL_START]!=GetSkillLevel(GUILD_SKILL_GAHO))
	  {
	  iDefMoreBonus = skill_levels[GUILD_SKILL_GAHO - GUILD_SKILL_START]-GetSkillLevel(GUILD_SKILL_GAHO);
	  }
	  if (skill_levels[GUILD_SKILL_HIM - GUILD_SKILL_START]!=GetSkillLevel(GUILD_SKILL_HIM))
	  {
	  iAttMoreBonus = skill_levels[GUILD_SKILL_HIM  - GUILD_SKILL_START]-GetSkillLevel(GUILD_SKILL_HIM);
	  }

	  if (iDefMoreBonus || iAttMoreBonus)
	  {
	  for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	  {
	  (*it)->PointChange(POINT_ATT_GRADE, iAttMoreBonus);
	  (*it)->PointChange(POINT_DEF_GRADE, iDefMoreBonus);
	  }
	  }*/

	thecore_memcpy(m_data.abySkill, skill_levels, sizeof(BYTE) * GUILD_SKILL_COUNT);
	ComputeGuildPoints();
}

static DWORD __guild_levelup_exp(int level)
{
	return guild_exp_table[level];
}

void CGuild::GuildPointChange(BYTE type, int amount, bool save)
{
	switch (type)
	{
		case POINT_SP:
			m_data.power += amount;

			m_data.power = MINMAX(0, m_data.power, m_data.max_power);

			if (save)
			{
				SaveSkill();
			}

			for_each(m_memberOnline.begin(), m_memberOnline.end(), std::bind1st(std::mem_fun_ref(&CGuild::SendSkillInfoPacket),*this));
			break;

		case POINT_EXP:
			if (amount < 0 && m_data.exp < (DWORD) - amount)
			{
				m_data.exp = 0;
			}
			else
			{
				m_data.exp += amount;

				while (m_data.exp >= __guild_levelup_exp(m_data.level))
				{

					if (m_data.level < GUILD_MAX_LEVEL)
					{
						m_data.exp -= __guild_levelup_exp(m_data.level);
						++m_data.level;
						++m_data.skill_point;

						if (m_data.level > GUILD_MAX_LEVEL)
							m_data.level = GUILD_MAX_LEVEL;

						ComputeGuildPoints();
						GuildPointChange(POINT_SP, m_data.max_power-m_data.power);

						if (save)
							ChangeLadderPoint(GUILD_LADDER_POINT_PER_LEVEL);

						// NOTIFY_GUILD_EXP_CHANGE
						for_each(m_memberOnline.begin(), m_memberOnline.end(), std::bind1st(std::mem_fun(&CGuild::SendGuildInfoPacket), this));
						// END_OF_NOTIFY_GUILD_EXP_CHANGE
					}

					if (m_data.level == GUILD_MAX_LEVEL)
					{
						m_data.exp = 0;
					}
				}
			}

			TPacketGCGuild pack;
			pack.header = HEADER_GC_GUILD;
			pack.size = sizeof(pack)+5;
			pack.subheader = GUILD_SUBHEADER_GC_CHANGE_EXP;

			TEMP_BUFFER buf;
			buf.write(&pack,sizeof(pack));
			buf.write(&m_data.level,1);
			buf.write(&m_data.exp,4);

			for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
			{
				LPDESC d = (*it)->GetDesc();

				if (d)
					d->Packet(buf.read_peek(), buf.size());
			}

			if (save)
				SaveLevel();

			break;
	}
}

void CGuild::SkillRecharge()
{
	//GuildPointChange(POINT_SP, m_data.max_power / 2);
	//GuildPointChange(POINT_SP, 10);
}

void CGuild::SaveMember(DWORD pid)
{
	TGuildMemberContainer::iterator it = m_member.find(pid);

	if (it == m_member.end())
		return;

	DBManager::instance().Query(
			"UPDATE guild_member SET grade = %d, offer = %u, is_general = %d WHERE pid = %u and guild_id = %u",
			it->second.grade, it->second.offer_exp, it->second.is_general, pid, m_data.guild_id);
}

void CGuild::LevelChange(DWORD pid, BYTE level)
{
	TGuildMemberContainer::iterator cit = m_member.find(pid);

	if (cit == m_member.end())
		return;

	cit->second.level = level;

	TPacketGuildChangeMemberData gd_guild;

	gd_guild.guild_id = GetID();
	gd_guild.pid = pid;
	gd_guild.offer = cit->second.offer_exp;
	gd_guild.grade = cit->second.grade;
	gd_guild.level = level;

	db_clientdesc->DBPacket(HEADER_GD_GUILD_CHANGE_MEMBER_DATA, 0, &gd_guild, sizeof(gd_guild));

	TPacketGCGuild pack;
	pack.header = HEADER_GC_GUILD;
	cit->second._dummy = 0;

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();

		if (d)
		{
			pack.subheader = GUILD_SUBHEADER_GC_LIST;
			pack.size = sizeof(pack) + 13;
			d->BufferedPacket(&pack, sizeof(pack));
			d->Packet(&(cit->second), sizeof(DWORD) * 3 + 1);
		}
	}
}

void CGuild::ChangeMemberData(DWORD pid, DWORD offer, BYTE level, BYTE grade)
{
	TGuildMemberContainer::iterator cit = m_member.find(pid);

	if (cit == m_member.end())
		return;

	cit->second.offer_exp = offer;
	cit->second.level = level;
	cit->second.grade = grade;
	cit->second._dummy = 0;

	TPacketGCGuild pack;
	memset(&pack, 0, sizeof(pack));
	pack.header = HEADER_GC_GUILD;

	for (TGuildMemberOnlineContainer::iterator it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPDESC d = (*it)->GetDesc();
		if (d)
		{
			pack.subheader = GUILD_SUBHEADER_GC_LIST;
			pack.size = sizeof(pack) + 13;
			d->BufferedPacket(&pack, sizeof(pack));
			d->Packet(&(cit->second), sizeof(DWORD) * 3 + 1);
		}
	}
}

namespace
{
	struct FGuildChat
	{
		const char* c_pszText;

		FGuildChat(const char* c_pszText)
			: c_pszText(c_pszText)
			{}

		void operator()(LPCHARACTER ch)
		{
			ch->ChatPacket(CHAT_TYPE_GUILD, "%s", c_pszText);
		}
	};
}

void CGuild::P2PChat(const char* c_pszText)
{
	std::for_each(m_memberOnline.begin(), m_memberOnline.end(), FGuildChat(c_pszText));
}

void CGuild::Chat(const char* c_pszText)
{
	std::for_each(m_memberOnline.begin(), m_memberOnline.end(), FGuildChat(c_pszText));

	TPacketGGGuild p1;
	TPacketGGGuildChat p2;

	p1.bHeader = HEADER_GG_GUILD;
	p1.bSubHeader = GUILD_SUBHEADER_GG_CHAT;
	p1.dwGuild = GetID();
	strlcpy(p2.szText, c_pszText, sizeof(p2.szText));

	P2P_MANAGER::instance().Send(&p1, sizeof(TPacketGGGuild));
	P2P_MANAGER::instance().Send(&p2, sizeof(TPacketGGGuildChat));
}

LPCHARACTER CGuild::GetMasterCharacter()
{ 
	return CHARACTER_MANAGER::instance().FindByPID(GetMasterPID()); 
}

void CGuild::Packet(const void* buf, int size)
{
	for (itertype(m_memberOnline) it = m_memberOnline.begin(); it!=m_memberOnline.end();++it)
	{
		LPDESC d = (*it)->GetDesc();

		if (d)
			d->Packet(buf, size);
	}
}

int CGuild::GetTotalLevel() const
{
	int total = 0;

	for (itertype(m_member) it = m_member.begin(); it != m_member.end(); ++it)
	{
		total += it->second.level;
	}

	return total;
}

bool CGuild::ChargeSP(LPCHARACTER ch, int iSP)
{
	int gold = iSP * 100;

	if (gold < iSP || ch->GetGold() < gold)
		return false;

	int iRemainSP = m_data.max_power - m_data.power;

	if (iSP > iRemainSP)
	{
		iSP = iRemainSP;
		gold = iSP * 100;
	}

	ch->PointChange(POINT_GOLD, -gold);
	DBManager::instance().SendMoneyLog(MONEY_LOG_GUILD, 1, -gold);

	SendDBSkillUpdate(iSP);
	ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> %d PM a fost folosit pentru a încãrcare.", iSP);
	return true;
}

void CGuild::SkillUsableChange(DWORD dwSkillVnum, bool bUsable)
{
	DWORD dwRealVnum = dwSkillVnum - GUILD_SKILL_START;

	if (dwRealVnum >= GUILD_SKILL_COUNT)
		return; 

	abSkillUsable[dwRealVnum] = bUsable;

	// GUILD_SKILL_COOLTIME_BUG_FIX
	sys_log(0, "CGuild::SkillUsableChange(guild=%s, skill=%d, usable=%d)", GetName(), dwSkillVnum, bUsable);
	// END_OF_GUILD_SKILL_COOLTIME_BUG_FIX
}

int CGuild::GetMaxMemberCount()
{
	return 32 + 2 * (m_data.level - 1);
}

void CGuild::AdvanceLevel(int iLevel)
{
	if (m_data.level == iLevel)
		return;

	m_data.level = MIN(GUILD_MAX_LEVEL, iLevel);
}

void CGuild::RequestDepositMoney(LPCHARACTER ch, int iGold)
{
	if (!ch->CanDeposit())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "Cibirichi44");
		return;
	}

	if (ch->GetGold() < iGold)
		return;


	ch->PointChange(POINT_GOLD, -iGold);

	TPacketGDGuildMoney p;
	p.dwGuild = GetID();
	p.iGold = iGold;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_DEPOSIT_MONEY, 0, &p, sizeof(p));

	char buf[64+1];
	snprintf(buf, sizeof(buf), "%u %s", GetID(), GetName());
	LogManager::instance().CharLog(ch, iGold, "GUILD_DEPOSIT", buf);

	ch->UpdateDepositPulse();
	sys_log(0, "GUILD: DEPOSIT %s:%u player %s[%u] gold %d", GetName(), GetID(), ch->GetName(), ch->GetPlayerID(), iGold);
}

void CGuild::RequestWithdrawMoney(LPCHARACTER ch, int iGold)
{
	if (!ch->CanDeposit())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "Cibirichi44");
		return;
	}

	if (ch->GetPlayerID() != GetMasterPID())
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Doar liderul poate face asta.");
		return;
	}

	if (m_data.gold < iGold)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Yang insuficient.");
		return;
	}

	TPacketGDGuildMoney p;
	p.dwGuild = GetID();
	p.iGold = iGold;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_WITHDRAW_MONEY, 0, &p, sizeof(p));

	ch->UpdateDepositPulse();
}

void CGuild::RecvMoneyChange(int iGold)
{
	m_data.gold = iGold;

	TPacketGCGuild p;
	p.header = HEADER_GC_GUILD;
	p.size = sizeof(p) + sizeof(int);
	p.subheader = GUILD_SUBHEADER_GC_MONEY_CHANGE;

	for (itertype(m_memberOnline) it = m_memberOnline.begin(); it != m_memberOnline.end(); ++it)
	{
		LPCHARACTER ch = *it;
		LPDESC d = ch->GetDesc();
		d->BufferedPacket(&p, sizeof(p));
		d->Packet(&iGold, sizeof(int));
	}
}

void CGuild::RecvWithdrawMoneyGive(int iChangeGold)
{
	LPCHARACTER ch = GetMasterCharacter();

	if (ch)
	{
		ch->PointChange(POINT_GOLD, iChangeGold);
		sys_log(0, "GUILD: WITHDRAW %s:%u player %s[%u] gold %d", GetName(), GetID(), ch->GetName(), ch->GetPlayerID(), iChangeGold);
	}

	TPacketGDGuildMoneyWithdrawGiveReply p;
	p.dwGuild = GetID();
	p.iChangeGold = iChangeGold;
	p.bGiveSuccess = ch ? 1 : 0;
	db_clientdesc->DBPacket(HEADER_GD_GUILD_WITHDRAW_MONEY_GIVE_REPLY, 0, &p, sizeof(p));
}

bool CGuild::HasLand()
{
	return building::CManager::instance().FindLandByGuild(GetID()) != NULL;
}

// GUILD_JOIN_BUG_FIX
EVENTINFO(TInviteGuildEventInfo)
{
	DWORD	dwInviteePID;
	DWORD	dwGuildID;

	TInviteGuildEventInfo() : dwInviteePID(0), dwGuildID(0) { }
};

EVENTFUNC( GuildInviteEvent )
{
	TInviteGuildEventInfo *pInfo = dynamic_cast<TInviteGuildEventInfo*>( event->info );

	if ( pInfo == NULL )
	{
		sys_err( "GuildInviteEvent> <Factor> Null pointer" );
		return 0;
	}

	CGuild* pGuild = CGuildManager::instance().FindGuild( pInfo->dwGuildID );

	if ( pGuild ) 
	{
		sys_log( 0, "GuildInviteEvent %s", pGuild->GetName() );
		pGuild->InviteDeny( pInfo->dwInviteePID );
	}

	return 0;
}

void CGuild::Invite(LPCHARACTER pchInviter, LPCHARACTER pchInvitee)
{
	if (quest::CQuestManager::instance().GetPCForce(pchInviter->GetPlayerID())->IsRunning() == true)
	{
	    pchInviter->ChatPacket(CHAT_TYPE_INFO, "Cibirichi41");
	    return;
	}
	
	if (quest::CQuestManager::instance().GetPCForce(pchInvitee->GetPlayerID())->IsRunning() == true)
		return;

	if (pchInvitee->IsBlockMode(BLOCK_GUILD_INVITE)) 
	{
		pchInviter->ChatPacket(CHAT_TYPE_INFO, "Jucãtorul nu doreºte o astfel de invitaþie.");
		return;
	} 
	else if (!HasGradeAuth(GetMember(pchInviter->GetPlayerID())->grade, GUILD_AUTH_ADD_MEMBER)) 
	{
		pchInviter->ChatPacket(CHAT_TYPE_INFO, "Permisiuni insuficiente.");
		return;
	} 
	else if (pchInvitee->GetEmpire() != pchInviter->GetEmpire()) 
	{
		pchInviter->ChatPacket(CHAT_TYPE_INFO, "Jucãtorul aparþine altui regat.");
		return;
	}
	else if (pchInvitee->GetLevel() < 15)
	{
		pchInviter->ChatPacket(CHAT_TYPE_INFO, "Jucãtorul nu are nivelul minim 15.");
		return;
	}

	GuildJoinErrCode errcode = VerifyGuildJoinableCondition(pchInvitee);
	switch (errcode) 
	{
		case GERR_NONE: break;
		case GERR_ALREADYJOIN:	pchInviter->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Jucãtorul s-a alãturat deja breslei."); return;
		case GERR_GUILDISFULL:	pchInviter->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Limita maximã de membrii a fost atinsã."); return;
		case GERR_GUILD_IS_IN_WAR: pchInviter->ChatPacket( CHAT_TYPE_INFO, "<Breaslã> Jucãtorul nu se poate alãtura breslei pe timp de rãzboi."); return;
		case GERR_INVITE_LIMIT: pchInviter->ChatPacket( CHAT_TYPE_INFO, "<Breaslã> Jucãtorul nu a putut fi invitat."); return;

		default: sys_err( "ignore guild join error(%d)", errcode ); return;
	}

	if ( m_GuildInviteEventMap.end() != m_GuildInviteEventMap.find( pchInvitee->GetPlayerID() ) )
		return;

	TInviteGuildEventInfo* pInfo = AllocEventInfo<TInviteGuildEventInfo>();
	pInfo->dwInviteePID = pchInvitee->GetPlayerID();
	pInfo->dwGuildID = GetID();

	m_GuildInviteEventMap.insert(EventMap::value_type(pchInvitee->GetPlayerID(), event_create(GuildInviteEvent, pInfo, PASSES_PER_SEC(10))));

	DWORD gid = GetID();

	TPacketGCGuild p;
	p.header	= HEADER_GC_GUILD;
	p.size	= sizeof(p) + sizeof(DWORD) + GUILD_NAME_MAX_LEN + 1;
	p.subheader	= GUILD_SUBHEADER_GC_GUILD_INVITE;

	TEMP_BUFFER buf;
	buf.write( &p, sizeof(p) );
	buf.write( &gid, sizeof(DWORD) );
	buf.write( GetName(), GUILD_NAME_MAX_LEN + 1 );

	pchInvitee->GetDesc()->Packet( buf.read_peek(), buf.size() );
}

void CGuild::InviteAccept( LPCHARACTER pchInvitee )
{
	EventMap::iterator itFind = m_GuildInviteEventMap.find( pchInvitee->GetPlayerID() );
	if ( itFind == m_GuildInviteEventMap.end() ) 
	{
		sys_log( 0, "GuildInviteAccept from not invited character(invite guild: %s, invitee: %s)", GetName(), pchInvitee->GetName() );
		return;
	}

	event_cancel( &itFind->second );
	m_GuildInviteEventMap.erase( itFind );

	GuildJoinErrCode errcode = VerifyGuildJoinableCondition( pchInvitee );
	switch ( errcode ) 
	{
		case GERR_NONE: break;
		case GERR_ALREADYJOIN:	pchInvitee->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Jucãtorul s-a alãturat deja breslei."); return;
		case GERR_GUILDISFULL:	pchInvitee->ChatPacket(CHAT_TYPE_INFO, "<Breaslã> Limita maximã de membrii a fost atinsã."); return;
		case GERR_GUILD_IS_IN_WAR : pchInvitee->ChatPacket( CHAT_TYPE_INFO, "<Breaslã> Jucãtorul nu se poate alãtura breslei pe timp de rãzboi." ); return;
		case GERR_INVITE_LIMIT : pchInvitee->ChatPacket( CHAT_TYPE_INFO, "<Breaslã> Jucãtorul nu a putut fi invitat." ); return;

		default: sys_err( "ignore guild join error(%d)", errcode ); return;
	}

	RequestAddMember(pchInvitee, 15);
}

void CGuild::InviteDeny( DWORD dwPID )
{
	EventMap::iterator itFind = m_GuildInviteEventMap.find( dwPID );
	if ( itFind == m_GuildInviteEventMap.end() ) 
	{
		sys_log( 0, "GuildInviteDeny from not invited character(invite guild: %s, invitee PID: %d)", GetName(), dwPID );
		return;
	}

	event_cancel( &itFind->second );
	m_GuildInviteEventMap.erase( itFind );
}

CGuild::GuildJoinErrCode CGuild::VerifyGuildJoinableCondition( const LPCHARACTER pchInvitee )
{
	if ( pchInvitee->GetGuild() )
		return GERR_ALREADYJOIN;
	else if ( GetMemberCount() >= GetMaxMemberCount() )
		return GERR_GUILDISFULL;
	else if (UnderAnyWar())
		return GERR_GUILD_IS_IN_WAR;

	return GERR_NONE;
}
// END_OF_GUILD_JOIN_BUG_FIX

bool CGuild::ChangeMasterTo(DWORD dwPID)
{
	if ( GetMember(dwPID) == NULL ) return false;

	TPacketChangeGuildMaster p;
	p.dwGuildID = GetID();
	p.idFrom = GetMasterPID();
	p.idTo = dwPID;

	db_clientdesc->DBPacket(HEADER_GD_REQ_CHANGE_GUILD_MASTER, 0, &p, sizeof(p));

	return true;
}

void CGuild::SendGuildDataUpdateToAllMember(SQLMsg* pmsg)
{
	TGuildMemberOnlineContainer::iterator iter = m_memberOnline.begin();

	for (; iter != m_memberOnline.end(); iter++ )
	{
		SendGuildInfoPacket(*iter);
		SendAllGradePacket(*iter);
	}
}

