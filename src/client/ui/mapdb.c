/*
Copyright (C) 2024 Jonathan "Paril" Barkley

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ui.h"
#include "common/json.h"

typedef struct {
	char		id[32];
	char		command[32];
	char		name[64];
	bool		needsSkillSelect;
} mapdb_episode_t;

typedef struct {
	char		bsp[64];
	char		title[64];
	char		episode[32];
	char		short_name[8];
	uint8_t		unit;
	bool		sp;
	bool		dm;
	bool		bots;
	bool		ctf;
	bool		tdm;
	bool		coop;
	bool		display_bsp;
	char		*start_items;
} mapdb_map_t;

typedef struct {
	mapdb_episode_t		*episodes;
	size_t				num_episodes;
	mapdb_map_t			*maps;
	size_t				num_maps;
} mapdb_t;

static mapdb_t mapdb;

typedef enum {
	PRIM_UNKNOWN,
	
	PRIM_U8,
	PRIM_U16,
	PRIM_U32,
	PRIM_U64,
	PRIM_I8,
	PRIM_I16,
	PRIM_I32,
	PRIM_I64,
	PRIM_FLOAT,
	PRIM_DOUBLE,
	PRIM_BOOLEAN,
	PRIM_NULL	
} mapdb_primitive_type_t;

typedef struct {
	const char				*key;
	jsmntype_t				type;
	uintptr_t				offset;
	union {
		mapdb_primitive_type_t	primitive;
		size_t                  len; // for strings
	};
} mapdb_key_t;

#define q_member_sizeof(t, m) (sizeof(((t *) 0)->m ))

#define KEY_FIXED_STRING(name) \
	#name, JSMN_STRING, q_offsetof(KEY_TYPE, name), q_member_sizeof(KEY_TYPE, name)
#define KEY_DYNAMIC_STRING(name) \
	#name, JSMN_STRING, q_offsetof(KEY_TYPE, name), 0
#define KEY_SKIP(name) \
	#name, JSMN_UNDEFINED
#define KEY_U8(name) \
	#name, JSMN_PRIMITIVE, q_offsetof(KEY_TYPE, name), PRIM_U8
#define KEY_BOOLEAN(name) \
	#name, JSMN_PRIMITIVE, q_offsetof(KEY_TYPE, name), PRIM_BOOLEAN

#define KEY_TYPE mapdb_episode_t
static const mapdb_key_t episode_keys[] = {
	{ KEY_FIXED_STRING(id) },
	{ KEY_FIXED_STRING(command) },
	{ KEY_FIXED_STRING(name) },
	{ KEY_SKIP(activity) },
	{ KEY_BOOLEAN(needsSkillSelect) },
	{ NULL }
};
#undef KEY_TYPE

#define KEY_TYPE mapdb_map_t
static const mapdb_key_t map_keys[] = {
	{ KEY_FIXED_STRING(bsp) },
	{ KEY_FIXED_STRING(title) },
	{ KEY_FIXED_STRING(episode) },
	{ KEY_FIXED_STRING(short_name) },
	{ KEY_U8(unit) },
	{ KEY_BOOLEAN(sp) },
	{ KEY_BOOLEAN(dm) },
	{ KEY_BOOLEAN(bots) },
	{ KEY_BOOLEAN(ctf) },
	{ KEY_BOOLEAN(tdm) },
	{ KEY_BOOLEAN(coop) },
	{ KEY_BOOLEAN(display_bsp) },
	{ KEY_DYNAMIC_STRING(start_items) },
	{ NULL }
};
#undef KEY_TYPE

#define KEY_MEM \
	((uint8_t *) obj + key->offset)

static void MapDB_ParseKeys(json_parse_t *parser, void *obj, const mapdb_key_t *keys)
{
	jsmntok_t *jobj = Json_EnsureNext(parser, JSMN_OBJECT);

	for (size_t i = 0; i < jobj->size; i++) {
		bool parsed = false;

		for (const mapdb_key_t *key = keys; key->key; key++) {
			if (Json_Strcmp(parser, key->key))
				continue;

			Json_Next(parser);

			if (key->type == JSMN_UNDEFINED) {
				Json_Next(parser);
				parsed = true;
				break;
			}

			Json_Ensure(parser, key->type);

			if (key->type == JSMN_STRING) {
				size_t l = Json_Strlen(parser);
				if (key->len) {
					Q_strnlcpy((char *) KEY_MEM, parser->buffer + parser->pos->start, l, key->len);
				} else {
					char *buf = Z_TagMalloc(l + 1, TAG_MAPDB);

					if (!buf)
						Json_Errorno(parser, parser->pos, Q_ERR(ENOMEM));

					Q_strnlcpy(buf, parser->buffer + parser->pos->start, l, l + 1);
					*((char **) KEY_MEM) = buf;
				}
			} else if (key->type == JSMN_PRIMITIVE) {
				if (key->primitive == PRIM_BOOLEAN) {
					*((bool *) KEY_MEM) = parser->buffer[parser->pos->start] == 't';
				} else if (key->primitive == PRIM_U8) {
					*((uint8_t *) KEY_MEM) = strtol(parser->buffer + parser->pos->start, NULL, 10);
				} else {
					Json_Error(parser, parser->pos, "Unsupported primitive");
				}
			}

			Json_Next(parser);

			parsed = true;
			break;
		}

		if (!parsed) {
			Json_ErrorLocation(parser, parser->tokens);
			Com_DPrintf("unknown key in mapdb.json[%s]\n", parser->error_loc);
			Json_Next(parser);
			Json_SkipToken(parser);
			continue;
		}
	}
}

void MapDB_FetchEpisodes(char ***items, int *num_items)
{
	*num_items = mapdb.num_episodes;
	*items = UI_Mallocz(sizeof(char *) * (*num_items + 1));
    for (int i = 0; i < *num_items; i++) {
        (*items)[i] = UI_CopyString(mapdb.episodes[i].name);
    }
}

void MapDB_FetchUnits(char ***items, int **item_indices, int *num_items)
{
	*num_items = 0;

	for (int i = 0; i < mapdb.num_maps; i++)
		if (mapdb.maps[i].sp)
			(*num_items)++;

	*items = UI_Mallocz(sizeof(char *) * (*num_items + 1));
	*item_indices = UI_Mallocz(sizeof(int) * (*num_items));

	for (int i = 0, n = 0; i < mapdb.num_maps; i++) {
		if (!mapdb.maps[i].sp)
			continue;

		mapdb_episode_t *episode = NULL;

		for (int e = 0; e < mapdb.num_episodes; e++) {
			if (!strcmp(mapdb.episodes[e].id, mapdb.maps[i].episode)) {
				episode = &mapdb.episodes[e];
				break;
			}
		}

        (*items)[n] = UI_CopyString(va("(%s)\n%s", episode ? episode->name : "???", mapdb.maps[i].title));
		(*item_indices)[n] = i;
		n++;
	}
}

static cvar_t *mapdb_episode;
static cvar_t *mapdb_level;
static cvar_t *mapdb_type;

static void MapDB_Run_f(void)
{
	if (!strcmp(mapdb_type->string, "episode")) {
		int episode = mapdb_episode->integer;

		if (episode < 0 || episode >= mapdb.num_episodes) {
			Com_WPrintf("bad _mapdb_episode\n");
			return;
		}

		Cvar_Set("g_start_items", "");

		Cbuf_AddText(&cmd_buffer, mapdb.episodes[episode].command);
		Cbuf_AddText(&cmd_buffer, "\n");

	} else if (!strcmp(mapdb_type->string, "level")) {
		int level = mapdb_level->integer;

		if (level < 0 || level >= mapdb.num_maps) {
			Com_WPrintf("bad _mapdb_level\n");
			return;
		}

		Cvar_Set("g_start_items", mapdb.maps[level].start_items);

		Cbuf_AddText(&cmd_buffer, "map ");
		Cbuf_AddText(&cmd_buffer, mapdb.maps[level].bsp);
		Cbuf_AddText(&cmd_buffer, "\n");

	} else {
		Com_WPrintf("unknown _mapdb_type\n");
	}
}

void MapDB_Init(void)
{
	json_parse_t parser = {0};
	
    if (Json_ErrorHandler(parser)) {
		Com_WPrintf("Failed to load/parse mapdb.json[%s]: %s\n", parser.error_loc, parser.error);
		Z_Free(mapdb.episodes);
		Z_Free(mapdb.maps);
        return;
    }
	
	Json_Load("mapdb.json", &parser);

	jsmntok_t *obj = Json_EnsureNext(&parser, JSMN_OBJECT);

	for (int i = 0; i < obj->size; i++) {
		if (Json_Strcmp(&parser, "episodes") == 0) {
			Json_Next(&parser);

			jsmntok_t *episodes = Json_EnsureNext(&parser, JSMN_ARRAY);

			mapdb.num_episodes = episodes->size;
			mapdb.episodes = Z_TagMallocz(sizeof(mapdb_episode_t) * mapdb.num_episodes, TAG_MAPDB);

			if (!mapdb.episodes)
				Json_Errorno(&parser, episodes, Q_ERR(ENOMEM));

			for (size_t i = 0; i < mapdb.num_episodes; i++)
				MapDB_ParseKeys(&parser, &mapdb.episodes[i], episode_keys);

		} else if (Json_Strcmp(&parser, "maps") == 0) {
			Json_Next(&parser);

			jsmntok_t *maps = Json_EnsureNext(&parser, JSMN_ARRAY);

			mapdb.num_maps = maps->size;
			mapdb.maps = Z_TagMallocz(sizeof(mapdb_map_t) * mapdb.num_maps, TAG_MAPDB);

			if (!mapdb.maps)
				Json_Errorno(&parser, maps, Q_ERR(ENOMEM));

			for (size_t i = 0; i < mapdb.num_maps; i++)
				MapDB_ParseKeys(&parser, &mapdb.maps[i], map_keys);

		} else {
			Json_SkipToken(&parser);
		}
	}

	Json_Free(&parser);
	
	mapdb_episode = Cvar_Get("_mapdb_episode", "-1", 0);
	mapdb_level = Cvar_Get("_mapdb_level", "-1", 0);
	mapdb_type = Cvar_Get("_mapdb_type", "episode", 0);

	Cmd_AddCommand("_mapdb_run", MapDB_Run_f);
}

void MapDB_Shutdown(void)
{
	for (size_t i = 0; i < mapdb.num_maps; i++)
		Z_Free(mapdb.maps[i].start_items);

	Z_Free(mapdb.episodes);
	Z_Free(mapdb.maps);

	memset(&mapdb, 0, sizeof(mapdb));

    Z_LeakTest(TAG_MAPDB);

	Cmd_RemoveCommand("_mapdb_run");
}