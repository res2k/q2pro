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

q2protoio_ioarg_t default_q2protoio_ioarg = {.sz_read = &msg_read, .sz_write = &msg_write, .max_msg_len = 1384 /* conservative default */ };

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

void q2protoio_write_u8(uintptr_t io_arg, uint8_t x)
{
    MSG_WriteByte(x);
}

void q2protoio_write_u16(uintptr_t io_arg, uint16_t x)
{
    MSG_WriteShort(x);
}

void q2protoio_write_u32(uintptr_t io_arg, uint32_t x)
{
    MSG_WriteLong(x);
}

void q2protoio_write_u64(uintptr_t io_arg, uint64_t x)
{
    MSG_WriteLong64(x);
}

void* q2protoio_write_reserve_raw(uintptr_t io_arg, size_t size)
{
    return SZ_GetSpace(&msg_write, size);
}

void q2protoio_write_raw(uintptr_t io_arg, const void* data, size_t size, size_t *written)
{
    size_t buf_remaining = msg_write.maxsize - msg_write.cursize;
    size_t write_size = written ? min(buf_remaining, size) : size;
    void* p = SZ_GetSpace(&msg_write, write_size);
    memcpy(p, data, write_size);
    if (written)
        *written = write_size;
}

size_t q2protoio_write_available(uintptr_t io_arg)
{
    const q2protoio_ioarg_t *io_data = (const q2protoio_ioarg_t *)io_arg;
    sizebuf_t *sz = io_data->sz_write;
    // FIXME: does not take possibility of compression into account ... (see maybe_flush_msg)
    return io_data->max_msg_len - min(sz->cursize, io_data->max_msg_len);
}

q2proto_error_t q2protoerr_client_read(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    Com_Error(ERR_DROP, "%s", buf);
}

q2proto_error_t q2protoerr_server_write(uintptr_t io_arg, q2proto_error_t err, const char *msg, ...)
{
    char buf[256];
    va_list argptr;

    va_start(argptr, msg);
    Q_vsnprintf(buf, sizeof(buf), msg, argptr);
    va_end(argptr);

    Com_EPrintf("server write error: %s\n", buf);
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
