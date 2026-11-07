#include "map.h"
#include "actor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 *s_pack;
static const Player *s_player;
static size_t s_size;

static GameMap s_maps[MAX_MAPS];
static int s_mapCount;
static Door s_doors[MAX_DOORS];
static int s_doorCount;
static Npc s_npcs[MAX_NPCS];
static int s_npcCount;

void map_set_player(const Player *p) { s_player = p; }

static bool npc_dead(unsigned index)
{
	if (!s_player || index >= (unsigned)s_npcCount) return false;
	unsigned id = s_npcs[index].npcId;
	return id != 125 && id < 128 && (s_player->deadNpc[id / 8] & (1u << (id % 8)));
}

bool map_npc_dead_index(unsigned index)
{
	return npc_dead(index);
}

static u8 s_startMap, s_startX, s_startY;

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void map_pack_free(void)
{
	free(s_pack);
	s_pack = NULL;
	s_size = 0;
	s_mapCount = s_doorCount = s_npcCount = 0;
}

bool map_pack_load(const char *romfs_path)
{
	FILE *f = fopen(romfs_path, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 32)
	{
		fclose(f);
		return false;
	}

	u8 *pack = (u8 *)malloc((size_t)sz);
	if (!pack)
	{
		fclose(f);
		return false;
	}
	if (fread(pack, 1, (size_t)sz, f) != (size_t)sz)
	{
		fclose(f);
		free(pack);
		return false;
	}
	fclose(f);

	bool ok = false;
	do
	{
		if (memcmp(pack, "GM3D", 4) != 0)
			break;
		if (rd16(pack + 4) != 1)
			break;

		int mapCount = rd16(pack + 6);
		int doorCount = rd16(pack + 8);
		int npcCount = rd16(pack + 10);
		if (mapCount < 1 || mapCount > MAX_MAPS) break;
		if (doorCount < 0 || doorCount > MAX_DOORS) break;
		if (npcCount < 0 || npcCount > MAX_NPCS) break;

		u32 offMaps = rd32(pack + 12);
		u32 offDoors = rd32(pack + 16);
		u32 offNpcs = rd32(pack + 20);
		u32 offCells = rd32(pack + 24);
		size_t size = (size_t)sz;

		/* Compare against the remaining span instead of adding: a corrupt
		 * header with an offset near 0xFFFFFFFF would wrap around and pass. */
		if (offMaps > size || 44u * (u32)mapCount > size - offMaps) break;
		if (offDoors > size || 6u * (u32)doorCount > size - offDoors) break;
		if (offNpcs > size || 3u * (u32)npcCount > size - offNpcs) break;
		if (offCells > size) break;

		u32 cellCursor = offCells;
		for (int i = 0; i < mapCount; i++)
		{
			const u8 *e = pack + offMaps + (u32)i * 44;
			GameMap *m = &s_maps[i];
			m->w = e[0];
			m->h = e[1];
			if (m->w == 0 || m->h == 0) { ok = false; goto done_parse; }
			for (int g = 0; g < 8; g++)
				m->ground[g] = rd16(e + 4 + g * 2);
			memcpy(m->name, e + 20, MAP_NAME_LEN);
			m->name[MAP_NAME_LEN - 1] = '\0';

			size_t cellBytes = (size_t)m->w * m->h * 2;
			if (cellCursor + cellBytes > size) goto done_parse;
			m->cells = (const u16 *)(pack + cellCursor);
			cellCursor += (u32)cellBytes;
		}

		for (int i = 0; i < doorCount; i++)
		{
			const u8 *e = pack + offDoors + (u32)i * 6;
			s_doors[i].tile = rd16(e);
			s_doors[i].targetMap = e[2];
			s_doors[i].pad = e[3];
			s_doors[i].tx = e[4];
			s_doors[i].ty = e[5];
		}
		for (int i = 0; i < npcCount; i++)
		{
			const u8 *e = pack + offNpcs + (u32)i * 3;
			s_npcs[i].tile = rd16(e);
			s_npcs[i].npcId = e[2];
		}

		s_startMap = pack[28];
		s_startX = pack[29];
		s_startY = pack[30];
		if (s_startMap >= mapCount) break;
		if (s_startX >= s_maps[s_startMap].w || s_startY >= s_maps[s_startMap].h) break;

		s_mapCount = mapCount;
		s_doorCount = doorCount;
		s_npcCount = npcCount;
		ok = true;
	} while (0);

done_parse:
	if (!ok)
	{
		free(pack);
		return false;
	}

	free(s_pack);
	s_pack = pack;
	s_size = (size_t)sz;
	return true;
}

int map_count(void) { return s_mapCount; }

const GameMap *map_get(int index)
{
	if (index < 0 || index >= s_mapCount)
		return NULL;
	return &s_maps[index];
}

u8 map_start_map(void) { return s_startMap; }
u8 map_start_x(void) { return s_startX; }
u8 map_start_y(void) { return s_startY; }

u16 map_cell(const GameMap *m, int x, int y)
{
	if (!m || x < 0 || y < 0 || x >= m->w || y >= m->h)
		return 0xFFFF;
	return m->cells[y * m->w + x];
}

bool map_blocked(const GameMap *m, int x, int y)
{
	u16 c = map_cell(m, x, y);
	if (c == 0xFFFF)
		return true;
	u16 obj = CELL_OBJ(c);
	/* tools.s move_blak never consults check_stat: a killed NPC's cell
	 * still blocks, even though get_image_id draws a 315 skull there. */
	return (obj >= BLAK_OFF && obj < DOOR_OFF) || (obj >= NPC_OFF);
}

const Door *map_door_at(const GameMap *m, int x, int y)
{
	u16 c = map_cell(m, x, y);
	if (c == 0xFFFF)
		return NULL;
	u16 obj = CELL_OBJ(c);
	if (obj < DOOR_OFF || obj >= NPC_OFF)
		return NULL;
	unsigned idx = obj - DOOR_OFF;
	return idx < (unsigned)s_doorCount ? &s_doors[idx] : NULL;
}

u16 map_resolve_tile(const GameMap *m, u16 obj)
{
	(void)m;
	if (obj < BLAK_OFF)
		return obj;
	if (obj < DOOR_OFF)
	{
		u16 t = obj - BLAK_OFF;
		return t < TILE_COUNT ? t : INVALID_TILE;
	}
	if (obj < NPC_OFF)
	{
		unsigned idx = obj - DOOR_OFF;
		if (idx >= (unsigned)s_doorCount)
			return INVALID_TILE;
		return s_doors[idx].tile < TILE_COUNT ? s_doors[idx].tile : INVALID_TILE;
	}
	unsigned idx = obj - NPC_OFF;
	if (idx >= (unsigned)s_npcCount)
		return INVALID_TILE;
	/* tools.s:226: check_stat failure -> tile 315 (skull). */
	if (npc_dead(idx))
		return 315;
	return s_npcs[idx].tile < TILE_COUNT ? s_npcs[idx].tile : INVALID_TILE;
}

int map_door_count(void) { return s_doorCount; }

const Door *map_door(int index)
{
	if (index < 0 || index >= s_doorCount)
		return NULL;
	return &s_doors[index];
}

int map_npc_count(void) { return s_npcCount; }

const Npc *map_npc(int index)
{
	if (index < 0 || index >= s_npcCount)
		return NULL;
	return &s_npcs[index];
}


