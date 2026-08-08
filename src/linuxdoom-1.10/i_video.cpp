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
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <X11/extensions/XShm.h>
#include <SDL3/SDL.h>
// Had to dig up XShm.c for this one.
// It is in the libXext, but not in the XFree86 headers.
#ifdef LINUX
int XShmGetEventBase( Display* dpy ); // problems with g++?
#endif

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"
#include "i_video.hpp"
#include <memory>

#define POINTER_WARP_COUNTDOWN	1

using UniqueWindow = std::unique_ptr<SDL_Window, void(*)(SDL_Window*)>;
using UniqueRenderer = std::unique_ptr<SDL_Renderer, void(*)(SDL_Renderer*)>;
using UniqueSurface = std::unique_ptr<SDL_Surface, void(*)(SDL_Surface*)>;
using UniquePalette = std::unique_ptr<SDL_Palette, void(*)(SDL_Palette*)>;
using UniqueTexture = std::unique_ptr<SDL_Texture, void(*)(SDL_Texture*)>;


UniqueWindow window = UniqueWindow(nullptr, SDL_DestroyWindow);
UniqueRenderer renderer = UniqueRenderer(nullptr, SDL_DestroyRenderer);
UniqueSurface surface = UniqueSurface(nullptr, SDL_DestroySurface);
UniquePalette palette = UniquePalette(nullptr, SDL_DestroyPalette);
UniqueTexture texture = UniqueTexture(nullptr, SDL_DestroyTexture);
uint8_t *cpu_pixels = 0;


int		X_width;
int		X_height;

// MIT SHared Memory extension.
boolean		doShm;

XShmSegmentInfo	X_shminfo;
int		X_shmeventtype;

// Fake mouse handling.
// This cannot work properly w/o DGA.
// Needs an invisible mouse cursor at least.
boolean		grabMouse;
int		doPointerWarp = POINTER_WARP_COUNTDOWN;

// Blocky mode,
// replace each 320x200 pixel with multiply*multiply pixels.
// According to Dave Taylor, it still is a bonehead thing
// to use ....
static int	multiply=1;


//
//  Translates the key currently in X_event
//

int sdlTranslateKey(SDL_Keycode keyCode)
{
	int rc = 0;
	switch(keyCode)
	{
		case SDLK_LEFT: rc = KEY_LEFTARROW; break;
		case SDLK_RIGHT: rc = KEY_RIGHTARROW; break;
		case SDLK_DOWN: rc = KEY_DOWNARROW; break;
		case SDLK_UP: rc = KEY_UPARROW; break;
		case SDLK_ESCAPE: rc = KEY_ESCAPE; break;
		case SDLK_RETURN: rc = KEY_ENTER; break;
		case SDLK_TAB: rc = KEY_TAB; break;
		case SDLK_F1: rc = KEY_F1; break;
		case SDLK_F2: rc = KEY_F2; break;
		case SDLK_F3: rc = KEY_F3; break;
		case SDLK_F4: rc = KEY_F4; break;
		case SDLK_F5: rc = KEY_F5; break;
		case SDLK_F6: rc = KEY_F6; break;
		case SDLK_F7: rc = KEY_F7; break;
		case SDLK_F8: rc = KEY_F8; break;
		case SDLK_F9: rc = KEY_F9; break;
		case SDLK_F10: rc = KEY_F10; break;
		case SDLK_F11: rc = KEY_F11; break;
		case SDLK_F12: rc = KEY_F12; break;
		case SDLK_BACKSPACE:
		case SDLK_DELETE: rc = KEY_BACKSPACE; break;
		case SDLK_PAUSE: rc = KEY_PAUSE; break;
		case SDLK_KP_EQUALS:
		case SDLK_EQUALS: rc = KEY_EQUALS; break;
		case SDLK_KP_MINUS:
		case SDLK_MINUS: rc = KEY_MINUS; break;
		case SDLK_LSHIFT:
		case SDLK_RSHIFT: rc = KEY_RSHIFT; break;
		case SDLK_LCTRL:
		case SDLK_RCTRL: rc = KEY_RCTRL; break;
		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_LGUI:
		case SDLK_RGUI: rc = KEY_RALT; break;
		default:
			rc = keyCode;
			if (rc >= SDLK_SPACE && rc <= SDLK_GRAVE)
				rc = rc - SDLK_SPACE + ' ';
			if (rc >= 'A' && rc <= 'Z')
				rc = rc - 'A' + 'a';
			break;
	}
	return rc;
}

void I_ShutdownGraphics(void)
{
	texture.reset();
	surface.reset();
	palette.reset();
	renderer.reset();
	window.reset();
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
    // er?

}

static int	lastmousex = 0;
static int	lastmousey = 0;
boolean		mousemoved = false;
boolean		shmFinished;

void I_HandleEvent(const SDL_Event* sdlEvent)
{
	event_t event{};
	switch (sdlEvent->type)
	{
		case SDL_EVENT_KEY_DOWN:
			event.type = ev_keydown;
			event.data1 = sdlTranslateKey(sdlEvent->key.key);
			D_PostEvent(&event);
			break;
		case SDL_EVENT_KEY_UP:
			event.type = ev_keyup;
			event.data1 = sdlTranslateKey(sdlEvent->key.key);
			D_PostEvent(&event);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			event.type = ev_mouse;
			event.data1 = 
				(sdlEvent->button.down & Button1Mask) // or use SDL_BUTTON_LMASK
				|  (sdlEvent->button.down & Button2Mask ? 2 : 0) // or SDL_BUTTON_MMASK
				|  (sdlEvent->button.down & Button3Mask ? 4 : 0) // or SDL_BUTTON_RMASK
				|  (sdlEvent->button.button == SDL_BUTTON_LEFT)
				|  (sdlEvent->button.button == SDL_BUTTON_MIDDLE ? 2 : 0)
				|  (sdlEvent->button.button == SDL_BUTTON_RIGHT ? 4 : 0);
			event.data2 = event.data3 = 0;
			D_PostEvent(&event);
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			event.type = ev_mouse;
			event.data1 = 
				(sdlEvent->button.down & Button1Mask)
				|	(sdlEvent->button.down & Button2Mask ? 2 : 0)
				|   (sdlEvent->button.down & Button3Mask ? 4 : 0);
			event.data1 =
			    event.data1
	    		^ (sdlEvent->button.button == SDL_BUTTON_LEFT ? 1 : 0)
	    		^ (sdlEvent->button.button == SDL_BUTTON_MIDDLE ? 2 : 0)
	    		^ (sdlEvent->button.button == SDL_BUTTON_RIGHT ? 4 : 0);
			event.data2 = event.data3 = 0;
			D_PostEvent(&event);
			break;
      	case SDL_EVENT_MOUSE_MOTION:
			event.type = ev_mouse;
			event.data1 =
				(sdlEvent->motion.state & Button1Mask)
				| (sdlEvent->motion.state & Button2Mask ? 2 : 0)
				| (sdlEvent->motion.state & Button3Mask ? 4 : 0);
			event.data2 = ((int) sdlEvent->motion.x - lastmousex) << 2;
			event.data3 = (lastmousey - (int) sdlEvent->motion.y) << 2;

			if (event.data2 || event.data3)
			{
				lastmousex = (int) sdlEvent->motion.x;
				lastmousey = (int) sdlEvent->motion.y;
				if ((int) sdlEvent->motion.x != X_width/2 &&
					(int) sdlEvent->motion.y != X_height/2)
				{
					D_PostEvent(&event);
					mousemoved = false;
				} 
				else
				{
					mousemoved = true;
				}
			}
			break;
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			I_Quit();
			break;
		default:
			break;
	}
}

Cursor
createnullcursor
( Display*	display,
  Window	root )
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
				 &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

//
// I_StartTic
//
void I_StartTic (void)
{
	if (!window)
		return;

	SDL_Event sdlEvent;
	while(SDL_PollEvent(&sdlEvent))
		I_HandleEvent(&sdlEvent);

	if (grabMouse)
	{
		if (!--doPointerWarp)
		{
			SDL_WarpMouseInWindow(window.get(), X_width/2, X_height/2);
		}
	}

	mousemoved = False;
}


//
// I_UpdateNoBlit
//
void I_UpdateNoBlit (void)
{
    // what is this?
}

void I_FinishUpdate (void)
{

    static int	lasttic;

	if (devparm)
	{
		int now = I_GetTime();
		int tics = now -lasttic;
		int i;

		lasttic = now;
		if (tics > 20)
			tics = 20;
		if (tics < 0)
			tics = 0;

		for (i=0 ; i<tics*2 ; i+=2)
			screens[0][(SCREENHEIGHT - 1)* SCREENWIDTH + i] = 0xff;
		for ( ; i<20*2 ; i+=2)
			screens[0][(SCREENHEIGHT - 1)* SCREENWIDTH + i] = 0x0;
	}
    
	if (!SDL_UpdateTexture(
		texture.get(),
		NULL,
		surface->pixels,
		surface->pitch))
	{
		I_Error("SDL_UpdateTexture failed: %s", SDL_GetError());
	}

	SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
	SDL_RenderClear(renderer.get());

	if (!SDL_RenderTexture(renderer.get(), texture.get(), NULL, NULL))
	{
		I_Error("SDL_RenderTexture failed: %s", SDL_GetError());
	}
	if (!SDL_RenderPresent(renderer.get()))
	{
		I_Error("SDL_RenderPresent failed: %s", SDL_GetError());
	}
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, screens[0], SCREENWIDTH*SCREENHEIGHT);
}

static SDL_Color colours[256];

void UploadNewPalette(byte *doomPalette)
{
	for (int i = 0; i < 256; ++i)
	{
		colours[i].r = gammatable[usegamma][doomPalette[i * 3 + 0]];
		colours[i].g = gammatable[usegamma][doomPalette[i * 3 + 1]];
		colours[i].b = gammatable[usegamma][doomPalette[i * 3 + 2]];
		colours[i].a = SDL_ALPHA_OPAQUE;
	}
	if (!SDL_SetPaletteColors(palette.get(), colours, 0, 256))
		I_Error("SDL_SetPaletteColors failed: %s", SDL_GetError());
	if (!SDL_SetSurfacePalette(surface.get(), palette.get()))
		I_Error("SDL_SetSurfacePalette failed: %s", SDL_GetError());
}

void I_SetPalette (byte* doomPalette)
{
    UploadNewPalette(doomPalette);
}


void I_InitGraphics(void)
{
	X_width = SCREENWIDTH * multiply;
    X_height = SCREENHEIGHT * multiply;
	// SDL3 stuff added in.
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		I_Error("SDL_Init failed: %s", SDL_GetError());
	}
	int windowFlags = 0;
	window.reset(SDL_CreateWindow("Doombuntu", X_width, X_height, windowFlags));
	if (!window)
	{
		I_Error("SDL_CreateWindow Error: %s", SDL_GetError());
	}
	renderer.reset(SDL_CreateRenderer(window.get(), NULL));
	if (!renderer)
	{
		I_Error("SDL_CreateRenderer Error: %s", SDL_GetError());
	}

	bool sdlFullscreen = M_CheckParm("-fullscreen");
	SDL_SetWindowFullscreen(window.get(), sdlFullscreen);

	surface.reset(SDL_CreateSurfaceFrom(
		SCREENWIDTH,
		SCREENHEIGHT,
		SDL_PIXELFORMAT_INDEX8,
		screens[0],
		SCREENWIDTH
	));

	if (!surface)
	{
		I_Error("SDL_CreateSurfaceFrom Error: %s", SDL_GetError());
	}

	palette.reset(SDL_CreatePalette(256));
	if (!palette)
	{
		I_Error("SDL_CreatePalette Error: %s", SDL_GetError());
	}

	texture.reset(SDL_CreateTexture(
		renderer.get(),
		SDL_PIXELFORMAT_INDEX8,
		SDL_TEXTUREACCESS_STREAMING,
		SCREENWIDTH,
		SCREENHEIGHT
	));

	if (!texture)
	{
		I_Error("SDL_CreateTexture Error: %s", SDL_GetError());
	}
	
	if (!SDL_SetTexturePalette(texture.get(), palette.get()))
		I_Error("SDL_SetTexturePalette failed: %s", SDL_GetError());
	if (!SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST))
		I_Error("SDL_SetTextureScaleMode failed: %s", SDL_GetError());
}

