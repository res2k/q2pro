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
#include "common/intreadwrite.h"
#include "common/msg.h"
#include "common/q2proto_shared.h"

#include "q2proto/q2proto.h"

q2protoio_ioarg_t default_q2protoio_ioarg = {.sz_read = &msg_read};

static byte* io_read_data(uintptr_t io_arg, size_t len, size_t *readcount)
{
    Q_assert(io_arg == _Q2PROTO_IOARG_DEFAULT);
    sizebuf_t *sz = ((q2protoio_ioarg_t*)io_arg)->sz_read;

    if (readcount) {
        len = min(len, sz->cursize - sz->readcount);
        *readcount = len;
        return SZ_ReadData(sz, len);
    } else
        return (byte*)SZ_ReadData(sz, len);
}

uint8_t q2protoio_read_u8(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 1, NULL);
    return buf ? (uint8_t)buf[0] : (uint8_t)-1;
}

uint16_t q2protoio_read_u16(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 2, NULL);
    return buf ? (uint16_t)RL16(buf) : (uint16_t)-1;
}

uint32_t q2protoio_read_u32(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 4, NULL);
    return buf ? (uint32_t)RL32(buf) : (uint32_t)-1;
}

uint64_t q2protoio_read_u64(uintptr_t io_arg)
{
    byte *buf = io_read_data(io_arg, 8, NULL);
    return buf ? (uint64_t)RL64(buf) : (uint64_t)-1;
}

q2proto_string_t q2protoio_read_string(uintptr_t io_arg)
{
    q2proto_string_t str = {.str = NULL, .len = 0};
    str.str = (const char*)io_read_data(io_arg, 0, NULL);
    while (1) {
        byte *c = io_read_data(io_arg, 1, NULL);
        if (!c || *c == 0) {
            break;
        }
        str.len++;
    }
    return str;
}

const void* q2protoio_read_raw(uintptr_t io_arg, size_t size, size_t* readcount)
{
    return io_read_data(io_arg, size, readcount);
}

bool nonfatal_client_read_errors = false;

q2proto_error_t q2protoerr_client_read(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    if (nonfatal_client_read_errors)
        Com_WPrintf("%s\n", buf);
    else
        Com_Error(ERR_DROP, "%s", buf);
    return err;
}

#if Q2PROTO_SHOWNET
extern cvar_t   *cl_shownet;

bool q2protodbg_shownet_check(uintptr_t io_arg, int level)
{
    return cl_shownet->integer > level;
}

void q2protodbg_shownet(uintptr_t io_arg, int level, int offset, const char *msg, ...)
{
    if (cl_shownet->integer > level)
    {
        char buf[256];
        va_list argptr;

        va_start(argptr, msg);
        Q_vsnprintf(buf, sizeof(buf), msg, argptr);
        va_end(argptr);

        Com_LPrintf(PRINT_DEVELOPER, "%3u:%s\n", msg_read.readcount + offset, buf);
    }
}
#endif
