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
// sv_game.c -- interface to the game dll

#include "server.h"

const game_export_t     *ge;
const game_export_ex_t  *gex;

static void PF_configstring(int index, const char *val);

/*
================
PF_FindIndex

================
*/
static int PF_FindIndex(const char *name, int start, int max, int skip, const char *func)
{
    char *string;
    int i;

    if (!name || !name[0])
        return 0;

    for (i = 1; i < max; i++) {
        if (i == skip) {
            continue;
        }
        string = sv.configstrings[start + i];
        if (!string[0]) {
            break;
        }
        if (!strcmp(string, name)) {
            return i;
        }
    }

    if (i == max) {
        if (g_features->integer & GMF_ALLOW_INDEX_OVERFLOW) {
            Com_DPrintf("%s(%s): overflow\n", func, name);
            return 0;
        }
        Com_Error(ERR_DROP, "%s(%s): overflow", func, name);
    }

    PF_configstring(i + start, name);

    return i;
}

static int PF_ModelIndex(const char *name)
{
    return PF_FindIndex(name, svs.csr.models, svs.csr.max_models, MODELINDEX_PLAYER, __func__);
}

static int PF_SoundIndex(const char *name)
{
    return PF_FindIndex(name, svs.csr.sounds, svs.csr.max_sounds, 0, __func__);
}

static int PF_ImageIndex(const char *name)
{
    return PF_FindIndex(name, svs.csr.images, svs.csr.max_images, 0, __func__);
}

/*
===============
PF_Unicast

Sends the contents of the mutlicast buffer to a single client.
Archived in MVD stream.
===============
*/
static void PF_Unicast(edict_t *ent, qboolean reliable)
{
    client_t    *client;
    int         cmd, flags, clientNum;

    if (!ent) {
        goto clear;
    }

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    clientNum = NUM_FOR_EDICT(ent) - 1;
    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        Com_WPrintf("%s to a non-client %d\n", __func__, clientNum);
        goto clear;
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        goto clear;
    }

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        goto clear;
    }

    cmd = msg_write.data[0];

    flags = 0;
    if (reliable) {
        flags |= MSG_RELIABLE;
    }

    if (cmd == svc_layout || (cmd == svc_configstring && RL16(&msg_write.data[1]) == CS_STATUSBAR)) {
        flags |= MSG_COMPRESS_AUTO;
    }

    SV_ClientAddMessage(client, flags);

    // fix anti-kicking exploit for broken mods
    if (cmd == svc_disconnect) {
        client->drop_hack = true;
        goto clear;
    }

    SV_MvdUnicast(ent, clientNum, reliable);

clear:
    SZ_Clear(&msg_write);
}

/*
=================
PF_bprintf

Sends text to all active clients.
Archived in MVD stream.
=================
*/
static void PF_bprintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;
    int         i;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    SV_MvdBroadcastPrint(level, string);

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string.str = string;
    message.print.string.len = len;
    q2proto_server_multicast_write(Q2P_PROTOCOL_Q2PRO, &svs.server_info, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &message);

    // echo to console
    if (COM_DEDICATED) {
        // mask off high bits
        for (i = 0; i < len; i++)
            string[i] &= 127;
        Com_Printf("%s", string);
    }

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level >= client->messagelevel) {
            SV_ClientAddMessage(client, MSG_RELIABLE);
        }
    }

    SZ_Clear(&msg_write);
}

/*
===============
PF_dprintf

Debug print to server console.
===============
*/
static void PF_dprintf(const char *fmt, ...)
{
    char        msg[MAXPRINTMSG];
    va_list     argptr;

#if USE_SAVEGAMES
    // detect YQ2 game lib by unique first two messages
    if (!svs.gamedetecthack)
        svs.gamedetecthack = 1 + !strcmp(fmt, "Game is starting up.\n");
    else if (svs.gamedetecthack == 2)
        svs.gamedetecthack = 3 + !strcmp(fmt, "Game is %s built on %s.\n");
#endif

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Con_SkipNotify(true);
    Com_Printf("%s", msg);
    Con_SkipNotify(false);
}

/*
===============
PF_cprintf

Print to a single client if the level passes.
Archived in MVD stream.
===============
*/
static void PF_cprintf(edict_t *ent, int level, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         clientNum;
    size_t      len;
    client_t    *client;

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    if (!ent) {
        Com_LPrintf(level == PRINT_CHAT ? PRINT_TALK : PRINT_ALL, "%s", msg);
        return;
    }

    clientNum = NUM_FOR_EDICT(ent) - 1;
    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        Com_Error(ERR_DROP, "%s to a non-client %d", __func__, clientNum);
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_PRINT, .print = {0}};
    message.print.level = level;
    message.print.string.str = msg;
    message.print.string.len = len;
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    if (level >= client->messagelevel) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SV_MvdUnicast(ent, clientNum, true);

    SZ_Clear(&msg_write);
}

/*
===============
PF_centerprintf

Centerprint to a single client.
Archived in MVD stream.
===============
*/
static void PF_centerprintf(edict_t *ent, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         n;
    size_t      len;

    if (!ent) {
        return;
    }

    n = NUM_FOR_EDICT(ent);
    if (n < 1 || n > sv_maxclients->integer) {
        Com_WPrintf("%s to a non-client %d\n", __func__, n - 1);
        return;
    }

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    client_t* client = svs.client_pool + n - 1;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, n - 1);
        return;
    }

    q2proto_svc_message_t message = {.type = Q2P_SVC_CENTERPRINT, .centerprint = {{0}}};
    message.centerprint.message.str = msg;
    message.centerprint.message.len = len;
    q2proto_server_write(&client->q2proto_ctx, (uintptr_t)&client->io_data, &message);

    PF_Unicast(ent, true);
}

/*
===============
PF_error

Abort the server with a game error
===============
*/
static q_noreturn void PF_error(const char *fmt, ...)
{
    char        msg[MAXERRORMSG];
    va_list     argptr;

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Com_Error(ERR_DROP, "Game Error: %s", msg);
}

/*
=================
PF_setmodel

Also sets mins and maxs for inline bmodels
=================
*/
static void PF_setmodel(edict_t *ent, const char *name)
{
    if (!ent || !name)
        Com_Error(ERR_DROP, "PF_setmodel: NULL");

    ent->s.modelindex = PF_ModelIndex(name);

// if it is an inline model, get the size information for it
    if (name[0] == '*') {
        const mmodel_t *mod = CM_InlineModel(&sv.cm, name);
        VectorCopy(mod->mins, ent->mins);
        VectorCopy(mod->maxs, ent->maxs);
        PF_LinkEdict(ent);
    }
}

/*
===============
PF_configstring

If game is actively running, broadcasts configstring change.
Archived in MVD stream.
===============
*/
static void PF_configstring(int index, const char *val)
{
    size_t len, maxlen;
    client_t *client;
    char *dst;

    if (index < 0 || index >= svs.csr.end)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    if (sv.state == ss_dead) {
        Com_WPrintf("%s: not yet initialized\n", __func__);
        return;
    }

    if (!val)
        val = "";

    // error out entirely if it exceedes array bounds
    len = strlen(val);
    maxlen = (svs.csr.end - index) * MAX_QPATH;
    if (len >= maxlen) {
        Com_Error(ERR_DROP,
                  "%s: index %d overflowed: %zu > %zu",
                  __func__, index, len, maxlen - 1);
    }

    // print a warning and truncate everything else
    maxlen = Com_ConfigstringSize(&svs.csr, index);
    if (len >= maxlen) {
        Com_WPrintf(
            "%s: index %d overflowed: %zu > %zu\n",
            __func__, index, len, maxlen - 1);
        len = maxlen - 1;
    }

    dst = sv.configstrings[index];
    if (!strncmp(dst, val, maxlen)) {
        return;
    }

    // change the string in sv
    memcpy(dst, val, len);
    dst[len] = 0;

    if (sv.state == ss_loading) {
        return;
    }

    SV_MvdConfigstring(index, val, len);

    // send the update to everyone
    q2proto_svc_message_t message = {.type = Q2P_SVC_CONFIGSTRING, .configstring = {0}};
    message.configstring.index = index;
    message.configstring.value.str = val;
    message.configstring.value.len = len;
    q2proto_server_multicast_write(Q2P_PROTOCOL_Q2PRO, &svs.server_info, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &message);

    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

static const char *PF_GetConfigstring(int index)
{
    if (index < 0 || index >= svs.csr.end)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    return sv.configstrings[index];
}

static void PF_WriteFloat(float f)
{
    Com_Error(ERR_DROP, "PF_WriteFloat not implemented");
}

static void PF_WritePos(const vec3_t pos)
{
    q2proto_server_write_pos(Q2P_PROTOCOL_Q2PRO, &svs.server_info, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, pos);
}

static qboolean PF_inVIS(const vec3_t p1, const vec3_t p2, vis_t vis)
{
    const mleaf_t *leaf1, *leaf2;
    byte mask[VIS_MAX_BYTES];

    leaf1 = CM_PointLeaf(&sv.cm, p1);
    BSP_ClusterVis(sv.cm.cache, mask, leaf1->cluster, vis & VIS_PHS);

    leaf2 = CM_PointLeaf(&sv.cm, p2);
    if (leaf2->cluster == -1)
        return false;
    if (!Q_IsBitSet(mask, leaf2->cluster))
        return false;
    if (vis & VIS_NOAREAS)
        return true;
    if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
        return false;       // a door blocks it
    return true;
}

/*
=================
PF_inPVS

Also checks portalareas so that doors block sight
=================
*/
static qboolean PF_inPVS(const vec3_t p1, const vec3_t p2)
{
    return PF_inVIS(p1, p2, VIS_PVS);
}

/*
=================
PF_inPHS

Also checks portalareas so that doors block sound
=================
*/
static qboolean PF_inPHS(const vec3_t p1, const vec3_t p2)
{
    return PF_inVIS(p1, p2, VIS_PHS);
}

/*
==================
SV_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

If channel & 8, the sound will be sent to everyone, not just
things in the PHS.

FIXME: if entity isn't in PHS, they must be forced to be sent or
have the origin explicitly sent.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

Timeofs can range from 0.0 to 0.1 to cause sounds to be started
later in the frame than they normally would.

If origin is NULL, the origin is determined from the entity origin
or the midpoint of the entity box for bmodels.
==================
*/
static void SV_StartSound(const vec3_t origin, edict_t *edict,
                          int channel, int soundindex, float volume,
                          float attenuation, float timeofs)
{
    vec3_t      origin_v;
    client_t    *client;
    byte        mask[VIS_MAX_BYTES];
    const mleaf_t       *leaf1, *leaf2;
    q2proto_sound_t snd = {0};
    message_packet_t    *msg;
    bool        force_pos;

    if (!edict)
        Com_Error(ERR_DROP, "%s: edict = NULL", __func__);
    if (volume < 0 || volume > 1)
        Com_Error(ERR_DROP, "%s: volume = %f", __func__, volume);
    if (attenuation < 0 || attenuation > 4)
        Com_Error(ERR_DROP, "%s: attenuation = %f", __func__, attenuation);
    if (timeofs < 0 || timeofs > 0.255f)
        Com_Error(ERR_DROP, "%s: timeofs = %f", __func__, timeofs);
    if (soundindex < 0 || soundindex >= svs.csr.max_sounds)
        Com_Error(ERR_DROP, "%s: soundindex = %d", __func__, soundindex);

    // send origin for invisible entities
    // the origin can also be explicitly set
    force_pos = (edict->svflags & SVF_NOCLIENT) || origin;

    // use the entity origin unless it is a bmodel or explicitly specified
    if (!origin) {
        if (edict->solid == SOLID_BSP) {
            VectorAvg(edict->mins, edict->maxs, origin_v);
            VectorAdd(origin_v, edict->s.origin, origin_v);
            origin = origin_v;
        } else {
            origin = edict->s.origin;
        }
    }

    snd.index = soundindex;
    // always send the entity number for channel overrides
    snd.has_entity_channel = true;
    snd.entity = NUM_FOR_EDICT(edict);
    snd.channel = channel;
    snd.has_position = true;
    VectorCopy(origin, snd.pos);
    snd.volume = volume;
    snd.attenuation = attenuation;
    snd.timeofs = timeofs;

    // prepare multicast message
    q2proto_svc_message_t sound_msg = {.type = Q2P_SVC_SOUND, .sound = {0}};
    q2proto_sound_encode_message(&snd, &sound_msg.sound);

    q2proto_server_multicast_write(Q2P_PROTOCOL_Q2PRO, &svs.server_info, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &sound_msg);

    // if the sound doesn't attenuate, send it to everyone
    // (global radio chatter, voiceovers, etc)
    if (attenuation == ATTN_NONE)
        channel |= CHAN_NO_PHS_ADD;

    // multicast if force sending origin
    if (force_pos) {
        multicast_t to = MULTICAST_PHS;
        if (channel & CHAN_NO_PHS_ADD)
            to = MULTICAST_ALL;
        if (channel & CHAN_RELIABLE)
            to += MULTICAST_ALL_R;
        SV_Multicast(origin, to);
        return;
    }

    leaf1 = NULL;
    if (!(channel & CHAN_NO_PHS_ADD)) {
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        BSP_ClusterVis(sv.cm.cache, mask, leaf1->cluster, DVIS_PHS);
    }

    // decide per client if origin needs to be sent
    FOR_EACH_CLIENT(client) {
        // do not send sounds to connecting clients
        if (!CLIENT_ACTIVE(client)) {
            continue;
        }

        // PHS cull this sound
        if (!(channel & CHAN_NO_PHS_ADD)) {
            leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask, leaf2->cluster))
                continue;
        }

        // reliable sounds will always have position explicitly set,
        // as no one guarantees reliables to be delivered in time
        if (channel & CHAN_RELIABLE) {
            SV_ClientAddMessage(client, MSG_RELIABLE);
            continue;
        }

        // default client doesn't know that bmodels have weird origins
        if (edict->solid == SOLID_BSP && client->protocol == PROTOCOL_VERSION_DEFAULT) {
            SV_ClientAddMessage(client, 0);
            continue;
        }

        if (LIST_EMPTY(&client->msg_free_list)) {
            Com_WPrintf("%s: %s: out of message slots\n",
                        __func__, client->name);
            continue;
        }

        msg = LIST_FIRST(message_packet_t, &client->msg_free_list, entry);

        msg->cursize = SOUND_PACKET;
        msg->sound = sound_msg.sound;
        msg->sound.flags &= ~SND_POS; // SND_POS, will be set, if necessary, by emit_snd()

        List_Remove(&msg->entry);
        List_Append(&client->msg_unreliable_list, &msg->entry);
        client->msg_unreliable_bytes += msg_write.cursize;
    }

    // clear multicast buffer
    SZ_Clear(&msg_write);

    SV_MvdStartSound(snd.entity, channel, sound_msg.sound.flags, soundindex, sound_msg.sound.volume, sound_msg.sound.attenuation, sound_msg.sound.timeofs);
}

static void PF_StartSound(edict_t *entity, int channel,
                          int soundindex, float volume,
                          float attenuation, float timeofs)
{
    if (!entity)
        return;
    SV_StartSound(NULL, entity, channel, soundindex, volume, attenuation, timeofs);
}

// TODO: support origin; add range checks?
static void PF_LocalSound(edict_t *target, const vec3_t origin,
                          edict_t *entity, int channel,
                          int soundindex, float volume,
                          float attenuation, float timeofs)
{
    int entnum = NUM_FOR_EDICT(target);

    q2proto_sound_t snd = {.index = soundindex};
    // always send the entity number for channel overrides
    snd.has_entity_channel = true;
    snd.entity = entnum;
    snd.channel = channel;
    snd.volume = volume;
    snd.attenuation = attenuation;
    snd.timeofs = timeofs;

    q2proto_svc_message_t message = {.type = Q2P_SVC_SOUND, .sound = {0}};
    q2proto_sound_encode_message(&snd, &message.sound);

    int clientNum = entnum - 1;
    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        Com_WPrintf("%s to a non-client %d\n", __func__, clientNum);
        return;
    }

    client_t *client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        return;
    }

    q2proto_server_write(&client->q2proto_ctx, Q2PROTO_IOARG_SERVER_WRITE_MULTICAST, &message);

    PF_Unicast(target, !!(channel & CHAN_RELIABLE));
}

void PF_Pmove(void *pm)
{
    const pmoveParams_t *pmp = sv_client ? &sv_client->pmp : &svs.pmp;

    if (IS_NEW_GAME_API)
        PmoveNew(pm, pmp);
    else
        PmoveOld(pm, pmp);
}

static cvar_t *PF_cvar(const char *name, const char *value, int flags)
{
    if (flags & CVAR_EXTENDED_MASK) {
        Com_WPrintf("Game attemped to set extended flags on '%s', masked out.\n", name);
        flags &= ~CVAR_EXTENDED_MASK;
    }

    return Cvar_Get(name, value, flags | CVAR_GAME);
}

static void PF_AddCommandString(const char *string)
{
#if USE_CLIENT
    if (!strcmp(string, "menu_loadgame\n"))
        string = "pushmenu loadgame\n";
#endif
    Cbuf_AddText(&cmd_buffer, string);
}

static void PF_SetAreaPortalState(int portalnum, qboolean open)
{
    CM_SetAreaPortalState(&sv.cm, portalnum, open);
}

static qboolean PF_AreasConnected(int area1, int area2)
{
    return CM_AreasConnected(&sv.cm, area1, area2);
}

static void *PF_TagMalloc(unsigned size, unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    return Z_TagMallocz(size, tag + TAG_MAX);
}

static void PF_FreeTags(unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    Z_FreeTags(tag + TAG_MAX);
}

static void PF_DebugGraph(float value, int color)
{
}

static int PF_LoadFile(const char *path, void **buffer, unsigned flags, unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    return FS_LoadFileEx(path, buffer, flags, tag + TAG_MAX);
}

static void *PF_TagRealloc(void *ptr, size_t size)
{
    if (!ptr && size) {
        Com_Error(ERR_DROP, "%s: untagged allocation not allowed", __func__);
    }
    return Z_Realloc(ptr, size);
}

//==============================================

static const game_import_t game_import = {
    .multicast = SV_Multicast,
    .unicast = PF_Unicast,
    .bprintf = PF_bprintf,
    .dprintf = PF_dprintf,
    .cprintf = PF_cprintf,
    .centerprintf = PF_centerprintf,
    .error = PF_error,

    .linkentity = PF_LinkEdict,
    .unlinkentity = PF_UnlinkEdict,
    .BoxEdicts = SV_AreaEdicts,
    .trace = SV_Trace,
    .pointcontents = SV_PointContents,
    .setmodel = PF_setmodel,
    .inPVS = PF_inPVS,
    .inPHS = PF_inPHS,
    .Pmove = PF_Pmove,

    .modelindex = PF_ModelIndex,
    .soundindex = PF_SoundIndex,
    .imageindex = PF_ImageIndex,

    .configstring = PF_configstring,
    .sound = PF_StartSound,
    .positioned_sound = SV_StartSound,

    .WriteChar = MSG_WriteChar,
    .WriteByte = MSG_WriteByte,
    .WriteShort = MSG_WriteShort,
    .WriteLong = MSG_WriteLong,
    .WriteFloat = PF_WriteFloat,
    .WriteString = MSG_WriteString,
    .WritePosition = PF_WritePos,
    .WriteDir = MSG_WriteDir,
    .WriteAngle = MSG_WriteAngle,

    .TagMalloc = PF_TagMalloc,
    .TagFree = Z_Free,
    .FreeTags = PF_FreeTags,

    .cvar = PF_cvar,
    .cvar_set = Cvar_UserSet,
    .cvar_forceset = Cvar_Set,

    .argc = Cmd_Argc,
    .argv = Cmd_Argv,
    .args = Cmd_RawArgs,
    .AddCommandString = PF_AddCommandString,

    .DebugGraph = PF_DebugGraph,
    .SetAreaPortalState = PF_SetAreaPortalState,
    .AreasConnected = PF_AreasConnected,
};

static const filesystem_api_v1_t filesystem_api_v1 = {
    .OpenFile = FS_OpenFile,
    .CloseFile = FS_CloseFile,
    .LoadFile = PF_LoadFile,

    .ReadFile = FS_Read,
    .WriteFile = FS_Write,
    .FlushFile = FS_Flush,
    .TellFile = FS_Tell,
    .SeekFile = FS_Seek,
    .ReadLine = FS_ReadLine,

    .ListFiles = FS_ListFiles,
    .FreeFileList = FS_FreeList,

    .ErrorString = Q_ErrorString,
};

#if USE_REF && USE_DEBUG
static const debug_draw_api_v1_t debug_draw_api_v1 = {
    .ClearDebugLines = R_ClearDebugLines,
    .AddDebugLine = R_AddDebugLine,
    .AddDebugPoint = R_AddDebugPoint,
    .AddDebugAxis = R_AddDebugAxis,
    .AddDebugBounds = R_AddDebugBounds,
    .AddDebugSphere = R_AddDebugSphere,
    .AddDebugCircle = R_AddDebugCircle,
    .AddDebugCylinder = R_AddDebugCylinder,
    .AddDebugArrow = R_AddDebugArrow,
    .AddDebugCurveArrow = R_AddDebugCurveArrow,
    .AddDebugText = R_AddDebugText,
};
#endif

static void *PF_GetExtension(const char *name)
{
    if (!name)
        return NULL;

    if (!strcmp(name, FILESYSTEM_API_V1))
        return (void *)&filesystem_api_v1;

#if USE_REF && USE_DEBUG
    if (!strcmp(name, DEBUG_DRAW_API_V1) && !dedicated->integer)
        return (void *)&debug_draw_api_v1;
#endif

    return NULL;
}

static const game_import_ex_t game_import_ex = {
    .apiversion = GAME_API_VERSION_EX,
    .structsize = sizeof(game_import_ex),

    .local_sound = PF_LocalSound,
    .get_configstring = PF_GetConfigstring,
    .clip = SV_Clip,
    .inVIS = PF_inVIS,

    .GetExtension = PF_GetExtension,
    .TagRealloc = PF_TagRealloc,
};

static void *game_library;

/*
===============
SV_ShutdownGameProgs

Called when either the entire server is being killed, or
it is changing to a different game directory.
===============
*/
void SV_ShutdownGameProgs(void)
{
    gex = NULL;
    if (ge) {
        ge->Shutdown();
        ge = NULL;
    }
    if (game_library) {
        Sys_FreeLibrary(game_library);
        game_library = NULL;
    }
    Cvar_Set("g_features", "0");

    Z_LeakTest(TAG_FREE);
}

static void *SV_LoadGameLibraryFrom(const char *path)
{
    void *entry;

    entry = Sys_LoadLibrary(path, "GetGameAPI", &game_library);
    if (!entry)
        Com_EPrintf("Failed to load game library: %s\n", Com_GetLastError());
    else
        Com_Printf("Loaded game library from %s\n", path);

    return entry;
}

static void *SV_LoadGameLibrary(const char *libdir, const char *gamedir)
{
    char path[MAX_OSPATH];

    if (Q_concat(path, sizeof(path), libdir,
                 PATH_SEP_STRING, gamedir, PATH_SEP_STRING,
                 "game" CPUSTRING LIBSUFFIX) >= sizeof(path)) {
        Com_EPrintf("Game library path length exceeded\n");
        return NULL;
    }

    if (os_access(path, X_OK)) {
        Com_Printf("Can't access %s: %s\n", path, strerror(errno));
        return NULL;
    }

    return SV_LoadGameLibraryFrom(path);
}

/*
===============
SV_InitGameProgs

Init the game subsystem for a new map
===============
*/
void SV_InitGameProgs(void)
{
    game_import_t   import;
    game_entry_t    entry = NULL;

    // unload anything we have now
    SV_ShutdownGameProgs();

    // for debugging or `proxy' mods
    if (sys_forcegamelib->string[0])
        entry = SV_LoadGameLibraryFrom(sys_forcegamelib->string);

    // try game first
    if (!entry && fs_game->string[0]) {
        if (sys_homedir->string[0])
            entry = SV_LoadGameLibrary(sys_homedir->string, fs_game->string);
        if (!entry)
            entry = SV_LoadGameLibrary(sys_libdir->string, fs_game->string);
    }

    // then try baseq2
    if (!entry) {
        if (sys_homedir->string[0])
            entry = SV_LoadGameLibrary(sys_homedir->string, BASEGAME);
        if (!entry)
            entry = SV_LoadGameLibrary(sys_libdir->string, BASEGAME);
    }

    // all paths failed
    if (!entry)
        Com_Error(ERR_DROP, "Failed to load game library");

    // load a new game dll
    import = game_import;

    ge = entry(&import);
    if (!ge) {
        Com_Error(ERR_DROP, "Game library returned NULL exports");
    }

    Com_DPrintf("Game API version: %d\n", ge->apiversion);

    if (ge->apiversion != GAME_API_VERSION_OLD && ge->apiversion != GAME_API_VERSION_NEW) {
        Com_Error(ERR_DROP, "Game library is version %d, expected %d or %d",
                  ge->apiversion, GAME_API_VERSION_OLD, GAME_API_VERSION_NEW);
    }

    // get extended api if present
    game_entry_ex_t entry_ex = Sys_GetProcAddress(game_library, "GetGameAPIEx");
    if (entry_ex) {
        gex = entry_ex(&game_import_ex);
        if (gex && gex->apiversion >= GAME_API_VERSION_EX_MINIMUM)
            Com_DPrintf("Game supports Q2PRO extended API version %d.\n", gex->apiversion);
        else
            gex = NULL;
    }

    // initialize
    ge->Init();

    if (g_features->integer & GMF_PROTOCOL_EXTENSIONS) {
        Com_Printf("Game supports Q2PRO protocol extensions.\n");
        svs.csr = cs_remap_new;
    }

    // sanitize edict_size
    unsigned min_size = svs.csr.extended ? sizeof(edict_t) : q_offsetof(edict_t, x);
    unsigned max_size = INT_MAX / svs.csr.max_edicts;

    if (ge->edict_size < min_size || ge->edict_size > max_size || ge->edict_size % q_alignof(edict_t)) {
        Com_Error(ERR_DROP, "Game library returned bad size of edict_t");
    }

    // sanitize max_edicts
    if (ge->max_edicts <= sv_maxclients->integer || ge->max_edicts > svs.csr.max_edicts) {
        Com_Error(ERR_DROP, "Game library returned bad number of max_edicts");
    }

    if (svs.csr.extended)
        svs.server_info.game_type = IS_NEW_GAME_API ? Q2PROTO_GAME_Q2PRO_EXTENDED_V2 : Q2PROTO_GAME_Q2PRO_EXTENDED;
    else
        svs.server_info.game_type = Q2PROTO_GAME_VANILLA;
    svs.server_info.default_packet_length = MAX_PACKETLEN_WRITABLE_DEFAULT;
}
