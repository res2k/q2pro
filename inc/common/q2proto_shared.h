/*
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

#pragma once

#include "q2proto/q2proto.h"

#if USE_ZLIB
#include <zlib.h>

struct q2protoio_deflate_args_s
{
    /// Buffer to store deflated data
    byte *z_buffer;
    /// Size of deflated data buffer
    unsigned z_buffer_size;
    /// Deflate stream
    z_streamp z;
};
#endif // USE_ZLIB

typedef struct q2protoio_ioarg_s {
    sizebuf_t *sz_read;
    sizebuf_t *sz_write;
    size_t max_msg_len;
    struct q2protoio_deflate_args_s *deflate;
} q2protoio_ioarg_t;

extern q2protoio_ioarg_t default_q2protoio_ioarg;

#define _Q2PROTO_IOARG_DEFAULT      ((uintptr_t)&default_q2protoio_ioarg)

#if USE_CLIENT || USE_SERVER
#define Q2PROTO_IOARG_SERVER_READ               _Q2PROTO_IOARG_DEFAULT
#define Q2PROTO_IOARG_SERVER_WRITE_MULTICAST    _Q2PROTO_IOARG_DEFAULT
#endif

#if USE_CLIENT
#define Q2PROTO_IOARG_CLIENT_READ   _Q2PROTO_IOARG_DEFAULT
#define Q2PROTO_IOARG_CLIENT_WRITE  _Q2PROTO_IOARG_DEFAULT
#endif
