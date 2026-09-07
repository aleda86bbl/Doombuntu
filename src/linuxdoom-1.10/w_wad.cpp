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
// $Log:$
//
// DESCRIPTION:
//	Handles WAD file header, directory, lump I/O.
//
//-----------------------------------------------------------------------------

static const char rcsid[] = "$Id: w_wad.c,v 1.5 1997/02/03 16:47:57 b1 Exp $";

#ifdef NORMALUNIX
#include <algorithm>
#include <alloca.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <ranges>
#include <string.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#define O_BINARY 0
#endif

#include "i_system.h"
#include "m_swap.h"
#include "z_zone.h"

#include "w_wad.hpp"

//
// GLOBALS
//

// Location of each lump on disk.
std::vector<LumpInfoType> lumpInfo;
int numLumps;
std::vector<void *> lumpCache;

namespace
{
    constexpr std::size_t wadNameLength = 8;
    std::string_view WADNameView(char const * name)
    {
        std::size_t length = 0;
        while (length < wadNameLength && name[length] != '\0')
        {
            ++length;
        }
        return {name, length};
    }
}

//
// LUMP BASED ROUTINES.
//

//
// W_AddFile
// All files are optional, but at least one file must be
//  found (PWAD, if all required lumps are present).
// Files with a .wad extension are wadlink files
//  with multiple lumps.
// Other files are single lumps with the base filename
//  for the lump name.
//
// If filename starts with a tilde, the file is handled
//  specially to allow map reloads.
// But: the reload feature is a fragile hack...

int reloadLump;
std::string_view reloadName;

[[nodiscard]] bool iequals(std::string_view s1, std::string_view s2) noexcept
{
    if (s1.size() != s2.size())
        return false;

    auto toLower = [](unsigned char c) noexcept { return static_cast<char>(std::tolower(c)); };

    return std::ranges::equal(s1 | std::views::transform(toLower), s2 | std::views::transform(toLower));
}

void W_AddFile(std::string_view filenameIn)
{
    if (filenameIn.starts_with('~'))
    {
        filenameIn = filenameIn.substr(1);
        reloadName = filenameIn;
        reloadLump = numLumps;
    }

    auto filename = std::filesystem::path(filenameIn);
    std::ifstream fileStream(filename, std::ios::in | std::ios::binary);
    if (!fileStream.is_open())
    {
        std::cerr << " couldn't open " << filename << std::endl;
        return;
    }
    std::cout << " adding " << filename << std::endl;

    int startLump = numLumps;
    std::vector<FileLumpType> fileInfo;
    if (!iequals(filename.extension().string(), ".wad"))
    {
        fileInfo.emplace_back(0, std::filesystem::file_size(filename), filename.stem().string().substr(0, 8));
        numLumps++;
    }
    else
    {
        WADInfoType header;
        fileStream.read(reinterpret_cast<char *>(&header), sizeof(WADInfoType));

        auto identification = std::string_view{header.identification.data(), header.identification.size()};

        bool const isIWAD = identification == "IWAD";
        bool const isPWAD = identification == "PWAD";

        if (!isIWAD && !isPWAD)
        {
            I_Error("Wad file %.*s doesn't have IWAD or PWAD id\n", static_cast<int>(filenameIn.size()), filenameIn.data());
        }
        header.numLumps = fromLittleEndian(header.numLumps);
        header.infoTableOfs = fromLittleEndian(header.infoTableOfs);
        fileInfo.resize(header.numLumps);
        fileStream.seekg(header.infoTableOfs, std::ios::beg);
        std::streamsize length = header.numLumps * sizeof(FileLumpType);
        if (!fileInfo.empty())
        {
            fileStream.read(reinterpret_cast<char *>(&fileInfo[0]), length);
            numLumps += header.numLumps;
        }
    }

    lumpInfo.resize(numLumps);
    auto lumpStartItr = lumpInfo.begin() + startLump;
    auto lumpEndItr = lumpInfo.begin() + numLumps;
    auto lumpRangeItr = std::ranges::subrange(lumpStartItr, lumpEndItr);

    auto lumpFileInfoViews = std::views::zip(lumpRangeItr, fileInfo);

    bool reloadLump = !reloadName.empty();
    for (auto [lumpP, fileInfoI] : lumpFileInfoViews)
    {
        lumpP.reloadLump = reloadLump;
        lumpP.position = fromLittleEndian(fileInfoI.filePos);
        lumpP.size = fromLittleEndian(fileInfoI.size);
        lumpP.name = fileInfoI.name;
        lumpP.sourcePath = filename;
    }

    if (reloadLump)
        fileStream.close();
}

void W_Reload(void)
{
    if (reloadName.empty())
    {
        return;
    }

    auto filename = std::filesystem::path(reloadName);
    std::ifstream fileStream(filename, std::ios::in | std::ios::binary);
    if (!fileStream.is_open())
    {
        std::cerr << " couldn't open " << filename << std::endl;
        return;
    }
    WADInfoType header;
    fileStream.read(reinterpret_cast<char *>(&header), sizeof(WADInfoType));
    int lumpCount = fromLittleEndian(header.numLumps);
    header.infoTableOfs = fromLittleEndian(header.infoTableOfs);

    std::vector<FileLumpType> fileInfo;
    fileInfo.resize(lumpCount);
    fileStream.seekg(header.infoTableOfs, std::ios::beg);
    std::streamsize length = lumpCount * sizeof(FileLumpType);
    fileStream.read(reinterpret_cast<char *>(&fileInfo[0]), length);

    auto lumpStartItr = lumpInfo.begin() + reloadLump;
    auto lumpEndItr = lumpInfo.begin() + reloadLump + lumpCount;
    auto lumpRangeItr = std::ranges::subrange(lumpStartItr, lumpEndItr);

    auto lumpCacheStartItr = lumpCache.begin() + reloadLump;
    auto lumpCacheEndItr = lumpCache.begin() + reloadLump + lumpCount;
    auto lumpCacheRangeItr = std::ranges::subrange(lumpCacheStartItr, lumpCacheEndItr);

    auto lumpCacheFileInfoViews = std::views::zip(lumpRangeItr, lumpCacheRangeItr, fileInfo);

    for (auto [lumpP, lumpC, fileInfoI] : lumpCacheFileInfoViews)
    {
        if (lumpC)
        {
            Z_Free(lumpC);
        }

        lumpP.position = fromLittleEndian(fileInfoI.filePos);
        lumpP.size = fromLittleEndian(fileInfoI.size);
    }
    fileStream.close();
}

void W_InitMultipleFiles(std::span<std::string_view const> filenames)
{
    numLumps = 0;
    for (auto filename : filenames)
    {
        W_AddFile(filename);
    }

    if (!numLumps)
        I_Error("W_InitFiles: no files found");

    lumpCache.assign(numLumps, nullptr);
}

void W_InitMultipleFilesC (char** filenames)
{
    std::vector<std::string_view> fileNames;
    for (; *filenames; filenames++)
        fileNames.emplace_back(*filenames);
    W_InitMultipleFiles(fileNames);
}

//
// W_InitFile
// Just initialize from a single file.
//
void W_InitFile(std::string_view filename)
{
    std::array<std::string_view, 1> names{filename};
    W_InitMultipleFiles(names);
}

//
// W_NumLumps
//
int W_NumLumps(void)
{
    return numLumps;
}

//
// W_CheckNumForName
// Returns -1 if name not found.
//

int W_CheckNumForName(std::string_view name)
{
    // TODO make the input name uppercase if it fails.
    for (auto [index, lumpP] : lumpInfo | std::views::enumerate | std::views::reverse)
    {
        auto wadName = std::string_view{lumpP.name.data(), lumpP.name.size()};
        wadName = wadName.substr(0, wadName.find('\0'));
        if (iequals(wadName, name))
        {
            return index;
        }
    }
    return -1;
}

int W_CheckNumForNameC(char const *name)
{
    return W_CheckNumForName(WADNameView(name));
}

//
// W_GetNumForName
// Calls W_CheckNumForName, but bombs out if not found.
//
int W_GetNumForName(std::string_view name)
{
    int i;

    i = W_CheckNumForName(name);

    if (i == -1)
        I_Error("W_GetNumForName: %.*s not found!", static_cast<int>(name.size()), name.data());

    return i;
}

int	W_GetNumForNameC (char const* name)
{
    return W_GetNumForName(WADNameView(name));
}

//
// W_LumpLength
// Returns the buffer size needed to load the given lump.
//
int W_LumpLength(int lump)
{
    if (lump >= numLumps)
        I_Error("W_LumpLength: %i >= numLumps", lump);

    return lumpInfo[lump].size;
}

//
// W_ReadLump
// Loads the lump into the given buffer,
//  which must be >= W_LumpLength().
//
void W_ReadLump(int lump, void *dest)
{
    if (lump >= numLumps)
        I_Error("W_ReadLump: %i >= numLumps", lump);
    LumpInfoType const &currentLump = lumpInfo.at(lump);

    std::string lumpFilename;
    if (currentLump.reloadLump)
    {
        lumpFilename = std::string(reloadName);
    }
    else
    {
        lumpFilename = currentLump.sourcePath;
    }
    std::ifstream fileStream(lumpFilename, std::ios::in | std::ios::binary);
    if (!fileStream.is_open())
        I_Error("W_ReadLump: couldn't open %s", lumpFilename.c_str());
    
    fileStream.seekg(currentLump.position, std::ios::beg);
    fileStream.read(reinterpret_cast<char *>(dest), currentLump.size);
    std::streamsize bytesRead = fileStream.gcount();
    if (bytesRead < currentLump.size)
    {
        I_Error("W_ReadLump: only read %i of %i on lump %i", bytesRead, currentLump.size, lump);
    }
    fileStream.close();
}

//
// W_CacheLumpNum
//
void *W_CacheLumpNum(int lump, int tag)
{
    if (lump >= numLumps)
        I_Error("W_CacheLumpNum: %i >= numlumps", lump);

    if (!lumpCache[lump])
    {
        Z_Malloc(W_LumpLength(lump), tag, &lumpCache[lump]);
        W_ReadLump(lump, lumpCache[lump]);
    }
    else
    {
        Z_ChangeTag(lumpCache[lump], tag);
    }
    return lumpCache[lump];
}

//
// W_CacheLumpName
//
void *W_CacheLumpName(char const *name, int tag)
{
    return W_CacheLumpNum(W_GetNumForName(WADNameView(name)), tag);
}

void W_CopyLumpName(int lump, char name[9])
{
    if (lump < 0 || lump >= numLumps)
    {
        I_Error("W_CopyLumpName: bad lump %i", lump);
    }

    memcpy(name, lumpInfo[lump].name.data(), 8);
    name[8] = '\0';
}

//
// W_Profile
//
int info[2500][10];
int profilecount;

void W_Profile(void)
{
    char ch;
    for (auto [index, lumpCacheI] : lumpCache | std::views::enumerate)
    {
        if (!lumpCacheI)
        {
            ch = ' ';
            continue;
        }
        else
        {
            auto block = reinterpret_cast<memblock_t *>(
                static_cast<byte *>(lumpCacheI) -sizeof(memblock_t));
            if (block->tag < PU_PURGELEVEL)
                ch = 'S';
            else
                ch = 'P';
        }
        info[index][profilecount] = ch;
    }
    profilecount++;

    std::array<char, 8> name;
    std::ofstream fileStream("waddump.txt", std::ios::out);
    for (auto  [index, lumpInfoI] : lumpInfo | std::views::enumerate)
    {
        name = lumpInfoI.name;
        auto it = std::find(name.begin(), name.end(), '\0');
        if (it != name.end())
        {
            std::fill(it, name.end(), ' ');
        }
        fileStream.write(name.data(), name.size());
        fileStream << " ";
        for (int j=0; j< profilecount; j++)
        {
            fileStream << "    " << info[index][j];
        }
        fileStream << std::endl;
    }
    fileStream.close();
    
}
