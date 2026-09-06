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
//
// $Log:$
//
// DESCRIPTION:
//	Main loop menu stuff.
//	Default Config File.
//	PCX Screenshots.
//
//-----------------------------------------------------------------------------

static const char rcsid[] = "$Id: m_misc.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ctype.h>

#include "doomdef.h"

#include "z_zone.h"

#include "m_argv.h"
#include "m_swap.h"

#include "w_wad.hpp"

#include "i_system.h"
#include "i_video.hpp"
#include "v_video.h"

#include "hu_stuff.h"

// State.
#include "doomstat.h"

// Data.
#include "dstrings.h"

#include "m_misc.hpp"
#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>
//
// M_DrawText
// Returns the final X coordinate
// HU_Init must have been called to init the font
//
extern patch_t *hu_font[HU_FONTSIZE];

int M_DrawText(int x, int y, boolean direct, char *string)
{
    int c;
    int w;

    while (*string)
    {
        c = toupper(*string) - HU_FONTSTART;
        string++;
        if (c < 0 || c > HU_FONTSIZE)
        {
            x += 4;
            continue;
        }

        w = SHORT(hu_font[c]->width);
        if (x + w > SCREENWIDTH)
            break;
        if (direct)
            V_DrawPatchDirect(x, y, 0, hu_font[c]);
        else
            V_DrawPatch(x, y, 0, hu_font[c]);
        x += w;
    }

    return x;
}

//
// M_WriteFile
//
#ifndef O_BINARY
#define O_BINARY 0
#endif

boolean M_WriteFile(char const *name, void *source, int length)
{
    int handle;
    int count;

    handle = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);

    if (handle == -1)
        return false;

    count = write(handle, source, length);
    close(handle);

    if (count < length)
        return false;

    return true;
}

//
// M_ReadFile
//
int M_ReadFile(char const *name, byte **buffer)
{
    int handle, count, length;
    struct stat fileinfo;
    byte *buf;

    handle = open(name, O_RDONLY | O_BINARY, 0666);
    if (handle == -1)
        I_Error("Couldn't read file %s", name);
    if (fstat(handle, &fileinfo) == -1)
        I_Error("Couldn't read file %s", name);
    length = fileinfo.st_size;
    buf = (byte *)Z_Malloc(length, PU_STATIC, NULL);
    count = read(handle, buf, length);
    close(handle);

    if (count < length)
        I_Error("Couldn't read file %s", name);

    *buffer = buf;
    return length;
}

//
// DEFAULTS
//
int usemouse;
int usejoystick;

extern int key_right;
extern int key_left;
extern int key_up;
extern int key_down;

extern int key_strafeleft;
extern int key_straferight;

extern int key_fire;
extern int key_use;
extern int key_strafe;
extern int key_speed;

extern int mousebfire;
extern int mousebstrafe;
extern int mousebforward;

extern int joybfire;
extern int joybstrafe;
extern int joybuse;
extern int joybspeed;

extern int viewwidth;
extern int viewheight;

extern int mouseSensitivity;
extern int showMessages;

extern int detailLevel;

extern int screenblocks;

extern int showMessages;

// machine-independent sound params
extern int numChannels;

// UNIX hack, to be removed.
#ifdef SNDSERV
extern int mb_used;
#endif

#ifdef LINUX
char *mousetype;
char *mousedev;
#endif

extern char *chat_macros[];

struct DefaultType
{
    std::string name;
    int *location;
    long long defaultValue;
    int scanTranslate; // PC scan code hack
    int untranslated;  // lousy hack
};

constexpr std::size_t BaseArraySize = 27;

#ifdef NORMALUNIX
constexpr std::size_t NormalUnixSize = 10;
#else
constexpr std::size_t NormalUnixSize = 0;
#endif

#ifdef SNDSERV
constexpr std::size_t SndServSize = 1;
#else
constexpr std::size_t SndServSize = 0;
#endif

#ifdef LINUX
constexpr std::size_t LinuxSize = 2;
#else
constexpr std::size_t LinuxSize = 0;
#endif

constexpr std::size_t DefaultArraySize = BaseArraySize + NormalUnixSize + SndServSize + LinuxSize;

std::array<DefaultType, DefaultArraySize> defaults = {
    {{"mouse_sensitivity", &mouseSensitivity, 5},
     {"sfx_volume", &snd_SfxVolume, 8},
     {"music_volume", &snd_MusicVolume, 8},
     {"show_messages", &showMessages, 1},
#ifdef NORMALUNIX
     {"key_right", &key_right, KEY_RIGHTARROW},
     {"key_left", &key_left, KEY_LEFTARROW},
     {"key_up", &key_up, KEY_UPARROW},
     {"key_down", &key_down, KEY_DOWNARROW},
     {"key_strafeleft", &key_strafeleft, ','},
     {"key_straferight", &key_straferight, '.'},

     {"key_fire", &key_fire, KEY_RCTRL},
     {"key_use", &key_use, ' '},
     {"key_strafe", &key_strafe, KEY_RALT},
     {"key_speed", &key_speed, KEY_RSHIFT},

// UNIX hack, to be removed.
#ifdef SNDSERV
     {"mb_used", &mb_used, 32},
#endif

#endif

#ifdef LINUX
     {"mousedev", (int *)&mousedev, (long long)"/dev/ttyS0"},
     {"mousetype", (int *)&mousetype, (long long)"microsoft"},
#endif

     {"use_mouse", &usemouse, 1},
     {"mouseb_fire", &mousebfire, 0},
     {"mouseb_strafe", &mousebstrafe, 1},
     {"mouseb_forward", &mousebforward, 2},

     {"use_joystick", &usejoystick, 0},
     {"joyb_fire", &joybfire, 0},
     {"joyb_strafe", &joybstrafe, 1},
     {"joyb_use", &joybuse, 3},
     {"joyb_speed", &joybspeed, 2},

     {"screenblocks", &screenblocks, 9},
     {"detaillevel", &detailLevel, 0},

     {"snd_channels", &numChannels, 3},

     {"usegamma", &usegamma, 0},

     {"chatmacro0", (int *)&chat_macros[0], (long long)HUSTR_CHATMACRO0},
     {"chatmacro1", (int *)&chat_macros[1], (long long)HUSTR_CHATMACRO1},
     {"chatmacro2", (int *)&chat_macros[2], (long long)HUSTR_CHATMACRO2},
     {"chatmacro3", (int *)&chat_macros[3], (long long)HUSTR_CHATMACRO3},
     {"chatmacro4", (int *)&chat_macros[4], (long long)HUSTR_CHATMACRO4},
     {"chatmacro5", (int *)&chat_macros[5], (long long)HUSTR_CHATMACRO5},
     {"chatmacro6", (int *)&chat_macros[6], (long long)HUSTR_CHATMACRO6},
     {"chatmacro7", (int *)&chat_macros[7], (long long)HUSTR_CHATMACRO7},
     {"chatmacro8", (int *)&chat_macros[8], (long long)HUSTR_CHATMACRO8},
     {"chatmacro9", (int *)&chat_macros[9], (long long)HUSTR_CHATMACRO9}}};

int numdefaults;
char *defaultfile;

//
// M_SaveDefaults
//
void M_SaveDefaults(void)
{
    std::ofstream fileStream(defaultfile, std::ios::out);
    if (!fileStream.is_open())
        return;

    for (const auto &[name, location, defaultValue, scanTranslate, untranslated] : defaults)
    {
        if (defaultValue == -1)
        {
            char *stringPtr = *reinterpret_cast<char **>(location);
            std::string_view stringView(stringPtr);
            fileStream << name << "\t\t" << stringView << "\n";
        }
        else
        {
            fileStream << name << "\t\t" << *location << "\n";
        }
    }
    fileStream.close();
}

//
// M_LoadDefaults
//
extern byte scantokey[128];

using namespace std::string_view_literals;

void M_LoadDefaults(void)
{
    int i;

    // set everything to base values
    for (auto &defaultIter : defaults)
    {
        *defaultIter.location = defaultIter.defaultValue;
    }

    // check for a custom default file
    i = M_CheckParm("-config");
    if (i && i < myargc - 1)
    {
        defaultfile = myargv[i + 1];
        std::cout << "	default file: " << defaultfile << std::endl;
    }
    else
    {
        defaultfile = basedefault;
    }

    // read the file in, overriding any set defaults
    std::ifstream fileStream(defaultfile, std::ios::in);
    if (!fileStream.is_open())
        return;

    std::string fileLine;
    std::array<std::string, 2> lineParts;
    std::string newString;
    while (std::getline(fileStream, fileLine))
    {
        auto enumeratedSplitViews = fileLine | std::views::split("\t\t"sv) | std::views::enumerate;
        for (auto [i, subRange] : enumeratedSplitViews)
        {
            if (i > lineParts.size())
                break;
            lineParts[i] = std::string(subRange.begin(), subRange.end());
        }
        std::string const &paramName = lineParts[0];
        std::string const &paramStrVal = lineParts[1];
        int paramVal;
        bool isString;
        if (paramStrVal[0] == '"')
        {
            isString = true;
            newString = std::string(paramStrVal);
        }
        else
        {
            isString = false;
            char const *paramStrDataStart;
            char const *paramStrDataEnd;
            int base;
            if (paramStrVal[0] == '0' && paramStrVal[1] == 'x')
            {
                paramStrDataStart = paramStrVal.data() + 2;
                base = 16;
            }
            else
            {
                paramStrDataStart = paramStrVal.data();
                base = 10;
            }
            paramStrDataEnd = paramStrVal.data() + paramStrVal.size();
            auto [ptr, ec] = std::from_chars(paramStrDataStart, paramStrDataEnd, paramVal, base);
        }
        for (auto &defaultIter : defaults)
        {
            if (defaultIter.name == paramName)
            {
                if (!isString)
                {
                    *defaultIter.location = paramVal;
                }
                else
                {
                    *defaultIter.location = *newString.c_str();
                }
                break;
            }
        }
    }

    fileStream.close();
}

//
// SCREEN SHOTS
//

typedef struct
{
    char manufacturer;
    char version;
    char encoding;
    char bits_per_pixel;

    unsigned short xmin;
    unsigned short ymin;
    unsigned short xmax;
    unsigned short ymax;

    unsigned short hres;
    unsigned short vres;

    unsigned char palette[48];

    char reserved;
    char color_planes;
    unsigned short bytes_per_line;
    unsigned short palette_type;

    char filler[58];
    unsigned char data; // unbounded
} pcx_t;

//
// WritePCXfile
//
void WritePCXfile(char *filename, byte *data, int width, int height, byte *palette)
{
    int i;
    int length;
    pcx_t *pcx;
    byte *pack;

    pcx = (pcx_t *)Z_Malloc(width * height * 2 + 1000, PU_STATIC, NULL);

    pcx->manufacturer = 0x0a; // PCX id
    pcx->version = 5;         // 256 color
    pcx->encoding = 1;        // uncompressed
    pcx->bits_per_pixel = 8;  // 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = SHORT(width - 1);
    pcx->ymax = SHORT(height - 1);
    pcx->hres = SHORT(width);
    pcx->vres = SHORT(height);
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1; // chunky image
    pcx->bytes_per_line = SHORT(width);
    pcx->palette_type = SHORT(2); // not a grey scale
    memset(pcx->filler, 0, sizeof(pcx->filler));

    // pack the image
    pack = &pcx->data;

    for (i = 0; i < width * height; i++)
    {
        if ((*data & 0xc0) != 0xc0)
            *pack++ = *data++;
        else
        {
            *pack++ = 0xc1;
            *pack++ = *data++;
        }
    }

    // write the palette
    *pack++ = 0x0c; // palette ID byte
    for (i = 0; i < 768; i++)
        *pack++ = *palette++;

    // write output file
    length = pack - (byte *)pcx;
    M_WriteFile(filename, pcx, length);

    Z_Free(pcx);
}

//
// M_ScreenShot
//
void M_ScreenShot(void)
{
    int i;
    byte *linear;
    char lbmname[12];

    // munge planar buffer to linear
    linear = screens[2];
    I_ReadScreen(linear);

    // find a file name to save it to
    strcpy(lbmname, "DOOM00.pcx");

    for (i = 0; i <= 99; i++)
    {
        lbmname[4] = i / 10 + '0';
        lbmname[5] = i % 10 + '0';
        if (access(lbmname, 0) == -1)
            break; // file doesn't exist
    }
    if (i == 100)
        I_Error("M_ScreenShot: Couldn't create a PCX");

    // save the pcx file
    WritePCXfile(lbmname, linear, SCREENWIDTH, SCREENHEIGHT, (byte *)W_CacheLumpName("PLAYPAL", PU_CACHE));

    players[consoleplayer].message = "screen shot";
}
