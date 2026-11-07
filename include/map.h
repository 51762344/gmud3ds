#ifndef MAP_H
#define MAP_H

struct Player;
void map_set_player(const struct Player *p);

#include "gmud.h"

bool map_pack_load(const char *romfs_path);
void map_pack_free(void);

int map_count(void);
const GameMap *map_get(int index);

u8 map_start_map(void);
u8 map_start_x(void);
u8 map_start_y(void);

u16 map_cell(const GameMap *m, int x, int y);
bool map_blocked(const GameMap *m, int x, int y);
/* tools.s get_image_id draws a 315 skull for a dead NPC, but move_blak
 * still treats the cell as an obstacle. */
bool map_npc_dead_index(unsigned index);
const Door *map_door_at(const GameMap *m, int x, int y);
u16 map_resolve_tile(const GameMap *m, u16 obj);

int map_door_count(void);
const Door *map_door(int index);
int map_npc_count(void);
const Npc *map_npc(int index);

#endif
