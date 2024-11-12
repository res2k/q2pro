/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "server.h"

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/

// some protocol optimizations are disabled when recording a demo
#define Q2PRO_OPTIMIZE(c) \
    (!(c)->settings[CLS_RECORDING])

/*
=============
SV_TruncPacketEntities

Truncates remainder of entity_packed_t list, patching current frame to make
delta compression happy.
=============
*/
static bool SV_TruncPacketEntities(client_t *client, const client_frame_t *from,
                                   client_frame_t *to, int oldindex, int newindex)
{
    entity_packed_t *newent;
    const entity_packed_t *oldent;
    int i, oldnum, newnum, entities_mask, from_num_entities, to_num_entities;
    bool ret = true;

    if (!sv_trunc_packet_entities->integer || client->netchan.type)
        return false;

    SV_DPrintf(0, "Truncating frame %d at %u bytes for %s\n",
               client->framenum, msg_write.cursize, client->name);

    if (!from)
        from_num_entities = 0;
    else
        from_num_entities = from->num_entities;
    to_num_entities = to->num_entities;

    entities_mask = client->num_entities - 1;
    oldent = newent = NULL;
    while (newindex < to->num_entities || oldindex < from_num_entities) {
        if (newindex >= to->num_entities) {
            newnum = MAX_EDICTS;
        } else {
            i = (to->first_entity + newindex) & entities_mask;
            newent = &client->entities[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (from->first_entity + oldindex) & entities_mask;
            oldent = &client->entities[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // skip delta update
            *newent = *oldent;
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // remove new entity from frame
            to->num_entities--;
            for (i = newindex; i < to->num_entities; i++) {
                client->entities[(to->first_entity + i    ) & entities_mask] =
                client->entities[(to->first_entity + i + 1) & entities_mask];
            }
            continue;
        }

        if (newnum > oldnum) {
            // drop the frame if entity list got too big.
            // should not normally happen.
            if (to->num_entities >= MAX_PACKET_ENTITIES) {
                ret = false;
                break;
            }

            // insert old entity into frame
            for (i = to->num_entities - 1; i >= newindex; i--) {
                client->entities[(to->first_entity + i + 1) & entities_mask] =
                client->entities[(to->first_entity + i    ) & entities_mask];
            }

            client->entities[(to->first_entity + newindex) & entities_mask] = *oldent;
            to->num_entities++;

            // should never go backwards
            to_num_entities = max(to_num_entities, to->num_entities);

            oldindex++;
            newindex++;
            continue;
        }
    }

    client->next_entity = to->first_entity + to_num_entities;
    return ret;
}

static client_frame_t *get_last_frame(client_t *client)
{
    client_frame_t *frame;

    if (client->lastframe <= 0) {
        // client is asking for a retransmit
        client->frames_nodelta++;
        return NULL;
    }

    client->frames_nodelta = 0;

    if (client->framenum - client->lastframe >= UPDATE_BACKUP) {
        // client hasn't gotten a good message through in a long time
        Com_DPrintf("%s: delta request from out-of-date packet.\n", client->name);
        return NULL;
    }

    // we have a valid message to delta from
    frame = &client->frames[client->lastframe & UPDATE_MASK];
    if (frame->number != client->lastframe) {
        // but it got never sent
        Com_DPrintf("%s: delta request from dropped frame.\n", client->name);
        return NULL;
    }

    if (client->next_entity - frame->first_entity > client->num_entities) {
        // but entities are too old
        Com_DPrintf("%s: delta request from out-of-date entities.\n", client->name);
        return NULL;
    }

    return frame;
}

static inline void make_blend_delta(const uint8_t from[4], const uint8_t to[4], q2proto_color_delta_t *blend)
{
    int bits = 0;
    for (int i = 0; i < 4; i++) {
        if (from[i] != to[i])
            bits |= BIT(i);
    }

    q2proto_var_color_set_byte(&blend->values, to);
    blend->delta_bits = bits;
}

static void make_playerstate_delta(const player_packed_t *from, const player_packed_t *to, q2proto_svc_playerstate_t *playerstate, msgPsFlags_t flags)
{
    int     i;

    Q_assert(to);

    if (!from)
        from = &nullPlayerState;

    memset(playerstate, 0, sizeof(*playerstate));

    if (to->pmove.pm_type != from->pmove.pm_type) {
        playerstate->delta_bits |= Q2P_PSD_PM_TYPE;
        playerstate->pm_type = to->pmove.pm_type;
    }

    q2proto_var_coords_set_float(&playerstate->pm_origin.write.prev, from->pmove.origin);
    q2proto_var_coords_set_float(&playerstate->pm_origin.write.current, to->pmove.origin);

    if (!(flags & MSG_PS_IGNORE_PREDICTION)) {
        q2proto_var_coords_set_float(&playerstate->pm_velocity.write.prev, from->pmove.velocity);
        q2proto_var_coords_set_float(&playerstate->pm_velocity.write.current, to->pmove.velocity);

        if (to->pmove.pm_time != from->pmove.pm_time) {
            playerstate->delta_bits |= Q2P_PSD_PM_TIME;
            playerstate->pm_time = to->pmove.pm_time;
        }

        if (to->pmove.pm_flags != from->pmove.pm_flags) {
            playerstate->delta_bits |= Q2P_PSD_PM_FLAGS;
            playerstate->pm_flags = to->pmove.pm_flags;
        }

        if (to->pmove.gravity != from->pmove.gravity) {
            playerstate->delta_bits |= Q2P_PSD_PM_GRAVITY;
            playerstate->pm_gravity = to->pmove.gravity;
        }
    }

    if (!(flags & MSG_PS_IGNORE_DELTAANGLES)) {
        if (!VectorCompare(to->pmove.delta_angles, from->pmove.delta_angles)) {
            playerstate->delta_bits |= Q2P_PSD_PM_DELTA_ANGLES;
            q2proto_var_angles_set_float(&playerstate->pm_delta_angles, to->pmove.delta_angles);
        }
    }

    if (!VectorCompare(to->viewoffset, from->viewoffset)) {
        playerstate->delta_bits |= Q2P_PSD_VIEWOFFSET;
        q2proto_var_small_offsets_set_q2repro_viewoffset(&playerstate->viewoffset, to->viewoffset);
    }

    if (!(flags & MSG_PS_IGNORE_VIEWANGLES))
        Q2PROTO_SET_ANGLES_DELTA(playerstate->viewangles, to->viewangles, from->viewangles, short);

    if (!VectorCompare(to->kick_angles, from->kick_angles)) {
        playerstate->delta_bits |= Q2P_PSD_KICKANGLES;
        q2proto_var_small_angles_set_q2repro_kick_angles(&playerstate->kick_angles, to->kick_angles);
    }

    if (!(flags & MSG_PS_IGNORE_BLEND)) {
        make_blend_delta(from->screen_blend, to->screen_blend, &playerstate->blend);
        make_blend_delta(from->damage_blend, to->damage_blend, &playerstate->damage_blend);
    }

    if (to->fov != from->fov) {
        playerstate->delta_bits |= Q2P_PSD_FOV;
        playerstate->fov = to->fov;
    }

    if (to->rdflags != from->rdflags) {
        playerstate->delta_bits |= Q2P_PSD_RDFLAGS;
        playerstate->rdflags = to->rdflags;
    }

    if (!(flags & MSG_PS_IGNORE_GUNFRAMES)) {
        if (to->gunframe != from->gunframe)
            playerstate->delta_bits |= Q2P_PSD_GUNFRAME;
        if (!VectorCompare(to->gunoffset, from->gunoffset))
            playerstate->delta_bits |= Q2P_PSD_GUNOFFSET;
        if (!VectorCompare(to->gunangles, from->gunangles))
            playerstate->delta_bits |= Q2P_PSD_GUNANGLES;
        if (playerstate->delta_bits & (Q2P_PSD_GUNFRAME | Q2P_PSD_GUNOFFSET | Q2P_PSD_GUNANGLES)) {
            playerstate->gunframe = to->gunframe;
            q2proto_var_small_offsets_set_q2repro_gunoffset(&playerstate->gunoffset, to->gunoffset);
            q2proto_var_small_angles_set_q2repro_gunangles(&playerstate->gunangles, to->gunangles);
        }
    }

    if (!(flags & MSG_PS_IGNORE_GUNINDEX)) {
        if (to->gunindex != from->gunindex) {
            playerstate->delta_bits |= Q2P_PSD_GUNINDEX;
            playerstate->gunindex = to->gunindex & GUNINDEX_MASK;
            playerstate->gunskin = to->gunindex >> GUNINDEX_BITS;
        }
    }


    for (i = 0; i < MAX_STATS; i++)
        if (to->stats[i] != from->stats[i]) {
            playerstate->statbits |= BIT_ULL(i);
            playerstate->stats[i] = to->stats[i];
        }

    if (to->gunrate != from->gunrate) {
        playerstate->delta_bits |= Q2P_PSD_GUNRATE;
        playerstate->gunrate = to->gunrate;
    }

    if (to->pmove.viewheight != from->pmove.viewheight) {
        playerstate->delta_bits |= Q2P_PSD_PM_VIEWHEIGHT;
        playerstate->pm_viewheight = to->pmove.viewheight;
    }
}

static void write_entity_delta(client_t *client, const entity_packed_t *from, const entity_packed_t *to, msgEsFlags_t flags)
{
    q2proto_svc_message_t message = {.type = Q2P_SVC_FRAME_ENTITY_DELTA, .frame_entity_delta = {0}};

    if (!to) {
        Q_assert(from);
        Q_assert(from->number > 0 && from->number < MAX_EDICTS);

        message.frame_entity_delta.remove = true;
        message.frame_entity_delta.newnum = from->number;
        q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

        return; // remove entity
    }

    Q_assert(to->number > 0 && to->number < MAX_EDICTS);
    message.frame_entity_delta.newnum = to->number;

    if (client->q2proto_ctx.features.has_beam_old_origin_fix)
        flags |= MSG_ES_BEAMORIGIN;
    bool entity_differs = SV_MakeEntityDelta(&message.frame_entity_delta.entity_delta, from, to, flags);
    if (!(flags & MSG_ES_FORCE) && !entity_differs)
        return;
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
}

static bool emit_packet_entities(client_t               *client,
                                 const client_frame_t   *from,
                                 client_frame_t         *to,
                                 int                    clientEntityNum,
                                 unsigned               maxsize)
{
    entity_packed_t *newent;
    const entity_packed_t *oldent;
    int i, oldnum, newnum, oldindex, newindex, from_num_entities;
    bool ret = true;

    if (msg_write.cursize + 2 > maxsize)
        return false;

    if (!from)
        from_num_entities = 0;
    else
        from_num_entities = from->num_entities;

    newindex = 0;
    oldindex = 0;
    oldent = newent = NULL;
    while (newindex < to->num_entities || oldindex < from_num_entities) {
        if (msg_write.cursize + MAX_PACKETENTITY_BYTES > maxsize) {
            ret = SV_TruncPacketEntities(client, from, to, oldindex, newindex);
            break;
        }

        if (newindex >= to->num_entities) {
            newnum = MAX_EDICTS;
        } else {
            i = (to->first_entity + newindex) & (client->num_entities - 1);
            newent = &client->entities[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (from->first_entity + oldindex) & (client->num_entities - 1);
            oldent = &client->entities[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // Delta update from old position. Because the force parm is false,
            // this will not result in any bytes being emitted if the entity has
            // not changed at all. Note that players are always 'newentities',
            // this updates their old_origin always and prevents warping in case
            // of packet loss.
            int flags = 0;
            if (newnum <= client->maxclients) {
                flags |= MSG_ES_NEWENTITY;
            }
            if (newnum == clientEntityNum) {
                flags |= MSG_ES_FIRSTPERSON;
                VectorCopy(oldent->origin, newent->origin);
                VectorCopy(oldent->angles, newent->angles);
            }
            write_entity_delta(client, oldent, newent, flags);
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // this is a new entity, send it from the baseline
            oldent = client->baselines[newnum >> SV_BASELINES_SHIFT];
            if (oldent) {
                oldent += (newnum & SV_BASELINES_MASK);
            } else {
                oldent = &nullEntityState;
            }
            write_entity_delta(client, oldent, newent, MSG_ES_NEWENTITY | MSG_ES_FORCE);
            newindex++;
            continue;
        }

        if (newnum > oldnum) {
            // the old entity isn't present in the new message
            write_entity_delta(client, oldent, NULL, 0);
            oldindex++;
            continue;
        }
    }

    // end of packetentities
    q2proto_svc_message_t message = {.type = Q2P_SVC_FRAME_ENTITY_DELTA, .frame_entity_delta = {0}};
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);
    return ret;
}


/*
==================
SV_WriteFrameToClient_Default
==================
*/
bool SV_WriteFrameToClient_Default(client_t *client, unsigned maxsize)
{
    client_frame_t  *frame, *oldframe;
    player_packed_t *oldstate;
    int             lastframe;

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame(client);
    if (oldframe) {
        oldstate = &oldframe->ps;
        lastframe = client->lastframe;
    } else {
        oldstate = NULL;
        lastframe = -1;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_FRAME, .frame = {0}};
    message.frame.serverframe = client->framenum;
    message.frame.deltaframe = lastframe;
    message.frame.suppress_count = client->suppress_count;
    message.frame.q2pro_frame_flags = client->frameflags;

    message.frame.areabits_len = frame->areabytes;
    message.frame.areabits = frame->areabits;

    make_playerstate_delta(oldstate, &frame->ps, &message.frame.playerstate, 0);
    if ((oldframe ? oldframe->clientNum : 0) != frame->clientNum) {
        message.frame.playerstate.clientnum = frame->clientNum;
        message.frame.playerstate.delta_bits |= Q2P_PSD_CLIENTNUM;
    }

    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    bool ret = emit_packet_entities(client, oldframe, frame, 0, maxsize);

    client->suppress_count = 0;
    client->frameflags = 0;
    return ret;
}

/*
==================
SV_WriteFrameToClient_Enhanced
==================
*/
bool SV_WriteFrameToClient_Enhanced(client_t *client, unsigned maxsize)
{
    client_frame_t  *frame, *oldframe;
    player_packed_t *oldstate;
    msgPsFlags_t    psFlags;
    int             clientEntityNum;

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame(client);
    int lastframe;
    if (oldframe) {
        oldstate = &oldframe->ps;
        lastframe = client->lastframe;
    } else {
        oldstate = NULL;
        lastframe = -1;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_FRAME, .frame = {0}};
    message.frame.serverframe = client->framenum;
    message.frame.deltaframe = lastframe;
    message.frame.suppress_count = client->suppress_count;
    message.frame.q2pro_frame_flags = client->frameflags;

    message.frame.areabits_len = frame->areabytes;
    message.frame.areabits = frame->areabits;

    // ignore some parts of playerstate if not recording demo
    psFlags = client->psFlags;
    if (!client->settings[CLS_RECORDING]) {
        if (client->settings[CLS_NOGUN]) {
            psFlags |= MSG_PS_IGNORE_GUNFRAMES;
            if (client->settings[CLS_NOGUN] != 2) {
                psFlags |= MSG_PS_IGNORE_GUNINDEX;
            }
        }
        if (client->settings[CLS_NOBLEND]) {
            psFlags |= MSG_PS_IGNORE_BLEND;
        }
        if (frame->ps.pmove.pm_type < PM_DEAD) {
            if (!(frame->ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
                psFlags |= MSG_PS_IGNORE_VIEWANGLES;
            }
        } else {
            // lying dead on a rotating platform?
            psFlags |= MSG_PS_IGNORE_DELTAANGLES;
        }
    }

    clientEntityNum = 0;
    if (frame->ps.pmove.pm_type < PM_DEAD && !client->settings[CLS_RECORDING]) {
        clientEntityNum = frame->clientNum + 1;
    }
    if (client->settings[CLS_NOPREDICT]) {
        psFlags |= MSG_PS_IGNORE_PREDICTION;
    }
    psFlags |= MSG_PS_EXTENSIONS;
    psFlags |= MSG_PS_RERELEASE;

    // delta encode the playerstate
    make_playerstate_delta(oldstate, &frame->ps, &message.frame.playerstate, psFlags);

    if ((oldframe ? oldframe->clientNum : 0) != frame->clientNum) {
        message.frame.playerstate.clientnum = frame->clientNum;
        message.frame.playerstate.delta_bits |= Q2P_PSD_CLIENTNUM;
    }

    client->suppress_count = 0;
    client->frameflags = 0;

    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    // delta encode the entities
    return emit_packet_entities(client, oldframe, frame, clientEntityNum, maxsize);
}

bool SV_MakeEntityDelta(q2proto_entity_state_delta_t *delta, const entity_packed_t *from, const entity_packed_t *to, msgEsFlags_t flags)
{
    if (!from)
        from = &nullEntityState;

// send an update
    if (!(flags & MSG_ES_FIRSTPERSON)) {
        q2proto_var_coords_set_float(&delta->origin.write.prev, from->origin);
        q2proto_var_coords_set_float(&delta->origin.write.current, to->origin);

        if (to->angles[0] != from->angles[0]) {
            delta->angle.delta_bits |= BIT(0);
            q2proto_var_angles_set_short_comp(&delta->angle.values, 0, to->angles[0]);
        }
        if (to->angles[1] != from->angles[1]) {
            delta->angle.delta_bits |= BIT(1);
            q2proto_var_angles_set_short_comp(&delta->angle.values, 1, to->angles[1]);
        }
        if (to->angles[2] != from->angles[2]) {
            delta->angle.delta_bits |= BIT(2);
            q2proto_var_angles_set_short_comp(&delta->angle.values, 2, to->angles[2]);
        }

        bool write_old_origin =
            ((flags & MSG_ES_NEWENTITY) && !VectorCompare(to->old_origin, from->origin))
            || ((to->renderfx & RF_FRAMELERP) && !VectorCompare(to->old_origin, from->origin))
            || ((to->renderfx & RF_BEAM) && (!(flags & MSG_ES_BEAMORIGIN) || !VectorCompare(to->old_origin, from->old_origin)));
        if (write_old_origin)
        {
            delta->delta_bits |= Q2P_ESD_OLD_ORIGIN;
            q2proto_var_coords_set_float(&delta->old_origin, to->old_origin);
        }
    }

    if (to->skinnum != from->skinnum) {
        delta->delta_bits |= Q2P_ESD_SKINNUM;
        delta->skinnum = to->skinnum;
    }

    if (to->frame != from->frame) {
        delta->delta_bits |= Q2P_ESD_FRAME;
        delta->frame = to->frame;
    }

    if (to->effects != from->effects) {
        if ((uint32_t)to->effects != (uint32_t)from->effects)
            delta->delta_bits |= Q2P_ESD_EFFECTS;
        if ((to->effects >> 32) != (from->effects >> 32))
            delta->delta_bits |= Q2P_ESD_EFFECTS_MORE;
        delta->effects = to->effects;
        delta->effects_more = to->effects >> 32;
    }

    if (to->renderfx != from->renderfx) {
        delta->delta_bits |= Q2P_ESD_RENDERFX;
        delta->renderfx = to->renderfx;
    }

    if (to->solid != from->solid) {
        delta->delta_bits |= Q2P_ESD_SOLID;
        delta->solid = to->solid;
    }

    // event is not delta compressed, just 0 compressed
    if (to->event) {
        delta->delta_bits |= Q2P_ESD_EVENT;
        delta->event = to->event;
    }

    if (to->modelindex != from->modelindex) {
        delta->delta_bits |= Q2P_ESD_MODELINDEX;
        delta->modelindex = to->modelindex;
    }
    if (to->modelindex2 != from->modelindex2) {
        delta->delta_bits |= Q2P_ESD_MODELINDEX2;
        delta->modelindex2 = to->modelindex2;
    }
    if (to->modelindex3 != from->modelindex3) {
        delta->delta_bits |= Q2P_ESD_MODELINDEX3;
        delta->modelindex3 = to->modelindex3;
    }
    if (to->modelindex4 != from->modelindex4) {
        delta->delta_bits |= Q2P_ESD_MODELINDEX4;
        delta->modelindex4 = to->modelindex4;
    }

    if (to->sound != from->sound) {
        delta->delta_bits |= Q2P_ESD_SOUND;
        delta->sound = to->sound;
    }

    return (delta->delta_bits != 0) || (to->origin[0] != from->origin[0]) || (to->origin[1] != from->origin[1]) || (to->origin[2] != from->origin[2]) || (delta->angle.delta_bits != 0);
}

/*
=============================================================================

Build a client frame structure

=============================================================================
*/

#if USE_FPS
static void
fix_old_origin(const client_t *client, entity_packed_t *state, const edict_t *ent, int e)
{
    server_entity_t *sent = &sv.entities[e];
    int i, j, k;

    if (ent->s.renderfx & RF_BEAM)
        return;

    if (!ent->linkcount)
        return; // not linked in anywhere

    if (sent->create_framenum >= sv.framenum) {
        // created this frame. unfortunate for projectiles: they will move only
        // with 1/client->framediv fraction of their normal speed on the client
        return;
    }

    if (state->event == EV_PLAYER_TELEPORT && !Q2PRO_OPTIMIZE(client)) {
        // other clients will lerp from old_origin on EV_PLAYER_TELEPORT...
        VectorCopy(state->origin, state->old_origin);
        return;
    }

    if (sent->create_framenum > sv.framenum - client->framediv) {
        // created between client frames
        VectorCopy(sent->create_origin, state->old_origin);
        return;
    }

    // find the oldest valid origin
    for (i = 0; i < client->framediv - 1; i++) {
        j = sv.framenum - (client->framediv - i);
        k = j & ENT_HISTORY_MASK;
        if (sent->history[k].framenum == j) {
            VectorCopy(sent->history[k].origin, state->old_origin);
            return;
        }
    }

    // no valid old_origin, just use what game provided
}
#endif

static bool SV_EntityVisible(const client_t *client, const server_entity_t *svent, const byte *mask)
{
    if (svent->num_clusters == -1)
        // too many leafs for individual check, go by headnode
        return CM_HeadnodeVisible(CM_NodeNum(client->cm, svent->headnode), mask);

    // check individual leafs
    for (int i = 0; i < svent->num_clusters; i++)
        if (Q_IsBitSet(mask, svent->clusternums[i]))
            return true;

    return false;
}

static bool SV_EntityAttenuatedAway(const vec3_t org, const edict_t *ent)
{
    float dist = Distance(org, ent->s.origin);
    float dist_mult = SOUND_LOOPATTENUATE;

    if (ent->s.loop_attenuation && ent->s.loop_attenuation != ATTN_STATIC)
        dist_mult = ent->s.loop_attenuation * SOUND_LOOPATTENUATE_MULT;

    return (dist - SOUND_FULLVOLUME) * dist_mult > 1.0f;
}

#define IS_MONSTER(ent) \
    ((ent->svflags & (SVF_MONSTER | SVF_DEADMONSTER)) == SVF_MONSTER || (ent->s.renderfx & RF_FRAMELERP))

#define IS_HI_PRIO(ent) \
    (ent->s.number <= sv_client->maxclients || IS_MONSTER(ent) || ent->solid == SOLID_BSP)

#define IS_GIB(ent) \
    (sv_client->csr->extended ? (ent->s.renderfx & RF_LOW_PRIORITY) : (ent->s.effects & (EF_GIB | EF_GREENGIB)))

#define IS_LO_PRIO(ent) \
    (IS_GIB(ent) || (!ent->s.modelindex && !ent->s.effects))

static vec3_t clientorg;

static int entpriocmp(const void *p1, const void *p2)
{
    const edict_t *a = *(const edict_t **)p1;
    const edict_t *b = *(const edict_t **)p2;

    bool hi_a = IS_HI_PRIO(a);
    bool hi_b = IS_HI_PRIO(b);
    if (hi_a != hi_b)
        return hi_b - hi_a;

    bool lo_a = IS_LO_PRIO(a);
    bool lo_b = IS_LO_PRIO(b);
    if (lo_a != lo_b)
        return lo_a - lo_b;

    float dist_a = DistanceSquared(a->s.origin, clientorg);
    float dist_b = DistanceSquared(b->s.origin, clientorg);
    if (dist_a > dist_b)
        return 1;
    return -1;
}

static int entnumcmp(const void *p1, const void *p2)
{
    const edict_t *a = *(const edict_t **)p1;
    const edict_t *b = *(const edict_t **)p2;
    return a->s.number - b->s.number;
}

/*
=============
SV_BuildClientFrame

Decides which entities are going to be visible to the client, and
copies off the playerstat and areabits.
=============
*/
void SV_BuildClientFrame(client_t *client)
{
    int         i, e;
    vec3_t      org;
    edict_t     *ent;
    server_entity_t *svent;
    edict_t     *clent;
    client_frame_t  *frame;
    entity_packed_t *state;
    const mleaf_t   *leaf;
    int         clientarea, clientcluster;
    byte        clientphs[VIS_MAX_BYTES];
    byte        clientpvs[VIS_MAX_BYTES];
    int         max_packet_entities;
    edict_t     *edicts[MAX_EDICTS];
    int         num_edicts;
    qboolean (*visible)(edict_t *, edict_t *) = NULL;
    qboolean (*customize)(edict_t *, edict_t *, customize_entity_t *) = NULL;
    customize_entity_t temp;

    clent = client->edict;
    if (!clent->client)
        return;        // not in game yet

    Q_assert(client->entities);

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];
    frame->number = client->framenum;
    frame->sentTime = com_eventTime; // save it for ping calc later
    frame->latency = -1; // not yet acked

    client->frames_sent++;

    // find the client's PVS
    SV_GetClient_ViewOrg(client, org);
    // Rerelease game doesn't include viewheight in viewoffset, vanilla does
    if (svs.game_type == Q2PROTO_GAME_RERELEASE)
        org[2] += clent->client->ps.pmove.viewheight;

    leaf = CM_PointLeaf(client->cm, org);
    clientarea = leaf->area;
    clientcluster = leaf->cluster;

    // calculate the visible areas
    frame->areabytes = CM_WriteAreaBits(client->cm, frame->areabits, clientarea);
    if (!frame->areabytes) {
        frame->areabits[0] = 255;
        frame->areabytes = 1;
    }

    // grab the current player_state_t
    MSG_PackPlayer(&frame->ps, &clent->client->ps, MSG_PS_RERELEASE);

    // grab the current clientNum
    if (g_features->integer & GMF_CLIENTNUM) {
        frame->clientNum = SV_GetClient_ClientNum(client);
        if (!VALIDATE_CLIENTNUM(client->csr, frame->clientNum)) {
            Com_WPrintf("%s: bad clientNum %d for client %d\n",
                        __func__, frame->clientNum, client->number);
            frame->clientNum = client->number;
        }
    } else {
        frame->clientNum = client->number;
    }

    // limit maximum number of entities in client frame
    max_packet_entities =
        sv_max_packet_entities->integer > 0 ? sv_max_packet_entities->integer :
        MAX_PACKET_ENTITIES;

    if (g_customize_entity) {
        visible = g_customize_entity->EntityVisibleToClient;
        customize = g_customize_entity->CustomizeEntityToClient;
    }

    CM_FatPVS(client->cm, clientpvs, org);
    BSP_ClusterVis(client->cm->cache, clientphs, clientcluster, DVIS_PHS);

    // build up the list of visible entities
    frame->num_entities = 0;
    frame->first_entity = client->next_entity;

    num_edicts = 0;
    for (e = 1; e < client->ge->num_edicts; e++) {
        ent = EDICT_NUM2(client->ge, e);
        svent = &sv.entities[e];

        // ignore entities not in use
        if (!ent->inuse && (g_features->integer & GMF_PROPERINUSE))
            continue;

        // ignore ents without visible models
        if (ent->svflags & SVF_NOCLIENT)
            continue;

        // ignore ents without visible models unless they have an effect
        if (!HAS_EFFECTS(ent))
            continue;

        // ignore gibs if client says so
        if (client->settings[CLS_NOGIBS]) {
            if (ent->s.effects & EF_GIB && !(client->csr->extended && ent->s.effects & EF_ROCKET))
                continue;
            if (ent->s.effects & EF_GREENGIB)
                continue;
        }

        // ignore flares if client says so
        if (client->csr->extended && ent->s.renderfx & RF_FLARE && client->settings[CLS_NOFLARES])
            continue;

        // ignore if not touching a PV leaf
        if (ent != clent && !sv_novis->integer && !(ent->svflags & SVF_NOCULL)) {
            // check area
            if (!CM_AreasConnected(client->cm, clientarea, ent->areanum)) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if (!CM_AreasConnected(client->cm, clientarea, ent->areanum2)) {
                    continue;        // blocked by a door
                }
            }

            // beams just check one point for PHS
            // remaster uses different sound culling rules
            bool beam_cull = ent->s.renderfx & RF_BEAM;
            bool sound_cull = ent->s.sound;

            if (!SV_EntityVisible(client, svent, (beam_cull || sound_cull || (ent->s.renderfx & RF_CASTSHADOW)) ? clientphs : clientpvs))
                continue;

            // don't send sounds if they will be attenuated away
            if (sound_cull) {
                if (SV_EntityAttenuatedAway(org, ent)) {
                    if (!ent->s.modelindex)
                        continue;
                    if (!beam_cull && !SV_EntityVisible(client, svent, clientpvs))
                        continue;
                }
            } else if (!ent->s.modelindex && !(ent->s.renderfx & RF_CASTSHADOW)) {
                // Paril TODO: is this a good idea? seems weird to remove
                // visual effects based on distance if there's no model and
                // no sound...
                if (Distance(org, ent->s.origin) > 400)
                    continue;
            }
        }

        SV_CheckEntityNumber(ent, e);

        // optionally skip it
        if (visible && !visible(clent, ent))
            continue;

        edicts[num_edicts++] = ent;

        if (num_edicts == max_packet_entities && !sv_prioritize_entities->integer)
            break;
    }

    // prioritize entities on overflow
    if (num_edicts > max_packet_entities) {
        VectorCopy(org, clientorg);
        sv_client = client;
        sv_player = client->edict;
        qsort(edicts, num_edicts, sizeof(edicts[0]), entpriocmp);
        sv_client = NULL;
        sv_player = NULL;
        num_edicts = max_packet_entities;
        qsort(edicts, num_edicts, sizeof(edicts[0]), entnumcmp);
    }

    for (i = 0; i < num_edicts; i++) {
        ent = edicts[i];
        e = ent->s.number;

        // add it to the circular client_entities array
        state = &client->entities[client->next_entity & (client->num_entities - 1)];

        // optionally customize it
        if (customize && customize(clent, ent, &temp)) {
            Q_assert(temp.s.number == e);
            MSG_PackEntity(state, &temp.s, true);
        } else {
            MSG_PackEntity(state, &ent->s, true);
        }

#if USE_FPS
        // fix old entity origins for clients not running at
        // full server frame rate
        if (client->framediv != 1)
            fix_old_origin(client, state, ent, e);
#endif

        // clear footsteps
        if (client->settings[CLS_NOFOOTSTEPS] && (state->event == EV_FOOTSTEP
            || (state->event == EV_OTHER_FOOTSTEP || state->event == EV_LADDER_STEP))) {
            state->event = 0;
        }

        // hide POV entity from renderer, unless this is player's own entity
        if (e == frame->clientNum + 1 && ent != clent &&
            (!Q2PRO_OPTIMIZE(client))) {
            state->modelindex = 0;
        }

        if ((!USE_MVD_CLIENT || sv.state != ss_broadcast) && (ent->owner == clent)) {
            // don't mark players missiles as solid
            state->solid = 0;
        } else if (client->esFlags & MSG_ES_LONGSOLID && !client->csr->extended) {
            state->solid = sv.entities[e].solid32;
        }

        frame->num_entities++;
        client->next_entity++;
    }
}
