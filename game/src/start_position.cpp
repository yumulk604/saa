#include "stdafx.h"
#include "start_position.h"


char g_nation_name[4][32] =
{
	"",
	"Shinsoo",
	"Chunjo",
	"Jinno",
};

long g_start_map[4] =
{
	0,	// reserved
	1,	// �ż���
	21,	// õ����
	41	// ���뱹
};

DWORD g_start_position[4][2] =
{
	{      0,      0 },	// reserved
	{ 469300, 964200 },	// �ż���
	{  55700, 157900 },	// õ����
	{ 969600, 278400 }	// ���뱹
};


DWORD arena_return_position[4][2] =
{
	{       0,  0       },
	{   347600, 882700  },
	{   138600, 236600  },
	{   857200, 251800  }
};


DWORD g_create_position[4][2] = 
{
	{		0,		0 },
	{ 459800, 953900 },
	{ 52070, 166600 },
	{ 957300, 255200 }
};
