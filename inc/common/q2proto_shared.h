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

typedef struct q2protoio_ioarg_s {
    sizebuf_t *sz_read;
} q2protoio_ioarg_t;

extern q2protoio_ioarg_t default_q2protoio_ioarg;

#define _Q2PROTO_IOARG_DEFAULT      ((uintptr_t)&default_q2protoio_ioarg)

#if USE_CLIENT
#define Q2PROTO_IOARG_CLIENT_READ   _Q2PROTO_IOARG_DEFAULT
#endif

extern bool nonfatal_client_read_errors;
