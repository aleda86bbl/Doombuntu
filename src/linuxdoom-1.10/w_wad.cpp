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
#include <ctype.h>
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

#include "doomtype.h"
#include "i_system.h"
#include "m_swap.h"
#include "z_zone.h"

#ifdef __GNUG__
#pragma implementation "w_wad.h"
#endif
#include "w_wad.hpp"

//
// GLOBALS
//

// Location of each lump on disk.
std::vector<LumpInfoType> lumpInfo;
int numLumps;
std::vector<void *> lumpCache;

#define strcmpi strcasecmp

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

void strupr(char *s)
{
    while (*s)
    {
        *s = toupper(*s);
        s++;
    }
}

int filelength(int handle)
{
    struct stat fileinfo;

    if (fstat(handle, &fileinfo) == -1)
        I_Error("Error fstating");

    return fileinfo.st_size;
}

void ExtractFileBase(char *path, char *dest)
{
    char *src;
    int length;

    src = path + strlen(path) - 1;

    // back up until a \ or the start
    while (src != path && *(src - 1) != '\\' && *(src - 1) != '/')
    {
        src--;
    }

    // copy up to eight characters
    memset(dest, 0, 8);
    length = 0;

    while (*src && *src != '.')
    {
        if (++length == 9)
            I_Error("Filename base of %s >8 chars", path);

        *dest++ = toupper((int)*src++);
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

int reloadlump;
char *reloadname;

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

/*void W_AddFile(char *filename)
{
    wadinfo_t header;
    lumpinfo_t *lump_p;
    unsigned i;
    int handle;
    int length;
    int startlump;
    filelump_t *fileinfo;
    filelump_t singleinfo;
    int storehandle;

    // open the file and add to directory

    // handle reload indicator.
    if (filename[0] == '~')
    {
        filename++;
        reloadname = filename;
        reloadlump = numlumps;
    }

    if ((handle = open(filename, O_RDONLY | O_BINARY)) == -1)
    {
        printf(" couldn't open %s\n", filename);
        return;
    }

    printf(" adding %s\n", filename);
    startlump = numlumps;

    if (strcmpi(filename + strlen(filename) - 3, "wad"))
    {
        // single lump file
        fileinfo = &singleinfo;
        singleinfo.filepos = 0;
        singleinfo.size = LONG(filelength(handle));
        ExtractFileBase(filename, singleinfo.name);
        numlumps++;
    }
    else
    {
        // WAD file
        read(handle, &header, sizeof(header));
        if (strncmp(header.identification, "IWAD", 4))
        {
            // Homebrew levels?
            if (strncmp(header.identification, "PWAD", 4))
            {
                I_Error("Wad file %s doesn't have IWAD "
                        "or PWAD id\n",
                        filename);
            }

            // ???modifiedgame = true;
        }
        header.numlumps = LONG(header.numlumps);
        header.infotableofs = LONG(header.infotableofs);
        length = header.numlumps * sizeof(filelump_t);
        fileinfo = alloca(length);
        lseek(handle, header.infotableofs, SEEK_SET);
        read(handle, fileinfo, length);
        numlumps += header.numlumps;
    }

    // Fill in lumpinfo
    lumpinfo = realloc(lumpinfo, numlumps * sizeof(lumpinfo_t));

    if (!lumpinfo)
        I_Error("Couldn't realloc lumpinfo");

    lump_p = &lumpinfo[startlump];

    storehandle = reloadname ? -1 : handle;

    for (i = startlump; i < numlumps; i++, lump_p++, fileinfo++)
    {
        lump_p->handle = storehandle;
        lump_p->position = LONG(fileinfo->filepos);
        lump_p->size = LONG(fileinfo->size);
        strncpy(lump_p->name, fileinfo->name, 8);
    }

    if (reloadname)
        close(handle);
}*/

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

//
// W_Reload
// Flushes any of the reloadable lumps in memory
//  and reloads the directory.
//
/*void W_Reload(void)
{
    wadinfo_t header;
    int lumpcount;
    lumpinfo_t *lump_p;
    unsigned i;
    int handle;
    int length;
    filelump_t *fileinfo;

    if (!reloadname)
        return;

    if ((handle = open(reloadname, O_RDONLY | O_BINARY)) == -1)
        I_Error("W_Reload: couldn't open %s", reloadname);

    read(handle, &header, sizeof(header));
    lumpcount = LONG(header.numlumps);
    header.infotableofs = LONG(header.infotableofs);
    length = lumpcount * sizeof(filelump_t);
    fileinfo = alloca(length);
    lseek(handle, header.infotableofs, SEEK_SET);
    read(handle, fileinfo, length);

    // Fill in lumpinfo
    lump_p = &lumpinfo[reloadlump];

    for (i = reloadlump; i < reloadlump + lumpcount; i++, lump_p++, fileinfo++)
    {
        if (lumpcache[i])
            Z_Free(lumpcache[i]);

        lump_p->position = LONG(fileinfo->filepos);
        lump_p->size = LONG(fileinfo->size);
    }

    close(handle);
}*/

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
// W_InitMultipleFiles
// Pass a null terminated list of files to use.
// All files are optional, but at least one file
//  must be found.
// Files with a .wad extension are idlink files
//  with multiple lumps.
// Other files are single lumps with the base filename
//  for the lump name.
// Lump names can appear multiple times.
// The name searcher looks backwards, so a later file
//  does override all earlier ones.
//
/*void W_InitMultipleFiles(char **filenames)
{
    int size;

    // open all the files, load headers, and count lumps
    numlumps = 0;

    // will be realloced as lumps are added
    lumpinfo = malloc(1);

    for (; *filenames; filenames++)
        W_AddFile(*filenames);

    if (!numlumps)
        I_Error("W_InitFiles: no files found");

    // set up caching
    size = numlumps * sizeof(*lumpcache);
    lumpcache = malloc(size);

    if (!lumpcache)
        I_Error("Couldn't allocate lumpcache");

    memset(lumpcache, 0, size);
}*/

//
// W_InitFile
// Just initialize from a single file.
//
/*void W_InitFile(char *filename)
{
    char *names[2];

    names[0] = filename;
    names[1] = NULL;
    W_InitMultipleFiles(names);
}*/

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

/*int W_CheckNumForName(char const *name)
{
    union {
        char s[9];
        int x[2];

    } name8;

    int v1;
    int v2;
    lumpinfo_t *lump_p;

    // make the name into two integers for easy compares
    strncpy(name8.s, name, 8);

    // in case the name was a fill 8 chars
    name8.s[8] = 0;

    // case insensitive
    strupr(name8.s);

    v1 = name8.x[0];
    v2 = name8.x[1];

    // scan backwards so patch lump files take precedence
    lump_p = lumpinfo + numlumps;

    while (lump_p-- != lumpinfo)
    {
        if (*(int *)lump_p->name == v1 && *(int *)&lump_p->name[4] == v2)
        {
            return lump_p - lumpinfo;
        }
    }

    // TFB. Not found.
    return -1;
}*/

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
// W_ReadLump
// Loads the lump into the given buffer,
//  which must be >= W_LumpLength().
//
/*void W_ReadLump(int lump, void *dest)
{
    int c;
    lumpinfo_t *l;
    int handle;

    if (lump >= numlumps)
        I_Error("W_ReadLump: %i >= numlumps", lump);

    l = lumpinfo + lump;

    // ??? I_BeginRead ();

    if (l->handle == -1)
    {
        // reloadable file, so use open / read / close
        if ((handle = open(reloadname, O_RDONLY | O_BINARY)) == -1)
            I_Error("W_ReadLump: couldn't open %s", reloadname);
    }
    else
        handle = l->handle;

    lseek(handle, l->position, SEEK_SET);
    c = read(handle, dest, l->size);

    if (c < l->size)
        I_Error("W_ReadLump: only read %i of %i on lump %i", c, l->size, lump);

    if (l->handle == -1)
        close(handle);

    // ??? I_EndRead ();
}*/

//
// W_CacheLumpNum
//
/*void *W_CacheLumpNum(int lump, int tag)
{
    byte *ptr;

    if ((unsigned)lump >= numlumps)
        I_Error("W_CacheLumpNum: %i >= numlumps", lump);

    if (!lumpcache[lump])
    {
        // read the lump in

        // printf ("cache miss on lump %i\n",lump);
        ptr = Z_Malloc(W_LumpLength(lump), tag, &lumpcache[lump]);
        W_ReadLump(lump, lumpcache[lump]);
    }
    else
    {
        // printf ("cache hit on lump %i\n",lump);
        Z_ChangeTag(lumpcache[lump], tag);
    }

    return lumpcache[lump];
}*/

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

/*void W_Profile(void)
{
    int i;
    memblock_t *block;
    void *ptr;
    char ch;
    FILE *f;
    int j;
    char name[9];

    for (i = 0; i < numlumps; i++)
    {
        ptr = lumpcache[i];
        if (!ptr)
        {
            ch = ' ';
            continue;
        }
        else
        {
            block = (memblock_t *)((byte *)ptr - sizeof(memblock_t));
            if (block->tag < PU_PURGELEVEL)
                ch = 'S';
            else
                ch = 'P';
        }
        info[i][profilecount] = ch;
    }
    profilecount++;

    f = fopen("waddump.txt", "w");
    name[8] = 0;

    for (i = 0; i < numlumps; i++)
    {
        memcpy(name, lumpinfo[i].name, 8);

        for (j = 0; j < 8; j++)
            if (!name[j])
                break;

        for (; j < 8; j++)
            name[j] = ' ';

        fprintf(f, "%s ", name);

        for (j = 0; j < profilecount; j++)
            fprintf(f, "    %c", info[i][j]);

        fprintf(f, "\n");
    }
    fclose(f);
}*/
