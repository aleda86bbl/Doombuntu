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
//	WAD I/O functions.
//
//-----------------------------------------------------------------------------

#pragma once

#ifdef __cplusplus
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdint.h>
#include <string>

struct FileLumpType
{
    int32_t filePos;
    int32_t size;
    std::array<char, 8> name;

    FileLumpType(int32_t filePosIn, int32_t sizeIn, std::string const &nameIn)
        : filePos(filePosIn), size(sizeIn), name([&]() {
              std::array<char, 8> tempArray{};
              std::copy_n(nameIn.begin(), std::min(nameIn.size(), tempArray.size()), tempArray.begin());
              return tempArray;
          }())
    {
    }
    FileLumpType() = default;
};

struct WADInfoType
{
    std::array<char, 4> identification;
    int numLumps;
    int infoTableOfs;
};

struct LumpInfoType
{
    std::array<char, 8> name;
    bool reloadLump;
    int position;
    int size;
    std::filesystem::path sourcePath;
};

int W_CheckNumForName(std::string_view name);
int W_GetNumForName(std::string_view name);

extern "C"
{
#endif

#ifdef __GNUG__
#pragma interface
#endif

    //
    // TYPES
    //
    typedef struct
    {
        // Should be "IWAD" or "PWAD".
        char identification[4];
        int numlumps;
        int infotableofs;

    } wadinfo_t;

    /*typedef struct
    {
        int			filepos;
        int			size;
        char		name[8];

    } filelump_t;*/

    //
    // WADFILE I/O related stuff.
    //
    typedef struct
    {
        char name[8];
        int handle;
        int position;
        int size;
    } lumpinfo_t;


    void W_InitMultipleFilesC(char **filenames);
    void W_Reload(void);
    void W_CopyLumpName(int lump, char name[9]);

    int W_CheckNumForNameC(char const *name);
    int W_GetNumForNameC(char const *name);

    int W_LumpLength(int lump);
    void W_ReadLump(int lump, void *dest);

    void *W_CacheLumpNum(int lump, int tag);
    void *W_CacheLumpName(char const *name, int tag);

#ifdef __cplusplus
}
#endif

//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
