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
//	System interface for sound.
//
//-----------------------------------------------------------------------------

static const char rcsid[] = "$Id: i_unix.c,v 1.5 1997/02/03 22:45:10 b1 Exp $";

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <math.h>

#include <sys/time.h>
#include <sys/types.h>

#ifndef LINUX
#include <sys/filio.h>
#endif

#include <fcntl.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "z_zone.h"

#include "i_sound.hpp"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.hpp"
#include "w_wad.h"

#include "SDL3/SDL.h"
#include "doomdef.h"
#include <adlmidi.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <string_view>
#include <string>
#include <format>

constexpr size_t SAMPLE_COUNT = 512;
constexpr size_t NUM_CHANNELS = 8;
constexpr size_t BUFMUL = 4;
constexpr size_t MIX_BUFFER_SIZE = SAMPLE_COUNT * BUFMUL;
constexpr size_t MUSIC_SAMPLE_COUNT = SAMPLE_COUNT * 2;
constexpr size_t SAMPLERATE = 11025;
constexpr size_t SAMPLE_SIZE = 2;

// UNIX hack, to be removed.
// Separate sound server process.
using UniqueAudioStream = std::unique_ptr<SDL_AudioStream, void (*)(SDL_AudioStream *)>;
UniqueAudioStream audioStream = UniqueAudioStream(nullptr, SDL_DestroyAudioStream);
using UniqueMIDIPlayer = std::unique_ptr<ADL_MIDIPlayer, void (*)(ADL_MIDIPlayer *)>;
UniqueMIDIPlayer midiPlayer = UniqueMIDIPlayer(nullptr, adl_close);

struct Song
{
    std::byte *data{nullptr};
    size_t size{0};
    bool playing{false};
    bool looping{false};
};

std::unordered_map<int, Song> songs;
std::optional<std::reference_wrapper<Song const>> currentSong = std::nullopt;

// The number of internal mixing channels,
//  the samples calculated for each mixing step,
//  the size of the 16bit, 2 hardware channel (stereo)
//  mixing buffer, and the samplerate of the raw data.

// Needed for calling the actual sound output.

// The actual lengths of all sound effects.
std::array<int, NUMSFX> lengths{};

// The global mixing buffer.
// Basically, samples from all active internal channels
//  are modifed and added, and stored in the buffer
//  that is submitted to the audio device.
std::array<short, MIX_BUFFER_SIZE> mixBuffer{};
std::array<short, MIX_BUFFER_SIZE> activeMusicBuffer{};
std::array<short, MIX_BUFFER_SIZE> silentMusicBuffer{};
std::span<short const> musicBuffer{};

struct ChannelType
{
    int start{0}; // Time/gametic that the channel started playing, used to determine oldest which automatically has lowest priority.
    int handle{0}; // determined on registration, might be used to unregister/stop/modify
    int sfxID{0};  // SFX id of the playing sound effect. Used to catch duplicates (lie chainsaw)
    unsigned int step{0};  // The channel step amount.
    unsigned int stepRemainder{0}; // ... and a 0.16 bit remainder of last step.
    std::span<unsigned char const> sfxData;
    std::span<int> leftVolLookup;
    std::span<int> rightVolLookup;
};

std::array<ChannelType, NUM_CHANNELS> channels;

// Pitch to stepping lookup, unused.
std::array<int, 256> steptable{};

// Volume lookups.
std::array<int, 128 * 256> vol_lookup{};

// Hardware left and right channel volume lookup.
std::array<std::span<int>, NUM_CHANNELS> channelleftvol_lookup;
std::array<std::span<int>, NUM_CHANNELS> channelrightvol_lookup;

//
// Safe ioctl, convenience.
//
void myioctl(int fd, int command, int *arg)
{
    int rc;

    rc = ioctl(fd, command, arg);
    if (rc < 0)
    {
        fprintf(stderr, "ioctl(dsp,%d,arg) failed\n", command);
        fprintf(stderr, "errno=%d\n", errno);
        exit(-1);
    }
}

//
// This function loads the sound data from the WAD lump,
//  for single sound.
//
void *getsfx(std::string_view sfxname, int &len)
{
    unsigned char *sfx;
    unsigned char *paddedsfx;
    int i;
    int size;
    int paddedsize;
    int sfxlump;

    // Get the sound data from the WAD, allocate lump
    //  in zone memory.
    std::string name = std::format("ds{}", sfxname);

    // Now, there is a severe problem with the
    //  sound handling, in it is not (yet/anymore)
    //  gamemode aware. That means, sounds from
    //  DOOM II will be requested even with DOOM
    //  shareware.
    // The sound list is wired into sounds.c,
    //  which sets the external variable.
    // I do not do runtime patches to that
    //  variable. Instead, we will use a
    //  default sound for replacement.
    if (W_CheckNumForName(name.c_str()) == -1)
        sfxlump = W_GetNumForName("dspistol");
    else
        sfxlump = W_GetNumForName(name.c_str());

    size = W_LumpLength(sfxlump);

    // Debug.
    // fprintf( stderr, "." );
    // fprintf( stderr, " -loading  %s (lump %d, %d bytes)\n",
    //	     sfxname, sfxlump, size );
    // fflush( stderr );

    sfx = static_cast<unsigned char *>(W_CacheLumpNum(sfxlump, PU_STATIC));

    // Pads the sound effect out to the mixing buffer size.
    // The original realloc would interfere with zone memory.
    paddedsize = ((size - 8 + (SAMPLE_COUNT - 1)) / SAMPLE_COUNT) * SAMPLE_COUNT;

    // Allocate from zone memory.
    paddedsfx = static_cast<unsigned char *>(Z_Malloc(paddedsize + 8, PU_STATIC, 0));
    // ddt: (unsigned char *) realloc(sfx, paddedsize+8);
    // This should interfere with zone memory handling,
    //  which does not kick in in the soundserver.

    // Now copy and pad.
    memcpy(paddedsfx, sfx, size);
    for (i = size; i < paddedsize + 8; i++)
        paddedsfx[i] = 128;

    // Remove the cached lump.
    Z_Free(sfx);

    // Preserve padded length.
    len = paddedsize;

    // Return allocated padded data.
    return static_cast<void *>(paddedsfx + 8);
}

//
// This function adds a sound to the
//  list of currently active sounds,
//  which is maintained as a given number
//  (eight, usually) of internal channels.
// Returns a handle.
//
int addsfx(int sfxid, int volume, int step, int separation)
{
    static unsigned short handlenums = 0;
    int oldest = gametic;

    // Chainsaw troubles.
    // Play these sound effects only one at a time.
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful || sfxid == sfx_sawhit ||
        sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        // Loop all channels, check.
        for (auto && channel : channels )
        {
            // Active, and using the same SFX?
            if (!channel.sfxData.empty() && channel.sfxID == sfxid)
            {
                channel.sfxData = {};
                break;
            }
        }
    }

    // Loop all channels to find oldest SFX.
    std::reference_wrapper<ChannelType> targetChannel = channels.front();
    for (auto & channel : channels)
    {
        if (channel.sfxData.empty())
        {
            targetChannel = channel;
            break;
        }
        if (channel.start < oldest)
        {
            targetChannel = channel;
            oldest = channel.start;
        }
    }
    // Okay, in the less recent channel,
    //  we will handle the new SFX.
    // Set pointer to raw data.
    targetChannel.get().sfxData = {static_cast<unsigned char *>(S_sfx[sfxid].data), static_cast<size_t>(lengths[sfxid])};

    // Reset current handle number, limited to 0..100.
    if (!handlenums)
        handlenums = 100;

    // Preserved so sounds could be stopped (unused).
    int rc;
    targetChannel.get().handle = rc = handlenums++;
    // Set stepping???
    // Kinda getting the impression this is never used.
    targetChannel.get().step = step;
    // ???
    targetChannel.get().stepRemainder = 0;
    // Should be gametic, I presume.
    targetChannel.get().start = gametic;

    // Separation, that is, orientation/stereo.
    //  range is: 1 - 256
    separation += 1;
    // Per left/right channel.
    //  x^2 seperation,
    //  adjust volume properly.
    int leftvol = volume - ((volume * separation * separation) >> 16); ///(256*256);
    separation = separation - 257;
    int rightvol = volume - ((volume * separation * separation) >> 16);

    if (rightvol < 0 || rightvol > 127)
        I_Error("rightvol out of bounds");

    if (leftvol < 0 || leftvol > 127)
        I_Error("leftvol out of bounds");

    // Get the proper lookup table piece
    //  for this volume level???
    std::span<int> volSpanLookup(vol_lookup);
    targetChannel.get().leftVolLookup = volSpanLookup.subspan(leftvol * 256, 256);
    targetChannel.get().rightVolLookup = volSpanLookup.subspan(rightvol * 256, 256);
    
    // Preserve sound SFX id,
    //  e.g. for avoiding duplicates of chainsaw.
    targetChannel.get().sfxID = sfxid;

    // You tell me.
    return rc;
}

//
// SFX API
// Note: this was called by S_Init.
// However, whatever they did in the
// old DPMS based DOS version, this
// were simply dummies in the Linux
// version.
// See soundserver initdata().
//
void I_SetChannels()
{
    // This table provides step widths for pitch parameters.
    // I fail to see that this is currently used.
    for (auto &&[idx, value] : std::views::enumerate(steptable))
    {
        int i = static_cast<int>(idx) - 128;
        value = static_cast<int>(std::pow(2.0, (i / 64.0)) * 65536.0);
    }

    auto iRange = std::views::iota(0, 128);
    auto jRange = std::views::iota(0, 256);

    auto grid = std::views::cartesian_product(iRange, jRange);

    // Generates volume lookup tables
    //  which also turn the unsigned samples
    //  into signed samples.
    for (auto &&[coords, value] : std::views::zip(grid, vol_lookup))
    {
        auto [i, j] = coords;
        value = (i * (j - 128) * 256) / 127;
    }
}

void I_SetSfxVolume(int volume)
{
    // Identical to DOS.
    // Basically, this should propagate
    //  the menu/config file setting
    //  to the state variable used in
    //  the mixing.
    snd_SfxVolume = volume;
}

// MUSIC API - dummy. Some code from DOS version.
void I_SetMusicVolume(int volume)
{
    // Internal state variable.
    snd_MusicVolume = volume;
    // Now set volume on output device.
    // Whatever( snd_MusciVolume );
}

//
// Retrieve the raw data lump index
//  for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

//
// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
//
int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    // UNUSED
    priority = 0;

    // Debug.
    // fprintf( stderr, "starting sound %d", id );

    // Returns a handle (not used).
    id = addsfx(id, vol, steptable[pitch], sep);

    // fprintf( stderr, "/handle is %d\n", id );

    return id;
}

void I_StopSound(int handle)
{
    // You need the handle returned by StartSound.
    // Would be looping all channels,
    //  tracking down the handle,
    //  an setting the channel to zero.

    // UNUSED.
    handle = 0;
}

int I_SoundIsPlaying(int handle)
{
    // Ouch.
    return gametic < handle;
}

void I_ProduceSound(void)
{
    if (!audioStream)
        return;

    constexpr int bytesPerFrame = 2 * sizeof(short);
    constexpr int blockBytes = SAMPLE_COUNT * bytesPerFrame;
    constexpr int targetQueuedBytes = 4 * blockBytes;

    for (int queuedBytes = SDL_GetAudioStreamQueued(audioStream.get()); queuedBytes < targetQueuedBytes;
         queuedBytes += blockBytes)
    {
        I_UpdateSound();
        I_SubmitSound();
    }
}

//
// This function loops all active (internal) sound
//  channels, retrieves a given number of samples
//  from the raw sound data, modifies it according
//  to the current (internal) channel parameters,
//  mixes the per channel samples into the global
//  mixbuffer, clamping it to the allowed range,
//  and sets up everything for transferring the
//  contents of the mixbuffer to the (two)
//  hardware channels (left and right, that is).
//
// This function currently supports only 16bit.
//
void I_UpdateSound(void)
{
    // continue to play music
    if (currentSong.has_value() && currentSong->get().playing)
    {
        int const bytesWritten = adl_play(midiPlayer.get(), MUSIC_SAMPLE_COUNT, activeMusicBuffer.data());
        std::fill(activeMusicBuffer.begin() + std::max(bytesWritten, 0), activeMusicBuffer.end(), 0);
        musicBuffer = activeMusicBuffer;
    }
    else
    {
        musicBuffer = silentMusicBuffer;
    }

    // Mix current sound data.
    // Data, from raw sound, for right and left.
    // Left and right channel
    // are in global mixbuffer, alternating.
    
    // Mix sounds into the mixing buffer.
    // Loop over step*SAMPLECOUNT,
    //  that is 512 values for two channels.
    auto musicAndMixBufferViews = std::views::zip(
        musicBuffer.first(MUSIC_SAMPLE_COUNT) | std::views::adjacent<2> | std::views::stride(2),
        std::span{mixBuffer}.first(MUSIC_SAMPLE_COUNT) | std::views::adjacent<2> | std::views::stride(2)
    );
    
    for (auto [musicPair, mixPair] : musicAndMixBufferViews) 
    {
        int dl = 0;
        int dr = 0;
        
        // Love thy L2 chache - made this a loop.
        // Now more channels could be set at compile time
        //  as well. Thus loop those  channels.
        for (auto & channel : channels)
        {
            if (!channel.sfxData.empty())
            {
                unsigned int sample = channel.sfxData.front();
                dl += channel.leftVolLookup[sample];
                dr += channel.rightVolLookup[sample];
                // Increment index ???
                channel.stepRemainder += channel.step;
                // MSB is next sample???
                auto const advance = static_cast<std::size_t>(channel.stepRemainder >> 16);
                // Limit to LSB???
                channel.stepRemainder &= 65536 - 1;
                // Check whether we are done.
                if (advance >= channel.sfxData.size())
                {
                    channel.sfxData = {};
                }
                else
                {
                    channel.sfxData = channel.sfxData.subspan(advance);
                }
            }
        }

        int const musicGain = snd_MusicVolume;
        auto [musicBufferL, musicBufferR] = musicPair;
        dl += (musicBufferL * musicGain) / 127;
        dr += (musicBufferR * musicGain) / 127;

        auto [mixBufferL, mixBufferR] = mixPair;
        // Clamp to range. Left hardware channel.
        // Has been char instead of short.    
        mixBufferL = std::clamp(dl, -0x8000, 0x7fff);
        mixBufferR = std::clamp(dr, -0x8000, 0x7fff);
    }
}

//
// This would be used to write out the mixbuffer
//  during each game loop update.
// Updates sound buffer and audio device at runtime.
// It is called during Timer interrupt with SNDINTR.
// Mixing now done synchronous, and
//  only output be done asynchronous?
//
void I_SubmitSound(void)
{
    bool hasData = std::ranges::any_of(mixBuffer, [](short x) { return x != 0; });
    if (!audioStream || !hasData)
        return;

    int payloadBytes = SAMPLE_COUNT * BUFMUL;

    if (!SDL_PutAudioStreamData(audioStream.get(), mixBuffer.data(), payloadBytes))
    {
        std::cerr << "Buffer submit failed: " << SDL_GetError() << std::endl;
    }
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    // I fail too see that this is used.
    // Would be using the handle to identify
    //  on which channel the sound might be active,
    //  and resetting the channel parameters.

    // UNUSED.
    handle = vol = sep = pitch = 0;
}

void I_ShutdownSound(void)
{
    audioStream.reset();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void I_InitSound()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        std::cerr << "SDL3 Audio failed to init: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_AudioSpec audioSpec = {SDL_AUDIO_S16, 2, 11025};

    audioStream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, NULL, NULL));

    if (!audioStream)
    {
        std::cerr << "Could not map SDL3 device stream: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_ResumeAudioStreamDevice(audioStream.get());

    // Initialize external data (all sounds) at start, keep static.
    fprintf(stderr, "I_InitSound: ");

    for (int i = 1; i < NUMSFX; i++)
    {
        // Alias? Example is the chaingun sound linked to pistol.
        if (!S_sfx[i].link)
        {
            // Load data from WAD file.
            S_sfx[i].data = getsfx(S_sfx[i].name, lengths[i]);
        }
        else
        {
            // Previously loaded already?
            S_sfx[i].data = S_sfx[i].link->data;
            lengths[i] = lengths[(S_sfx[i].link - S_sfx) / sizeof(sfxinfo_t)];
        }
    }

    fprintf(stderr, " pre-cached all sound data\n");

    // Now initialize mixbuffer with zero.
    for (int i = 0; i < MIX_BUFFER_SIZE; i++)
        mixBuffer[i] = 0;

    // Finished initialization.
    fprintf(stderr, "I_InitSound: sound module ready\n");
}

//
// MUSIC API.
// Still no music done.
// Remains. Dummies.
//
void I_InitMusic(void)
{
    midiPlayer.reset(adl_init(SAMPLERATE));
    if (!midiPlayer)
    {
        std::cerr << "adl_init failed: " << adl_errorString() << std::endl;
    }
}

void I_ShutdownMusic(void)
{
    midiPlayer.reset();
}

static int looping = 0;
static int musicdies = -1;
static int newSongHandle = -1;

void I_PlaySong(int handle, int looping)
{
    songs.at(handle).playing = true;
    songs.at(handle).looping = looping;
    int openResult = adl_openData(midiPlayer.get(), songs.at(handle).data, songs.at(handle).size);
    if (openResult < 0)
    {
        std::cerr << "Error running adl_openData: " << adl_errorString() << std::endl;
        songs.at(handle).playing = false;
        songs.at(handle).looping = false;
        currentSong = std::nullopt;
    }
    adl_setLoopEnabled(midiPlayer.get(), songs.at(handle).looping);
    currentSong = songs.at(handle);
}

void I_PauseSong(int handle)
{
    songs.at(handle).playing = false;
}

void I_ResumeSong(int handle)
{
    songs.at(handle).playing = true;
}

void I_StopSong(int handle)
{
    songs.at(handle).playing = false;
    adl_reset(midiPlayer.get());
    currentSong = std::nullopt;
}

void I_UnRegisterSong(int handle)
{
    I_StopSong(handle);
    songs.erase(handle);
}

int I_RegisterSong(void *data, size_t size)
{
    ++newSongHandle;
    songs.emplace(newSongHandle, Song{static_cast<std::byte *>(data), size, false, false});
    return newSongHandle;
}

// Is the song playing?
int I_QrySongPlaying(int handle)
{
    return songs.at(handle).looping || songs.at(handle).playing;
}
