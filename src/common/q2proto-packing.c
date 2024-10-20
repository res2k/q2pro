/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2024 Andrey Nazarov
Copyright (C) 2024 Frank Richter

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

#include "shared/shared.h"
#include "common/q2proto_shared.h"

#define Q2P_PACK_GET_ENTITY_VALUE(ENTITY, MEMBER)   (Q2P_EntityState_##MEMBER(ENTITY))

static inline int Q2P_EntityState_modelindex(struct entity_state_packing_type esp)
{
    return esp.in->modelindex;
}

static inline int Q2P_EntityState_modelindex2(struct entity_state_packing_type esp)
{
    return esp.in->modelindex2;
}

static inline int Q2P_EntityState_modelindex3(struct entity_state_packing_type esp)
{
    return esp.in->modelindex3;
}

static inline int Q2P_EntityState_modelindex4(struct entity_state_packing_type esp)
{
    return esp.in->modelindex4;
}

static inline int Q2P_EntityState_frame(struct entity_state_packing_type esp)
{
    return esp.in->frame;
}

static inline int Q2P_EntityState_skinnum(struct entity_state_packing_type esp)
{
    return esp.in->skinnum;
}

static inline uint64_t Q2P_EntityState_effects(struct entity_state_packing_type esp)
{
    uint64_t fx = esp.in->effects;
#if USE_PROTOCOL_EXTENSIONS
    if (esp.ext)
        fx |= ((uint64_t)esp.ext->morefx) << 32;
#endif
    return fx;
}

static inline uint32_t Q2P_EntityState_renderfx(struct entity_state_packing_type esp)
{
    return esp.in->renderfx;
}

static inline const float* Q2P_EntityState_origin(struct entity_state_packing_type esp)
{
    return esp.in->origin;
}

static inline const float* Q2P_EntityState_angles(struct entity_state_packing_type esp)
{
    return esp.in->angles;
}

static inline const float* Q2P_EntityState_old_origin(struct entity_state_packing_type esp)
{
    return esp.in->old_origin;
}

static inline int Q2P_EntityState_sound(struct entity_state_packing_type esp)
{
    return esp.in->sound;
}

static inline float Q2P_EntityState_loop_volume(struct entity_state_packing_type esp)
{
#if USE_PROTOCOL_EXTENSIONS
    if (esp.ext)
        return esp.ext->loop_volume;
#endif
    return 1.f;
}

static inline float Q2P_EntityState_loop_attenuation(struct entity_state_packing_type esp)
{
#if USE_PROTOCOL_EXTENSIONS
    if (esp.ext)
        return esp.ext->loop_attenuation;
#endif
    return 0.f;
}

static inline int Q2P_EntityState_event(struct entity_state_packing_type esp)
{
    return esp.in->event;
}

static inline int Q2P_EntityState_solid(struct entity_state_packing_type esp)
{
    return esp.in->solid;
}

static inline float Q2P_EntityState_alpha(struct entity_state_packing_type esp)
{
#if USE_PROTOCOL_EXTENSIONS
    if (esp.ext)
        return esp.ext->alpha;
#endif
    return 1.f;
}

static inline float Q2P_EntityState_scale(struct entity_state_packing_type esp)
{
#if USE_PROTOCOL_EXTENSIONS
    if (esp.ext)
        return esp.ext->scale;
#endif
    return 1.f;
}

#define Q2P_PACK_ENTITY_FUNCTION_NAME    PackEntity
#define Q2P_PACK_ENTITY_TYPE             struct entity_state_packing_type

#include "q2proto/q2proto_packing_entitystate_impl.inc"

// The main thing to deal with is gunindex, gunskin...
#define Q2P_PACK_GET_PLAYER_VALUE(PLAYER, MEMBER)   (Q2P_PlayerStateNew_##MEMBER(PLAYER))
#define Q2P_PACK_GET_PLAYER_HEIGHTFOG_VALUE(PLAYER, HF_MEMBER)   (Q2P_PlayerStateNew_heightfog_##HF_MEMBER(PLAYER))

static inline const float* Q2P_PlayerStateNew_viewangles(const player_state_new_t *ps)
{
    return ps->viewangles;
}

static inline const float* Q2P_PlayerStateNew_viewoffset(const player_state_new_t *ps)
{
    return ps->viewoffset;
}

static inline const float* Q2P_PlayerStateNew_kick_angles(const player_state_new_t *ps)
{
    return ps->kick_angles;
}

static inline const float* Q2P_PlayerStateNew_gunangles(const player_state_new_t *ps)
{
    return ps->gunangles;
}

static inline const float* Q2P_PlayerStateNew_gunoffset(const player_state_new_t *ps)
{
    return ps->gunoffset;
}

static inline int Q2P_PlayerStateNew_gunindex(const player_state_new_t *ps)
{
    return ps->gunindex & GUNINDEX_MASK;
}

static inline int Q2P_PlayerStateNew_gunskin(const player_state_new_t *ps)
{
    return ps->gunindex >> GUNINDEX_BITS;
}

static inline int Q2P_PlayerStateNew_gunframe(const player_state_new_t *ps)
{
    return ps->gunframe;
}

static inline const float* Q2P_PlayerStateNew_blend(const player_state_new_t *ps)
{
    return ps->blend;
}

static inline const float* Q2P_PlayerStateNew_damage_blend(const player_state_new_t *ps)
{
    return ps->damage_blend;
}

static inline int Q2P_PlayerStateNew_fov(const player_state_new_t *ps)
{
    return ps->fov;
}

static inline int Q2P_PlayerStateNew_rdflags(const player_state_new_t *ps)
{
    return ps->rdflags;
}

static inline const short* Q2P_PlayerStateNew_stats(const player_state_new_t *ps)
{
    return ps->stats;
}

static inline const float* Q2P_PlayerStateNew_heightfog_start_color(const player_state_new_t *ps)
{
    return ps->heightfog.start.color;
}

static inline const float* Q2P_PlayerStateNew_heightfog_end_color(const player_state_new_t *ps)
{
    return ps->heightfog.end.color;
}

static inline float Q2P_PlayerStateNew_heightfog_density(const player_state_new_t *ps)
{
    return ps->heightfog.density;
}

static inline float Q2P_PlayerStateNew_heightfog_falloff(const player_state_new_t *ps)
{
    return ps->heightfog.falloff;
}

static inline float Q2P_PlayerStateNew_heightfog_start_dist(const player_state_new_t *ps)
{
    return ps->heightfog.start.dist;
}

static inline float Q2P_PlayerStateNew_heightfog_end_dist(const player_state_new_t *ps)
{
    return ps->heightfog.end.dist;
}

#define Q2P_PACK_PLAYER_FUNCTION_NAME    PackPlayerstateNew
#define Q2P_PACK_PLAYER_TYPE             player_state_new_t*
#define Q2P_PACK_PLAYER_STATS_NUM(PLAYER)       MAX_STATS_NEW

#include "q2proto/q2proto_packing_playerstate_impl.inc"

#undef Q2P_PACK_GET_PLAYER_VALUE
#undef Q2P_PACK_GET_PLAYER_HEIGHTFOG_VALUE
#undef Q2P_PACK_PLAYER_FUNCTION_NAME
#undef Q2P_PACK_PLAYER_TYPE
#undef Q2P_PACK_PLAYER_STATS_NUM

// player_state_old_t lacks some fields, so use wrapper functions here
#define Q2P_PACK_GET_PLAYER_VALUE(PLAYER, MEMBER)   (Q2P_PlayerStateOld_##MEMBER(PLAYER))
#define Q2P_PACK_GET_PLAYER_FOG_VALUE(PLAYER, FOG_MEMBER)   (Q2P_PlayerStateOld_fog_##FOG_MEMBER(PLAYER))
#define Q2P_PACK_GET_PLAYER_HEIGHTFOG_VALUE(PLAYER, HF_MEMBER)   (Q2P_PlayerStateOld_heightfog_##HF_MEMBER(PLAYER))

static inline const float* Q2P_PlayerStateOld_viewangles(const player_state_old_t *ps)
{
    return ps->viewangles;
}

static inline const float* Q2P_PlayerStateOld_viewoffset(const player_state_old_t *ps)
{
    return ps->viewoffset;
}

static inline const float* Q2P_PlayerStateOld_kick_angles(const player_state_old_t *ps)
{
    return ps->kick_angles;
}

static inline const float* Q2P_PlayerStateOld_gunangles(const player_state_old_t *ps)
{
    return ps->gunangles;
}

static inline const float* Q2P_PlayerStateOld_gunoffset(const player_state_old_t *ps)
{
    return ps->gunoffset;
}

static inline int Q2P_PlayerStateOld_gunindex(const player_state_old_t *ps)
{
    return ps->gunindex & GUNINDEX_MASK;
}

static inline int Q2P_PlayerStateOld_gunskin(const player_state_old_t *ps)
{
    return ps->gunindex >> GUNINDEX_BITS;
}

static inline int Q2P_PlayerStateOld_gunframe(const player_state_old_t *ps)
{
    return ps->gunframe;
}

static inline const float* Q2P_PlayerStateOld_blend(const player_state_old_t *ps)
{
    return ps->blend;
}

static const float zero_blend[4] = {0, 0, 0, 0};

static inline const float* Q2P_PlayerStateOld_damage_blend(const player_state_old_t *ps)
{
    return zero_blend;
}

static inline int Q2P_PlayerStateOld_fov(const player_state_old_t *ps)
{
    return ps->fov;
}

static inline int Q2P_PlayerStateOld_rdflags(const player_state_old_t *ps)
{
    return ps->rdflags;
}

static inline const short* Q2P_PlayerStateOld_stats(const player_state_old_t *ps)
{
    return ps->stats;
}

static inline const float* Q2P_PlayerStateOld_fog_color(const player_state_old_t *ps)
{
    return vec3_origin;
}

static inline float Q2P_PlayerStateOld_fog_density(const player_state_old_t *ps)
{
    return 0;
}

static inline float Q2P_PlayerStateOld_fog_sky_factor(const player_state_old_t *ps)
{
    return 0;
}

static inline const float* Q2P_PlayerStateOld_heightfog_start_color(const player_state_old_t *ps)
{
    return vec3_origin;
}

static inline const float* Q2P_PlayerStateOld_heightfog_end_color(const player_state_old_t *ps)
{
    return vec3_origin;
}

static inline float Q2P_PlayerStateOld_heightfog_density(const player_state_old_t *ps)
{
    return 0;
}

static inline float Q2P_PlayerStateOld_heightfog_falloff(const player_state_old_t *ps)
{
    return 0;
}

static inline float Q2P_PlayerStateOld_heightfog_start_dist(const player_state_old_t *ps)
{
    return 0;
}

static inline float Q2P_PlayerStateOld_heightfog_end_dist(const player_state_old_t *ps)
{
    return 0;
}

#define Q2P_PACK_PLAYER_FUNCTION_NAME    PackPlayerstateOld
#define Q2P_PACK_PLAYER_TYPE             player_state_old_t*
#define Q2P_PACK_PLAYER_STATS_NUM(PLAYER)       MAX_STATS_OLD

#include "q2proto/q2proto_packing_playerstate_impl.inc"
