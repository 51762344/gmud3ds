/* GMUD 3DS - shared constants and data types */
#ifndef GMUD_H
#define GMUD_H

#include <3ds.h>
#include <citro2d.h>

#define SCR_W 400
#define SCR_H 240

/* 0 = normal 400x240 (Azahar + New 3DS LL), 1 = optional 800x240 wide output.
 * WIDE only scales the top screen X axis. */
#ifndef TOP_WIDE
#define TOP_WIDE 0
#endif
#define TOP_X_SCALE (TOP_WIDE ? 2 : 1)
#define TOP_FB_W (SCR_W * TOP_X_SCALE)

/* The top screen is the world (plus transient notices); place/time and the
 * status dashboard live on the bottom screen, so the viewport is centred
 * with an 8px ink margin. */
#define MAP_VIEW_X 8
#define MAP_VIEW_Y 8
#define MAP_VIEW_W 384
#define MAP_VIEW_H 224

#define TILE 32
#define TILE_COUNT 512

#define MAX_MAPS 64
#define MAX_DOORS 256
#define MAX_NPCS 256
#define MAP_NAME_LEN 24

#define INVALID_TILE 0xFFFF

#define BLAK_OFF 1024
#define DOOR_OFF 2048
#define NPC_OFF 3072

#define CELL_OBJ(c) ((u16)((c) & 0x1FFFu))
#define CELL_GROUND(c) ((u8)((c) >> 13))

/* citro2d color is 0xAABBGGRR; theme hex values are #RRGGBB. */
#define C2D_RGBA(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)

typedef struct
{
	u8 w, h;
	u16 ground[8];
	char name[MAP_NAME_LEN];   /* UTF-8 */
	const u16 *cells;          /* w*h, row major, points into the pack buffer */
} GameMap;

typedef struct
{
	u16 tile;
	u8 targetMap;
	u8 pad;
	u8 tx, ty;
} Door;

typedef struct
{
	u16 tile;
	u8 npcId;
} Npc;

typedef enum
{
	LAYER_GROUND = 0,
	LAYER_OBJECT = 1,
} TileLayer;

#endif
