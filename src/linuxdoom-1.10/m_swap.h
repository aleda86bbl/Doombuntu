// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	Endianess handling, swapping 16bit and 32bit.
//
//-----------------------------------------------------------------------------

#ifndef __M_SWAP__
#define __M_SWAP__

#ifdef __cplusplus

#include <bit>
#include <cstdint>
#include <type_traits>
#include <concepts>

template <std::integral T> constexpr T fromLittleEndian(T value)
{
    static_assert(sizeof(T) == 2 || sizeof(T) == 4, "Only 16-bit and 32-bit integers are supported");

    if constexpr (std::endian::native == std::endian::little)
    {
        return value;
    }
    else
    {
        return std::byteswap(value); // C++23
    }
}

extern "C"
{
#endif

#ifdef __GNUG__
#pragma interface
#endif

// Endianess handling.
// WAD files are stored little endian.
#ifdef __BIG_ENDIAN__
    short SwapSHORT(short);
    long SwapLONG(long);
#define SHORT(x) ((short)SwapSHORT((unsigned short)(x)))
#define LONG(x) ((long)SwapLONG((unsigned long)(x)))
#else
#define SHORT(x) (x)
#define LONG(x) (x)
#endif

#ifdef __cplusplus
}
#endif
#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
