//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2014 Night Dive Studios, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	DOOM main program (D_DoomMain) and game loop (D_DoomLoop),
//	plus functions to determine game mode (shareware, registered),
//	parse command line parameters, configure game parameters (turbo),
//	and call the startup functions.
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rb_local.h"

#include "config.h"
#include "deh_main.h"
#include "doomdef.h"
#include "doomstat.h"

#include "dstrings.h"
#include "doomfeatures.h"
#include "sounds.h"

#include "d_iwad.h"

#include "z_zone.h"
#include "w_main.h"
#include "w_merge.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"

#include "f_finale.h"
#include "f_wipe.h"

#include "fe_frontend.h"  // haleyjd [SVE]

#include "m_argv.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_misc.h"
#include "m_menu.h"
#include "m_saves.h" // haleyjd [STRIFE]
#include "p_saveg.h"
#include "p_dialog.h" // haleyjd [STRIFE]

#include "i_endoom.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_swap.h"

#include "g_game.h"

#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "net_client.h"
#include "net_dedicated.h"
#include "net_query.h"

#include "p_setup.h"
#include "r_local.h"

#include "d_main.h"

// [SVE] svillarreal
#include "rb_config.h"
#include "m_qstring.h"
#include "i_ffmpeg.h"
#include "i_cpumode.h"
#include "i_platsystem.h"

//
// D-DoomLoop()
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
void D_DoomLoop (void);

static boolean D_AddFile(char *filename);

// Location where savegames are stored

char *          savegamedir;
char *          tmpsavegamedir;

// location of IWAD and WAD files

char *          iwadfile;


boolean         devparm;        // started game with -devparm
boolean         nomonsters;     // checkparm of -nomonsters
boolean         respawnparm;    // checkparm of -respawn
boolean         fastparm;       // checkparm of -fast
boolean         flipparm;       // [STRIFE] haleyjd 20110629: checkparm of -flip
boolean         randomparm;     // [STRIFE] haleyjd 20130915: checkparm of -random

// haleyjd [SVE]: remember starting state of these options
boolean         start_fastparm;
boolean         start_respawnparm;

boolean         showintro = true;   // [STRIFE] checkparm of -nograph, disables intro


//extern int soundVolume;
//extern  int	sfxVolume;
//extern  int	musicVolume;

extern  boolean	inhelpscreens;

skill_t		startskill;
int             startepisode;
int		startmap;
boolean		autostart;
int             startloadgame;

boolean		advancedemo;

// villsa [STRIFE] workparm variable (similar to devparm?)
boolean         workparm = false;

// villsa [STRIFE] stonecold cheat variable
boolean         stonecold = false;

// haleyjd 09/11/10: [STRIFE] Game type variables
boolean         isregistered;
boolean         isdemoversion;

// Store demo, do not accept any inputs
// haleyjd [STRIFE] Unused.
//boolean         storedemo;


char		wadfile[1024];          // primary wad file
char		mapdir[1024];           // directory of development maps

int             show_endoom = 0;
int             graphical_startup = 1;

// If true, startup has completed and the main game loop has started.

static boolean main_loop_started = false;

// fraggle 06/03/11 [STRIFE]: Unused config variable, preserved
// for compatibility:

static int comport = 0;

// fraggle 06/03/11 [STRIFE]: Multiplayer nickname?
char *nickname = NULL;

void D_ConnectNetGame(void);
void D_CheckNetGame(void);


//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents (void)
{
    event_t*    ev;

    // [SVE] svillarreal - we don't want any unwanted inputs to occur before
    // the game even starts/loads
    if(main_loop_started == false)
    {
        return;
    }

    // [SVE]: if in the in-game options menu, do not process input here
    if(FE_InOptionsMenu())
    {
        // clear out any queued events
        while((ev = D_PopEvent()) != NULL);
        return;
    }

    // haleyjd 08/22/2010: [STRIFE] there is no such thing as a "store demo" 
    // version of Strife

    // IF STORE DEMO, DO NOT ACCEPT INPUT
    //if (storedemo)
    //    return;

    while ((ev = D_PopEvent()) != NULL)
    {
        if (M_Responder (ev))
            continue;               // menu ate the event
        G_Responder (ev);
    }
}




//
// D_Display
//  draw current display, possibly wiping it from the previous
//
// wipegamestate can be set to -1 to force a wipe on the next draw
//
// haleyjd 08/23/10: [STRIFE]:
// * Changes to eliminate intermission and change timing of screenwipe
// * 20100901: Added ST_DrawExternal and popupactivestate static variable
// * 20110206: Start wipegamestate at GS_UNKNOWN (STRIFE-TODO: rename?)
//
gamestate_t     wipegamestate = GS_UNKNOWN;
extern  boolean setsizeneeded;
//extern  int             showMessages; [STRIFE] no such variable
void R_ExecuteSetViewSize (void);

void D_Display (void)
{
    static  boolean             viewactivestate = false;
    static  boolean             menuactivestate = false;
    static  boolean             inhelpscreensstate = false;
    static  boolean             popupactivestate = false; // [STRIFE]
    static  boolean             fullscreen = false;
    static  gamestate_t         oldgamestate = -1;
    static  int                 borderdrawcount;
    int                         nowtime;
    int                         tics;
    int                         wipestart;
    int                         y;
    boolean                     done;
    boolean                     wipe;
    boolean                     redrawsbar;

    if (nodrawers)
        return;                    // for comparative timing / profiling

    // edward 20201028: [SVE] reinit the 3D scene if the resolution changed
    if (use3drenderer)
    {
        RB_CheckReInitDrawer();
    }

    // haleyjd 20140902: [SVE] interpolation
    I_TimerStartDisplay();

    // [SVE]: if running in-game options menu, call its drawer.
    if(FE_InOptionsMenu())
    {
        FE_InGameOptionsDrawer();
        I_TimerEndDisplay();
        return;
    }

    redrawsbar = false;
    
    // change the view size if needed
    if (setsizeneeded)
    {
        R_ExecuteSetViewSize ();
        oldgamestate = -1;                      // force background redraw
        borderdrawcount = 3;
    }

    // save the current screen if about to wipe
    if (gamestate != wipegamestate)
    {
        wipe = true;
        wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
    }
    else
        wipe = false;

    // haleyjd: [SVE]
    if(use3drenderer && gamestate != GS_LEVEL)
    {
        dglClearColor(0, 0, 0, 1);
        RB_ClearBuffer(GLCB_COLOR);
    }

    if (gamestate == GS_LEVEL && gametic)
        HU_Erase();

    // do buffered drawing
    switch (gamestate)
    {
    case GS_LEVEL:
        if (!gametic)
            break;
        if (automapactive)
            AM_Drawer ();
        if (wipe || (viewheight != 200 && fullscreen) )
            redrawsbar = true;
        // haleyjd 08/29/10: [STRIFE] Always redraw sbar if menu is/was active
        if (menuactivestate || (inhelpscreensstate && !inhelpscreens))
            redrawsbar = true;              // just put away the help screen
        ST_Drawer (viewheight == 200, redrawsbar );
        fullscreen = viewheight == 200;
        break;
      
     // haleyjd 08/23/2010: [STRIFE] No intermission
     // haleyjd 20140921: [SVE] For Capture the Chalice
     case GS_INTERMISSION:
         WI_Drawer();
         break;

    case GS_FINALE:
        F_Drawer ();
        break;

    case GS_DEMOSCREEN:
        D_PageDrawer ();
        break;
    
    default:
        break;
    }
    
    // draw buffered stuff to screen
    I_UpdateNoBlit ();

    // draw the view directly
    if (gamestate == GS_LEVEL && !automapactive && gametic)
        R_RenderPlayerView (&players[displayplayer]);

    // clean up border stuff
    if (gamestate != oldgamestate && gamestate != GS_LEVEL)
        I_SetPalette (W_CacheLumpName (DEH_String("PLAYPAL"),PU_CACHE));

    // see if the border needs to be initially drawn
    if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL)
    {
        viewactivestate = false;        // view was not active
        R_FillBackScreen ();    // draw the pattern into the back screen
    }

    // see if the border needs to be updated to the screen
    if (gamestate == GS_LEVEL && !automapactive && scaledviewwidth != 320)
    {
        if (menuactive || menuactivestate || !viewactivestate)
        {
            borderdrawcount = 3;
            popupactivestate = false;
        }
        if (borderdrawcount)
        {
            R_DrawViewBorder ();    // erase old menu stuff
            borderdrawcount--;
        }

    }

    if (testcontrols)
    {
        // Box showing current mouse speed

        V_DrawMouseSpeedBox(testcontrols_mousespeed);
    }

    menuactivestate = menuactive;
    viewactivestate = viewactive;
    inhelpscreensstate = inhelpscreens;
    oldgamestate = wipegamestate = gamestate;

    // haleyjd 20120208: [STRIFE] Rogue moved this down to below border drawing
    if (gamestate == GS_LEVEL && gametic)
    {
        HU_Drawer ();
        if(ST_DrawExternal()) 
            popupactivestate = true;
        else if(popupactivestate)
        {
            popupactivestate = false;
            menuactivestate = 1;
        }
    }

    // draw pause pic
    if (paused)
    {
        if (automapactive)
            y = 4;
        else
            y = viewwindowy+4;
        V_DrawPatchDirect(viewwindowx + (scaledviewwidth - 68) / 2, y,
                          W_CacheLumpName (DEH_String("M_PAUSE"), PU_CACHE));
    }


    // menus go directly to the screen
    M_Drawer ();          // menu is drawn even on top of everything
    NetUpdate ();         // send out any new accumulation


    // normal update
    if (!wipe)
    {
        I_FinishUpdate ();              // page flip or blit buffer
        I_TimerEndDisplay();
        return;
    }
    
    // wipe update
    wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);

    wipestart = I_GetTime () - 1;

    do
    {
        do
        {
            nowtime = I_GetTime ();
            tics = nowtime - wipestart;
            I_Sleep(1);
        } while (tics < 3); // haleyjd 08/23/2010: [STRIFE] Changed from == 0 to < 3

        // haleyjd 08/26/10: [STRIFE] Changed to use ColorXForm wipe.
        wipestart = nowtime;
        done = wipe_ScreenWipe(wipe_ColorXForm
                               , 0, 0, SCREENWIDTH, SCREENHEIGHT, tics);
        I_UpdateNoBlit ();
        M_Drawer ();                            // menu is drawn even on top of wipes
        I_FinishUpdate ();                      // page flip or blit buffer
    } while (!done);

    I_TimerEndDisplay();
}

//
// Add configuration file variable bindings.
//

void D_BindVariables(void)
{
    int i;

    M_ApplyPlatformDefaults();

    I_BindVideoVariables();
    I_BindJoystickVariables();
    I_BindSoundVariables();

    M_BindBaseControls();
    M_BindWeaponControls();
    M_BindMapControls();
    M_BindMenuControls();
    M_BindStrifeControls(); // haleyjd 09/01/10: [STRIFE]
    M_BindChatControls(MAXPLAYERS);

    // [SVE] svillarreal - bind variables for hardware renderer
    RB_BindVariables();

    // haleyjd 20130915: Strife chat keys
    key_multi_msgplayer[0] = '1';
    key_multi_msgplayer[1] = '2';
    key_multi_msgplayer[2] = '3';
    key_multi_msgplayer[3] = '4';
    key_multi_msgplayer[4] = '5';
    key_multi_msgplayer[5] = '6';
    key_multi_msgplayer[6] = '7';
    key_multi_msgplayer[7] = '8';

#ifdef FEATURE_MULTIPLAYER
    NET_BindVariables();
#endif

    // haleyjd 08/29/10: [STRIFE]
    // * Added voice volume
    // * Added back flat
    // * Removed show_messages
    // * Added show_talk
    // fraggle 03/06/10: [STRIFE]
    // * Removed detailLevel
    // * screenblocks -> screensize
    // * Added nickname, comport

    M_BindVariable("mouse_sensitivity_X",    &mouseSensitivityX);
    M_BindVariable("mouse_sensitivity_Y",    &mouseSensitivityY);
    M_BindVariable("sfx_volume",             &sfxVolume);
    M_BindVariable("music_volume",           &musicVolume);
    M_BindVariable("voice_volume",           &voiceVolume); 
    M_BindVariable("show_talk",              &dialogshowtext);
    M_BindVariable("screensize",             &screenblocks);
    M_BindVariable("snd_channels",           &snd_channels);
    M_BindVariable("vanilla_savegame_limit", &vanilla_savegame_limit);
    M_BindVariable("vanilla_demo_limit",     &vanilla_demo_limit);
    M_BindVariable("show_endoom",            &show_endoom);
    M_BindVariable("back_flat",              &back_flat);
    M_BindVariable("graphical_startup",      &graphical_startup);
    M_BindVariable("interpolate_frames",     &d_interpolate);
    M_BindVariable("skip_movies",            &d_skipmovies);
    M_BindVariable("max_gore",               &d_maxgore);
    M_BindVariable("classicmode",            &classicmode);
    M_BindVariable("weapon_recoil",          &d_recoil);
    M_BindVariable("damage_indicator",       &d_dmgindictor);
    M_BindVariable("autoaim",                &autoaim);
    M_BindVariable("autorun",                &autorun);
    M_BindVariable("fullscreen_hud",         &fullscreenhud);

    M_BindVariable("nickname",               &nickname);
    M_BindVariable("comport",                &comport);

    // Multiplayer chat macros

    for (i=0; i<10; ++i)
    {
        char buf[12];

        M_snprintf(buf, sizeof(buf), "chatmacro%i", i);
        M_BindVariable(buf, &chat_macros[i]);
    }

    // [SVE]: bind unsaved runtime-only vars
    M_BindVariable("deathmatch",  &deathmatch);
    M_BindVariable("fastparm",    &fastparm);
    M_BindVariable("nomonsters",  &nomonsters);
    M_BindVariable("randomparm",  &randomparm);
    M_BindVariable("respawnparm", &respawnparm);
    M_BindVariable("startmap",    &startmap);
    M_BindVariable("startskill",  &startskill);
    M_BindVariable("timelimit",   &timelimit);
    M_BindVariable("d_fpslimit",  &d_fpslimit);

    // [SVE]: Gyroscope
    M_BindVariable("joy_gyroscope", &joy_gyroscope);
    M_BindVariable("joy_gyrostyle", &joy_gyrostyle);
    M_BindVariable("joy_gyrosensitivityh", &joy_gyrosensitivityh);
    M_BindVariable("joy_gyrosensitivityv", &joy_gyrosensitivityv);
}

//
// D_GrabMouseCallback
//
// Called to determine whether to grab the mouse pointer
//

boolean D_GrabMouseCallback(void)
{
    // Drone players don't need mouse focus

    if (drone)
        return false;

    // when menu is active or game is paused, release the mouse.
    if (menuactive || paused)
        return false;

    // only grab mouse when playing levels (but not demos)

    return (gamestate == GS_LEVEL) && !demoplayback;
}

// During startup, never grab the mouse.

static boolean D_StartupGrabCallback(void)
{
    return false;
}

//
// haleyjd 20141007: [SVE] Added mechanism to tell the low level code when and
// when not to warp the mouse to the lower right corner when releasing it. For
// our purposes, that's when the menus are active.
//
static boolean D_WarpMouseCallback(void)
{
    return !(menuactive || FE_InFrontend());
}

static int d_ticcount;
boolean    d_fpslimit = false;

//
// D_updateTics
//
// haleyjd 20140925: [SVE]
// Update timing information
//
static void D_updateTics(void)
{
    d_ticcount = I_GetTimeMS();
}

//
// D_capFrameRate
//
// haleyjd 20140904: [SVE]
// Choco's sound engine won't tolerate the main loop running at full blast 
// and turns into chop city. We must give up a non-trivial amount of CPU
// time on a consistent basis.
//
static boolean D_capFrameRate(void)
{
    float frameMillis = 1000.0f / 60.0f;
    int   curTics = I_GetTimeMS();
    float elapsed = (float)(curTics - d_ticcount);
    
    if(elapsed < frameMillis)
    {
        if(frameMillis - elapsed > 3.0f)
            I_Sleep(2);
        return true;
    }

    return false;
}

static boolean dofrontend = true;

//
//  D_DoomLoop
//
//  haleyjd 08/23/10: [STRIFE] Verified unmodified.
//
void D_DoomLoop (void)
{
    if (demorecording)
        G_BeginRecording ();

    TryRunTics();

    if(!showintro && !dofrontend) // [SVE]
    {
        I_SetWindowTitle(gamedescription);
        I_InitGraphics();
    }

    I_EnableLoadingDisk();
    I_SetGrabMouseCallback(D_GrabMouseCallback);
    I_SetWarpMouseCallback(D_WarpMouseCallback);

    V_RestoreBuffer();
    R_ExecuteSetViewSize();

    D_StartGameLoop();

    if (testcontrols)
    {
        wipegamestate = gamestate;
    }

    // haleyjd: [SVE]
    if (autostart)
        main_loop_started = true;

    while (1)
    {
        D_updateTics();

        // frame syncronous IO operations
        I_StartFrame();

        // process one or more tics
        TryRunTics(); // will run at least one tic

        S_UpdateSounds(players[consoleplayer].mo);// move positional sounds

        // Update display, next frame, with current state.
        D_Display();

        // Must cap framerate if interpolating
        if(d_interpolate && d_fpslimit)
        {
            while(D_capFrameRate());
        }
    }
}



//
//  DEMO LOOP
//
int             demosequence;
int             pagetic;
const char     *pagename;

// From the original instruction manual:
static char storytext[] =
   "You are a wandering mercenary led to the small "
   "town of Tarnhill by rumors of conflict between "
   "the Order, a well-equipped religious monarchy, "
   "and the Front, the rag tag resistance movement.  "
   "While searching for the Front you decided to take "
   "a brief rest somewhere that you thought was safe.  "
   "The Order's Acolytes have been rounding up all "
   "suspicious characters in the area.    \n"
   "Yes, you happen to be one of them.    \n"
   "What they didn't expect, though, is the knife you "
   "keep concealed for situations just like this one... ";

#define TEXTSPEED 3
static int storycount;


//
// D_PageTicker
// Handles timing for warped projection
//
// haleyjd 08/22/2010: [STRIFE] verified unmodified
//
void D_PageTicker (void)
{
    if (--pagetic < 0)
        D_AdvanceDemo ();

    if(!strcmp(pagename, "STRBACK"))
        ++storycount;
}

//
// D_TextWrite
//
// haleyjd 20141027: [SVE] Do backstory text
//
static void D_TextWrite(void)
{
    int       count;
    size_t    len;
    int       cx;
    int       cy;
    qstring_t text;
    static boolean formatted = false;

    if(!formatted)
    {
        M_DialogDimMsg(10, 30, storytext, false);
        formatted = true;
    }

    QStrInitCreate(&text);

    // title
    V_WriteBigText("The Story So Far...", 10, 6);

    cx = 10;
    cy = 30;

    QStrCopy(&text, storytext);
    count = (storycount - 10) / TEXTSPEED;
    if(count < 0)
        count = 0;
    len = (size_t)count;

    if(len < QStrLen(&text))
        QStrTruncate(&text, len);
    
    M_WriteText(cx, cy, QStrConstPtr(&text));

    QStrFree(&text);
}

//
// D_PageDrawer
//
// haleyjd 08/22/2010: [STRIFE] verified unmodified
// edward 09/26/2021: [SVE] Widescreen page support
//
void RB_PageDrawer(const char* szPagename, const int xoff);
void D_PageDrawer (void)
{
    patch_t* patch = W_CacheLumpName(pagename, PU_CACHE);
    const int xoff = -((patch->width - SCREENWIDTH) / 2);

    if (use3drenderer && (xoff < 0))
    {
        RB_PageDrawer(pagename, xoff);
    }
    else
    {
        V_DrawPatch(xoff, 0, patch);
    }

    if(!strcmp(pagename, "STRBACK"))
        D_TextWrite();
}


//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
// haleyjd 08/22/2010: [STRIFE] verified unmodified
//
void D_AdvanceDemo (void)
{
    advancedemo = true;
}

// haleyjd 20140904: [SVE] part of fix for QFMRM7
static int quittics;

//
// This cycles through the demo sequences.
// FIXME - version dependend demo numbers?
//
// [STRIFE] Modified for the opening slideshow and the exit screen
//
void D_DoAdvanceDemo (void)
{
    players[consoleplayer].playerstate = PST_LIVE;  // not reborn
    advancedemo = false;
    usergame = false;               // no save / end game here
    paused = false;
    gameaction = ga_nothing;

    // [SVE] svillarreal - moved here
    main_loop_started = true;
    
    // villsa 09/12/10: [STRIFE] converted pagetics to ticrate
    switch (demosequence)
    {
    case -5: // exit the game
        I_Quit();
        return;
    case -4: // show exit screen
        menuactive = false;
        pagetic = quittics;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("PANEL7");
        S_StartMusic(mus_fast);
        if(isdemoversion)
            demosequence = -3; // show Velocity logo
        else
            demosequence = -5; // exit
        return;
    case -3: // show Velocity logo for demo version
        pagetic = 6*TICRATE;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("vellogo");
        demosequence = -5; // exit
        return;
    case -2: // title screen
        pagetic = 6*TICRATE;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("TITLEPIC");
        S_StartMusic(mus_logo);
        demosequence = -1; // start intro cinematic
        return;
    case -1: // start of intro cinematic
        pagetic = 10;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("PANEL0");
        S_StartSound(NULL, sfx_rb2act);
        wipegamestate = -1;
        break;
    case 0: // Rogue logo
        pagetic = 4*TICRATE;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("RGELOGO");
        wipegamestate = -1;
        break;
    case 1:
        pagetic = 7*TICRATE;              // The comet struck our planet without
        gamestate = GS_DEMOSCREEN;        // warning.We lost our paradise in a 
        pagename = DEH_String("PANEL1");  // single, violent stroke.
        I_StartVoice(DEH_String("pro1")); 
        S_StartMusic(mus_intro);
        break;
    case 2:
        pagetic = 9*TICRATE;              // The impact released a virus which 
        gamestate = GS_DEMOSCREEN;        // swept through the land and killed 
        pagename = DEH_String("PANEL2");  // millions. They turned out to be 
        I_StartVoice(DEH_String("pro2")); // the lucky ones...
        break;
    case 3:
        pagetic = 12*TICRATE;             // For those that did not die became 
        gamestate = GS_DEMOSCREEN;        // mutations of humanity. Some became
        pagename = DEH_String("PANEL3");  // fanatics who heard the voice of a
        I_StartVoice(DEH_String("pro3")); // malignant God in their heads, and 
        break;                            // called themselves the Order.
    case 4:
        pagetic = 11*TICRATE;             // Those of us who were deaf to this
        pagename = DEH_String("PANEL4");  // voice suffer horribly and are 
        gamestate = GS_DEMOSCREEN;        // forced to serve these ruthless
        I_StartVoice(DEH_String("pro4")); // psychotics, who wield weapons more
        break;                            // powerful than anything we can muster.
    case 5:
        pagetic = 10*TICRATE;             // They destroy our women and children,
        gamestate = GS_DEMOSCREEN;        // so that we must hide them underground,
        pagename = DEH_String("PANEL5");  // and live like animals in constant
        I_StartVoice(DEH_String("pro5")); // fear for our lives.
        break;
    case 6:                               // But there are whispers of discontent.
        pagetic = 16*TICRATE;             // If we organize, can we defeat our
        gamestate = GS_DEMOSCREEN;        // masters? Weapons are being stolen,
        pagename = DEH_String("PANEL6");  // soldiers are being trained. A 
        I_StartVoice(DEH_String("pro6")); // Movement is born! Born of lifelong 
        break;                            // STRIFE!
    case 7: // titlepic again - unused...
        pagetic = 9*TICRATE;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("TITLEPIC");
        wipegamestate = -1;
        break;
    case 8: // demo
        // [SVE]
        storycount = 0;
        S_ChangeMusic(mus_dark, 1);
        pagename = "STRBACK";
        pagetic = 58*TICRATE;
        wipegamestate = -1;
        /*
        ClearTmp();
        pagetic = 9*TICRATE;
        G_DeferedPlayDemo(DEH_String("demo1"));
        */
        break;
    case 9: // velocity logo? - unused...
        pagetic = 6*TICRATE;
        gamestate = GS_DEMOSCREEN;
        pagename = DEH_String("vellogo");
        wipegamestate = -1;
        break;
    case 10: // credits
        gamestate = GS_DEMOSCREEN;
        pagetic = 12*TICRATE;
        pagename = DEH_String("CREDIT");
        wipegamestate = -1;
        break;
    default:
        break;
    }

    ++demosequence;

    if(demosequence > 11)
        demosequence = -2;
    if(demosequence == 7 || demosequence == 9)
        ++demosequence;
}



//
// D_StartTitle
//
// [STRIFE]
// haleyjd 09/11/10: Small modifications for new demo sequence.
//
void D_StartTitle (void)
{
    gamestate = GS_DEMOSCREEN;
    gameaction = ga_nothing;
    demosequence = -2;
    D_AdvanceDemo ();
}

//
// D_QuitGame
//
// [STRIFE] New function
// haleyjd 09/11/10: Sets up the quit game snippet powered by the
// demo sequence.
// haleyjd 20140904: [SVE] added tics param to fix QFMRM7
//
void D_QuitGame(int tics)
{
    gameaction = ga_nothing;
    demosequence = -4;
    quittics = tics; // [SVE] set quittics
    D_AdvanceDemo();
}

// Strings for dehacked replacements of the startup banner
//
// These are from the original source: some of them are perhaps
// not used in any dehacked patches

static char *banners[] =
{
    // strife1.wad:

    "                      "
    "STRIFE:  Quest for the Sigil v1.2"
    "                                 "
};

//
// Get game name: if the startup banner has been replaced, use that.
// Otherwise, use the name given
// 

static char *GetGameName(char *gamename)
{
    size_t i;
    const char *deh_sub;
    
    for (i=0; i<arrlen(banners); ++i)
    {
        // Has the banner been replaced?

        deh_sub = DEH_String(banners[i]);
        
        if (deh_sub != banners[i])
        {
            size_t gamename_size;

            // Has been replaced
            // We need to expand via printf to include the Doom version 
            // number
            // We also need to cut off spaces to get the basic name

            gamename_size = strlen(deh_sub) + 10;
            gamename = Z_Malloc(gamename_size, PU_STATIC, 0);
            M_snprintf(gamename, gamename_size, deh_sub,
                       STRIFE_VERSION / 100, STRIFE_VERSION % 100);

            while (gamename[0] != '\0' && isspace(gamename[0]))
            {
                memmove(gamename, gamename + 1, gamename_size - 1);
            }

            while (gamename[0] != '\0' && isspace(gamename[strlen(gamename)-1]))
                gamename[strlen(gamename) - 1] = '\0';

            return gamename;
        }
    }

    return gamename;
}

//
// D_loadResourceWad
//
// haleyjd 20140821: [SVE] Broke out of D_IdentifyVersion to support loading
// multiple wad files.
//
static char *D_loadResourceWad(char *filename)
{
    // try to find on WAD paths first
    char *name = D_FindWADByName(filename);

    if(!name) // not found?
    {
        int p;

        // If -iwad was used, check and see if exists on the same filepath.
        if((p = M_CheckParm("-iwad")) && p < myargc - 1)
        {
            char   *iwad     = myargv[p + 1];
            size_t  len      = strlen(iwad) + 1;
            char   *iwadpath = Z_Malloc(len, PU_STATIC, NULL);

            // extract base path of IWAD parameter
            M_GetFilePath(iwad, iwadpath, len);

            // concatenate with filename
            name = M_SafeFilePath(iwadpath, filename);
            Z_Free(iwadpath);

            if(!M_FileExists(name))
            {
                Z_Free(name);
                name = NULL;
            }
        }
    }

    return name;
}

//
// Find out what version of Doom is playing.
//
void D_IdentifyVersion(void)
{
    // gamemission is set up by the D_FindIWAD function.  But if 
    // we specify '-iwad', we have to identify using 
    // IdentifyIWADByName.  However, if the iwad does not match
    // any known IWAD name, we may have a dilemma.  Try to 
    // identify by its contents.
    
    // STRIFE-TODO: some elaborate checks? for now we assume...
    // The logic in strife1.exe is simple:
    // * if strife1.wad is found, set isregistered = true
    // * if strife0.wad is found, set isdemoversion = true

    // Make sure gamemode is set up correctly
    gamemode = commercial;
    gamemission = strife;
    isregistered = true;

    if(isregistered)
    {
        char *name;
        
        // Load voices.wad 
        if(!(name = D_loadResourceWad("voices.wad")))
            disable_voices = 1;

        if(disable_voices) // voices disabled?
        {
            if(devparm)
                 printf("Voices disabled\n");
        }
        else
            D_AddFile(name);

        // [SVE] load resource wad with new SVE stuff
        if(!(name = D_loadResourceWad("SVE.wad")))
            I_Error("D_IdentifyVersion: Cannot find SVE.wad file");

        printf(" adding %s\n", name);
        W_MergeFile(name);
    }
}

#if 0
//
// DoTimeBomb
//
// haleyjd 08/23/2010: [STRIFE] New function
// Code with no xrefs; probably left over from a private alpha or beta.
// Translated here because it explains what the SERIAL lump was meant to do.
//
void DoTimeBomb(void)
{
    dosdate_t date;
    char *serial;
    int serialnum;
    int serial_year;
    int serial_month;

    serial = W_CacheLumpName("serial", PU_CACHE);
    serialnum = atoi(serial);

    // Rogue, much like Governor Mourel, were lousy liars. These deceptive
    // error messages are pretty low :P
    dos_getdate(&date);
    if(date.year > 1996 || date.day > 15 && date.month > 4)
        I_Error("Data error! Corrupted WAD File!");
    serial_year = serialnum / 10000;
    serial_month = serialnum / 100 - 100 * serial_year;
    if(date.year < serial_year || 
       date.day < serialnum - 100 * serial_month - 10000 * serial_year &&
       date.month < serial_month)
       I_Error("Bad wadfile");
}
#endif

// Set the gamedescription string

void D_SetGameDescription(void)
{
    gamedescription = GetGameName("Strife: Quest for the Sigil");
}

//      print title for every printed line
char            title[128];

static boolean D_AddFile(char *filename)
{
    wad_file_t *handle;

    printf(" adding %s\n", filename);
    handle = W_AddFile(filename);

    return handle != NULL;
}

// Copyright message banners
// Some dehacked mods replace these.  These are only displayed if they are 
// replaced by dehacked.
// haleyjd 08/22/2010: [STRIFE] altered to match strings from binary
static char *copyright_banners[] =
{
    "===========================================================================\n"
    "ATTENTION:  This version of STRIFE has extra files added to it.\n"
    "        You will not receive technical support for modified games.\n"
    "===========================================================================\n",

    "===========================================================================\n"
    "             This version is NOT SHAREWARE, do not distribute!\n"
    "         Please report software piracy to the SPA: 1-800-388-PIR8\n"
    "===========================================================================\n",

    "===========================================================================\n"
    "                                Shareware!\n"
    "===========================================================================\n"
};

// Prints a message only if it has been modified by dehacked.

void PrintDehackedBanners(void)
{
    size_t i;

    for (i=0; i<arrlen(copyright_banners); ++i)
    {
        const char *deh_s;

        deh_s = DEH_String(copyright_banners[i]);

        if (deh_s != copyright_banners[i])
        {
            printf("%s", deh_s);

            // Make sure the modified banner always ends in a newline character.
            // If it doesn't, add a newline.  This fixes av.wad.

            if (deh_s[strlen(deh_s) - 1] != '\n')
            {
                printf("\n");
            }
        }
    }
}

static struct 
{
    char *description;
    char *cmdline;
    GameVersion_t version;
} gameversions[] = {
    { "Strife 1.2",          "1.2",       exe_strife_1_2  },
    { "Strife 1.31",         "1.31",      exe_strife_1_31 },
    { NULL,                  NULL,        0               }
};

// Initialize the game version

static void InitGameVersion(void)
{
    int p;
    int i;

    // haleyjd: we support emulating either the 1.2 or the 1.31 versions of 
    // Strife, which are the most significant. 1.2 is the most mature version
    // that still has the single saveslot restriction, whereas 1.31 is the
    // final revision. The differences between the two are barely worth
    // mentioning aside from that main one.

    //!
    // @arg <version>
    // @category compat
    //
    // Emulate a specific version of Strife. Valid values are "1.2" and "1.31".
    //

    p = M_CheckParmWithArgs("-gameversion", 1);

    if (p)
    {
        for (i=0; gameversions[i].description != NULL; ++i)
        {
            if (!strcmp(myargv[p+1], gameversions[i].cmdline))
            {
                gameversion = gameversions[i].version;
                break;
            }
        }

        if (gameversions[i].description == NULL) 
        {
            printf("Supported game versions:\n");

            for (i=0; gameversions[i].description != NULL; ++i)
            {
                printf("\t%s (%s)\n", gameversions[i].cmdline,
                        gameversions[i].description);
            }

            I_Error("Unknown game version '%s'", myargv[p+1]);
        }
    }
    else
    {
        gameversion = exe_strife_1_31;
    }
}

void PrintGameVersion(void)
{
    int i;

    for (i=0; gameversions[i].description != NULL; ++i)
    {
        if (gameversions[i].version == gameversion)
        {
            printf("Emulating the behavior of the "
                   "'%s' executable.\n", gameversions[i].description);
            break;
        }
    }
}

// Function called at exit to display the ENDOOM screen

static void D_Endoom(void)
{
    byte *endoom;

    // Don't show ENDOOM if we have it disabled, or we're running
    // in screensaver or control test mode. Only show it once the
    // game has actually started.


    if (!show_endoom || !main_loop_started || screensaver_mode || testcontrols)
    {
        return;
    }

    // haleyjd 08/27/10: [STRIFE] ENDOOM -> ENDSTRF
    endoom = W_CacheLumpName(DEH_String("ENDSTRF"), PU_STATIC);

    I_Endoom(endoom);
}

//=============================================================================
//
// haleyjd: Chocolate Strife Specifics
//
// None of the code in here is from the original executable, but is needed for
// other reasons.

//
// D_PatchClipCallback
//
// haleyjd 08/28/10: Clip patches to the framebuffer without errors.
// Returns false if V_DrawPatch should return without drawing.
//
boolean D_PatchClipCallback(patch_t *patch, int x, int y)
{
    // note that offsets were already accounted for in V_DrawPatch
    return (x + SHORT(patch->width) >= 0 && y + SHORT(patch->height) >= 0
            && x <= SCREENWIDTH && y <= SCREENHEIGHT);
}

//
// D_InitChocoStrife
//
// haleyjd 08/28/10: Take care of some Strife-specific initialization
// that is necessitated by Chocolate Doom issues, such as setting global 
// callbacks.
//
static void D_InitChocoStrife(void)
{
    // set the V_DrawPatch clipping callback
    V_SetPatchClipCallback(D_PatchClipCallback);
}

//
// haleyjd 20141002: [SVE] Graphical frontend
//

static void D_InitFrontend()
{
    // several command line parameters disable the frontend:
    if(M_CheckParm("-nofrontend")    || // intentional skip
       M_CheckParm("-devparm")       || // dev mode
       M_CheckParm("-warp")          || // warping
       M_CheckParm("-playdemo")      || // play demo
       M_CheckParm("-record")        || // record demo
       M_CheckParm("-server")        || // UDP server modes
       M_CheckParm("-privateserver") ||
       M_CheckParm("-autojoin")      || // UDP client modes
       M_CheckParm("-connect")       ||
       M_CheckParm("-drone")         ||
       M_CheckParm("-solo-net"))
    {
        dofrontend = false;
    }

    // init graphics
    I_GraphicsCheckCommandLine();

    if(dofrontend)
    {
        I_SetWindowTitle(gamedescription);
        I_SetGrabMouseCallback(D_StartupGrabCallback);
        I_SetWarpMouseCallback(D_WarpMouseCallback);
        I_InitGraphics();
        V_RestoreBuffer(); // make the V_ routines work
        V_LoadXlaTable();  // need XLATAB for software screen wipe
    }

    // haleyjd 08/28/10: Init Choco Strife stuff.
    D_InitChocoStrife();

    // haleyjd 20110924: moved S_Init up to here
    if(devparm) // [STRIFE]
        DEH_printf("S_Init: Setting up sound.\n");
    S_Init (sfxVolume * 8, musicVolume * 8, voiceVolume * 8); // [STRIFE]: voice

    // haleyjd 20110210: Create Strife hub save folders
    M_CreateSaveDirs(savegamedir);

    I_InitJoystick();
    
    if(devparm)
        DEH_printf("HU_Init: Setting up heads up display.\n");
    HU_Init ();

    if(dofrontend)
    {
        // Edward [SVE]: Signal to the platform that we want to handle quit events
        I_SetupPlatformQuit();

    }
    else
    {
        // Edward [SVE]: Signal to the platform that we want to handle quit events
        I_SetupPlatformQuit();
    }
}

//
// STRIFE Graphical Intro Sequence
//

#define MAXINTROPROGRESS 69

static int introprogress;        // track the progress of the intro

static byte *rawgfx_startup0;    // raw linear gfx for intro
static byte *rawgfx_startp[4];
static byte *rawgfx_startlz[2];
static byte *rawgfx_startbot;

//
// D_IntroBackground
//
// [STRIFE] New function
// haleyjd 20110206: Strife only drew this once, but for supporting double-
// buffered or page-flipped surfaces it is best to redraw the entire screen
// every frame.
//
static void D_IntroBackground(void)
{
    if(!showintro)
        return;

    // Edward - clear buffer for 3d render mode
    if (use3drenderer)
    {
        RB_ClearBuffer(GLCB_ALL);
    }

    // Fill the background entirely (wasn't needed in vanilla)
    V_DrawFilledBox(0, 0, SCREENWIDTH, SCREENHEIGHT, 0);

    // Strife cleared the screen somewhere in the low-level code between the
    // intro and the titlescreen, so this is to take care of that and get
    // proper fade-in behavior on the titlescreen
    if(introprogress >= MAXINTROPROGRESS)
    {
        I_FinishUpdate();
        return;
    }

    // Draw a 95-pixel rect from STARTUP0 starting at y=57 to (0,41) on the
    // screen (this was a memcpy directly to 0xA3340 in low DOS memory)
    V_DrawBlock(0, 41, 320, 95, rawgfx_startup0 + (320*57));
}

//
// D_InitIntroSequence
//
// [STRIFE] New function
// haleyjd 20110206: Initialize the graphical introduction sequence
//

static void D_InitIntroSequence(void)
{
    if(showintro)
    {
        // In vanilla Strife, Mode 13h was initialized directly in D_DoomMain.
        // We have to be a little more courteous of the low-level code here.
        // [SVE]: only do this here if not running the frontend.
        if(!dofrontend)
        {
            I_SetWindowTitle(gamedescription);
            I_SetGrabMouseCallback(D_StartupGrabCallback);
            I_InitGraphics();
            V_RestoreBuffer(); // make the V_ routines work
        }

        // Load all graphics
        rawgfx_startup0   = W_CacheLumpName("STARTUP0", PU_STATIC);
        rawgfx_startp[0]  = W_CacheLumpName("STRTPA1",  PU_STATIC);
        rawgfx_startp[1]  = W_CacheLumpName("STRTPB1",  PU_STATIC);
        rawgfx_startp[2]  = W_CacheLumpName("STRTPC1",  PU_STATIC);
        rawgfx_startp[3]  = W_CacheLumpName("STRTPD1",  PU_STATIC);
        rawgfx_startlz[0] = W_CacheLumpName("STRTLZ1",  PU_STATIC);
        rawgfx_startlz[1] = W_CacheLumpName("STRTLZ2",  PU_STATIC);
        rawgfx_startbot   = W_CacheLumpName("STRTBOT",  PU_STATIC);

        // Draw the background
        D_IntroBackground();
    }
    /*
    // STRIFE-FIXME: This was actually displayed on a special textmode
    // screen with ANSI color codes... would require use of textlib to
    // emulate properly...
    else
    {
        puts(DEH_String("Conversation ON"));
        puts(DEH_String("Rogue Entertainment"));
        puts(DEH_String("and"));
        puts(DEH_String("Velocity Games"));
        puts(DEH_String("present"));
        puts(DEH_String("S T R I F E"));
        puts(DEH_String("Loading..."));
    }
    */
}

//
// D_DrawIntroSequence
//
// [STRIFE] New function
// haleyjd 20110206: Refresh the intro sequence
//
static void D_DrawIntroSequence(void)
{
    int laserpos;
    int robotpos;

    if(!showintro)
        return;

    D_IntroBackground(); // haleyjd: refresh the background

    // Laser position
    laserpos = (200 * introprogress / MAXINTROPROGRESS) + 60;

    // BUG: (?) Due to this clip, the laser never even comes close to
    // touching the peasant; confirmed with vanilla. This MAY have been
    // intentional, for effect, however, since no death frames are shown 
    // either... kind of a black-out death.
    if(laserpos > 200)
        laserpos = 200;

    // Draw the laser
    // Blitted 16 bytes for 16 rows starting at 705280 + laserpos
    // (705280 - 0xA0000) / 320 == 156
    V_DrawBlock(laserpos, 156, 16, 16, rawgfx_startlz[laserpos % 2]);

    // Robot position
    robotpos = laserpos % 5 - 2;

    // Draw the robot
    // Blitted 48 bytes for 48 rows starting at 699534 + (320*robotpos)
    // 699534 - 0xA0000 == 44174, which % 320 == 14, / 320 == 138
    V_DrawBlock(14, 138 + robotpos, 48, 48, rawgfx_startbot);

    // Draw the peasant
    // Blitted 32 bytes for 64 rows starting at 699142
    // 699142 - 0xA0000 == 43782, which % 320 == 262, / 320 == 136
    V_DrawBlock(262, 136, 32, 64, rawgfx_startp[laserpos % 4]);

    I_FinishUpdate();
}

//
// D_IntroTick
//
// Advance the intro sequence
//
void D_IntroTick(void)
{
    static boolean didsound = false; // haleyjd 20120209
    
    if(devparm)
        return;

    ++introprogress;
    if(introprogress >= MAXINTROPROGRESS)
    {
        D_IntroBackground(); // haleyjd: clear the bg anyway
        
        // haleyjd 20120209: This isn't 100% true to vanilla because vanilla
        // would play this sound a half-dozen times. BUT, in vanilla, for 
        // whatever reason, under DMX, playing the same sound multiple times
        // doesn't add up violently like it does under SDL_mixer. This means
        // that without this one-time limitation, the sound is far too loud.
        if(!didsound)
        {
            S_StartSound(NULL, sfx_psdtha);
            didsound = true;
        }
    }
    else
        D_DrawIntroSequence();
}

//
// End Chocolate Strife Specifics
//
//=============================================================================

// haleyjd [SVE]: cheat state tracking in D_DoomMain
enum
{
    CHEAT_NONE = 0,    // not cheating
    CHEAT_SP   = 0x01, // cheating if in single player
    CHEAT_MP   = 0x02, // cheating if in multiplayer
    
    CHEAT_ANY  = (CHEAT_SP|CHEAT_MP) // cheating regardless
};

//
// D_DoomMain
//
void D_DoomMain (void)
{
    int  p;
    char file[256];
    char demolumpname[9];
    int setcheating = CHEAT_NONE; // haleyjd [SVE] 20140914
#ifndef SVE_PLAT_SWITCH
    I_AtExit(D_Endoom, false);
#endif

    // haleyjd 20110206 [STRIFE]: -nograph parameter

    //!
    // @vanilla
    //
    // Disable graphical introduction sequence
    //

    if (M_ParmExists("-nograph"))
        showintro = false;

    // Undocumented:
    // Invoked by setup to test the controls.

    if (M_ParmExists("-testcontrols"))
    {
        testcontrols = true;
        showintro = false;
        setcheating |= CHEAT_ANY;
    }

    // haleyjd 20110206: Moved up -devparm for max visibility

    //!
    // @vanilla
    //
    // Developer mode. Implies -nograph.
    //

    devparm = M_CheckParm ("-devparm");
    if(devparm)
        setcheating |= CHEAT_ANY;


#ifdef SVE_PLAT_SWITCH
	// dimitris 20200614: nx mount resources on first call
	
	SDL_GetBasePath(); // mount rom:
	SDL_GetPrefPath("Nightdive Studios", PACKAGE); // mount save:

#endif

    // print banner

    I_PrintBanner(PACKAGE_STRING);

    //DEH_printf("Z_Init: Init zone memory allocation daemon. \n"); [STRIFE] removed
    Z_Init ();

#ifdef FEATURE_MULTIPLAYER
    //!
    // @category net
    //
    // Start a dedicated server, routing packets but not participating
    // in the game itself.
    //

    if (M_CheckParm("-dedicated") > 0)
    {
        printf("Dedicated server mode.\n");
        NET_DedicatedServer();

        // Never returns
    }

    //!
    // @category net
    //
    // Query the Internet master server for a global list of active
    // servers.
    //

    if (M_CheckParm("-search"))
    {
        NET_MasterQuery();
        exit(0);
    }

    //!
    // @arg <address>
    // @category net
    //
    // Query the status of the server running on the given IP
    // address.
    //

    p = M_CheckParmWithArgs("-query", 1);

    if (p)
    {
        NET_QueryAddress(myargv[p+1]);
        exit(0);
    }

    //!
    // @category net
    //
    // Search the local LAN for running servers.
    //

    if (M_CheckParm("-localsearch"))
    {
        NET_LANQuery();
        exit(0);
    }

#endif
            
#ifdef FEATURE_DEHACKED
    if(devparm)
        printf("DEH_Init: Init Dehacked support.\n");
    DEH_Init();
#endif

    //!
    // @vanilla
    //
    // Disable monsters.
    //

    nomonsters = M_CheckParm ("-nomonsters");
    if(nomonsters)
        setcheating |= CHEAT_SP;

    //!
    // @vanilla
    //
    // Set Rogue playtesting mode (godmode, noclip toggled by backspace)
    //

    workparm = M_CheckParm ("-work");
    if(workparm)
        setcheating |= CHEAT_ANY;

    //!
    // @vanilla
    //
    // Flip player gun sprites (broken).
    //

    flipparm = M_CheckParm ("-flip");

    //!
    // @vanilla
    //
    // Respawn monsters after they are killed.
    //

    respawnparm = M_CheckParm ("-respawn");

    //!
    // @vanilla
    //
    // Items respawn at random locations
    //

    randomparm = M_CheckParm ("-random");

    //!
    // @vanilla
    //
    // Monsters move faster.
    //

    fastparm = M_CheckParm ("-fast");

    I_DisplayFPSDots(devparm);

    // haleyjd 20110206 [STRIFE]: -devparm implies -nograph
    if(devparm)
        showintro = false;

    // Note: Vanilla Strife does not understand the -deathmatch command
    // line parameter. deathmatch=1 is the default behavior when
    // playing a netgame.

    //!
    // @category net
    // @vanilla
    //
    // Start a deathmatch game.  Weapons do not stay in place and
    // all items respawn after 30 seconds.
    //

    if (M_CheckParm ("-altdeath"))
        deathmatch = 2;

    if (devparm)
        DEH_printf(D_DEVSTR);
    
    // find which dir to use for config files

#ifdef _WIN32

    //!
    // @platform windows
    // @vanilla
    //
    // Save configuration data and savegames in c:\strife.cd,
    // allowing play from CD.
    //

    if (M_CheckParm("-cdrom") > 0)
    {
        printf(D_CDROM);

        // haleyjd 08/22/2010: [STRIFE] Use strife.cd folder for -cdrom
        M_SetConfigDir("c:\\strife.cd\\");
    }
    else
#endif
    {
        // Auto-detect the configuration dir.

        M_SetConfigDir(NULL);
    }
    
    //!
    // @arg <x>
    // @vanilla
    //
    // Turbo mode.  The player's speed is multiplied by x%.  If unspecified,
    // x defaults to 200.  Values are rounded up to 10 and down to 400.
    //

    if ( (p=M_CheckParm ("-turbo")) )
    {
        int     scale = 200;
        extern int forwardmove[2];
        extern int sidemove[2];

        if (p<myargc-1)
            scale = atoi (myargv[p+1]);
        if (scale < 10)
            scale = 10;
        if (scale > 400)
            scale = 400;
        DEH_printf("turbo scale: %i%%\n", scale);
        forwardmove[0] = forwardmove[0]*scale/100;
        forwardmove[1] = forwardmove[1]*scale/100;
        sidemove[0] = sidemove[0]*scale/100;
        sidemove[1] = sidemove[1]*scale/100;
        setcheating |= CHEAT_ANY;
    }

    timelimit = 0;

    //! 
    // @arg <n>
    // @category net
    // @vanilla
    //
    // For multiplayer games: exit each level after n minutes.
    //

    p = M_CheckParmWithArgs("-timer", 1);

    if (p)
    {
        timelimit = atoi(myargv[p+1]);
        printf("timer: %i\n", timelimit);
    }

    //!
    // @category net
    // @vanilla
    //
    // Austin Virtual Gaming: end levels after 20 minutes.
    //

    p = M_CheckParm ("-avg");

    if (p)
    {
        timelimit = 20;
    }
    
    // init subsystems
    // DEH_printf("V_Init: allocate screens.\n"); [STRIFE] removed
    V_Init ();

    // Load configuration files before initialising other subsystems.
    // haleyjd 08/22/2010: [STRIFE] - use strife.cfg
    // DEH_printf("M_LoadDefaults: Load system defaults.\n"); [STRIFE] removed
    M_SetConfigFilenames("strife.cfg", PROGRAM_PREFIX "strife.cfg");
    D_BindVariables();
    M_LoadDefaults();

    if (!graphical_startup)
    {
        showintro = false;
    }

    // Save configuration at exit.
    I_AtExit(M_SaveDefaults, false);

    // Find the main IWAD file and load it.
    iwadfile = D_FindIWAD(IWAD_MASK_STRIFE, &gamemission);

    // None found?
    if (iwadfile == NULL)
    {
        I_Error("Game mode indeterminate.  No IWAD file was found.  Try\n"
                "specifying one with the '-iwad' command line parameter.\n");
    }

    modifiedgame = false;

    if(devparm) // [STRIFE] Devparm only
        DEH_printf("W_Init: Init WADfiles.\n");
    D_AddFile(iwadfile);
    W_CheckCorrectIWAD(strife);
    D_IdentifyVersion(); // haleyjd 20140911: [SVE] moved up here
    modifiedgame = W_ParseCommandLine();
    if(modifiedgame)
        setcheating |= CHEAT_ANY;

    // [STRIFE] serial number output
    if(devparm)
    {
        char msgbuf[80];
        char *serial  = W_CacheLumpName("SERIAL", PU_CACHE);
        int serialnum = atoi(serial);

        DEH_snprintf(msgbuf, sizeof(msgbuf), "Wad Serial Number: %d:", serialnum);
        printf("%s\n", msgbuf);
    }

    // add any files specified on the command line with -file wadfile
    // to the wad list
    //

    // Debug:
//    W_PrintDirectory();

    //!
    // @arg <demo>
    // @category demo
    // @vanilla
    //
    // Play back the demo named demo.lmp.
    //

    p = M_CheckParmWithArgs ("-playdemo", 1);

    if (!p)
    {
        //!
        // @arg <demo>
        // @category demo
        // @vanilla
        //
        // Play back the demo named demo.lmp, determining the framerate
        // of the screen.
        //
	p = M_CheckParmWithArgs("-timedemo", 1);

    }

    if (p)
    {
        // With Vanilla you have to specify the file without extension,
        // but make that optional.
        if (M_StringEndsWith(myargv[p + 1], ".lmp"))
        {
            M_StringCopy(file, myargv[p + 1], sizeof(file));
        }
        else
        {
            DEH_snprintf(file, sizeof(file), "%s.lmp", myargv[p+1]);
        }

        if (D_AddFile (file))
        {
            M_StringCopy(demolumpname, lumpinfo[numlumps - 1].name,
                         sizeof(demolumpname));
        }
        else
        {
            // If file failed to load, still continue trying to play
            // the demo in the same way as Vanilla Doom.  This makes
            // tricks like "-playdemo demo1" possible.

            M_StringCopy(demolumpname, myargv[p + 1], sizeof(demolumpname));
        }
        setcheating |= CHEAT_ANY; // no achievements during demo playback.

        printf("Playing demo %s.\n", file);
    }

    I_AtExit((atexit_func_t) G_CheckDemoStatus, true);

    // Generate the WAD hash table.  Speed things up a bit.

    W_GenerateHashTable();
    
    V_LoadBigFont(); // haleyjd 20140928: [SVE]

    InitGameVersion();
    D_SetGameDescription();
    savegamedir = M_GetSaveGameDir("strife1.wad");
    tmpsavegamedir = M_GetTmpSaveGameDir("strife1.wad");

    // fraggle 20130405: I_InitTimer is needed here for the netgame
    // startup. Start low-level sound init here too.
    I_InitTimer();
    I_InitSound(true);
    I_InitMusic();

#ifdef FEATURE_MULTIPLAYER
    if(devparm) // [STRIFE]
        printf ("NET_Init: Init network subsystem.\n");
    NET_Init();
#endif

    // get skill / episode / map from parms
    startskill = sk_easy; // [STRIFE] inits to sk_easy
    startepisode = 1;
    startmap = 1;
    autostart = false;

    // haleyjd 20141002: [SVE]: frontend
    D_InitFrontend();

    D_ConnectNetGame();

    // haleyjd 20141008: [SVE]: remember some startup state
    start_respawnparm = respawnparm;
    start_fastparm    = fastparm;

    // haleyjd 20110206 [STRIFE] Startup the introduction sequence
    D_InitIntroSequence();

    D_IntroTick(); // [STRIFE]

    // Check for -file in shareware
    if (modifiedgame)
    {
        // These are the lumps that will be checked in IWAD,
        // if any one is not present, execution will be aborted.
        // haleyjd 08/22/2010: [STRIFE] Check for Strife lumps.
        char name[3][8]=
        {
            "map23", "map30", "ROB3E1"
        };
        int i;

        // haleyjd 08/22/2010: [STRIFE] Changed string to match binary
        // STRIFE-FIXME: Needs to test isdemoversion variable
        if ( gamemode == shareware)
            I_Error(DEH_String("\nYou cannot -file with the demo "
                               "version. You must buy the real game!"));

        // Check for fake IWAD with right name,
        // but w/o all the lumps of the registered version. 
        // STRIFE-FIXME: Needs to test isregistered variable
        if (gamemode == registered)
            for (i = 0; i < 3; i++)
                if (W_CheckNumForName(name[i])<0)
                    I_Error(DEH_String("\nThis is not the registered version."));
    }
    
    D_IntroTick(); // [STRIFE]

    //!
    // @arg <skill>
    // @vanilla
    //
    // Set the game skill, 1-5 (1: easiest, 5: hardest).  A skill of
    // 0 disables all monsters.
    //

    p = M_CheckParmWithArgs("-skill", 1);

    if (p)
    {
        startskill = myargv[p+1][0]-'1';
        autostart = true;
    }

    // [STRIFE] no such thing in Strife
    //
    // // @arg <n>
    // // @vanilla
    // //
    // // Start playing on episode n (1-4)
    // //

    // p = M_CheckParmWithArgs("-episode", 1);

    // if (p)
    // {
    //     startepisode = myargv[p+1][0]-'0';
    //     startmap = 1;
    //     autostart = true;
    // }

    //!
    // @arg x
    // @vanilla
    //
    // Start a game immediately, warping to level x.
    //

    p = M_CheckParmWithArgs("-warp", 1);

    if (p)
    {
        if (gamemode == commercial)
            startmap = atoi (myargv[p+1]);
        else
        {
            startepisode = myargv[p+1][0]-'0';

            if (p + 2 < myargc)
            {
                startmap = myargv[p+2][0]-'0';
            }
            else
            {
                startmap = 1;
            }
        }
        autostart = true;
        setcheating |= CHEAT_SP; // cheating if SP game
    }

    if (testcontrols)
    {
        startepisode = 1;
        startmap = 3;
        autostart = true;
    }

    // Check for load game parameter
    // We do this here and save the slot number, so that the network code
    // can override it or send the load slot to other players.

    //!
    // @arg <s>
    // @vanilla
    //
    // Load the game in slot s.
    //

    p = M_CheckParmWithArgs("-loadgame", 1);
    
    if (p)
    {
        startloadgame = atoi(myargv[p+1]);
    }
    else
    {
        // Not loading a game
        startloadgame = -1;
    }

    if (W_CheckNumForName("SS_START") >= 0
     || W_CheckNumForName("FF_END") >= 0)
    {
        I_PrintDivider();
        printf(" WARNING: The loaded WAD file contains modified sprites or\n"
               " floor textures.  You may want to use the '-merge' command\n"
               " line option instead of '-file'.\n");
    }

    I_PrintStartupBanner(gamedescription);
    PrintDehackedBanners();

    // haleyjd 08/22/2010: [STRIFE] Modified string to match binary
    if(devparm) // [STRIFE]
       DEH_printf("R_Init: Loading Graphics - ");
    R_Init ();
    D_IntroTick(); // [STRIFE]

    if(devparm) // [STRIFE]
        DEH_printf("\nP_Init: Init Playloop state.\n");
    P_Init ();
    D_IntroTick(); // [STRIFE]

    if(devparm) // [STRIFE]
        DEH_printf("I_Init: Setting up machine state.\n");
    I_CheckIsScreensaver();
    D_IntroTick(); // [STRIFE]

    D_IntroTick(); // [STRIFE]

    if(devparm) // [STRIFE]
        DEH_printf("M_Init: Init Menu.\n");
    M_Init ();
    D_IntroTick(); // [STRIFE]

    // haleyjd 20110924: Moved S_Init up.
    D_IntroTick();

    // haleyjd 20110220: This stuff was done in I_StartupSound in vanilla, but 
    // we'll do it here instead so we don't have to modify the low-level shared
    // code with Strife-specific stuff.

    //!
    // @vanilla
    //
    // Disable voice dialog and show dialog as text instead,
    // even if voices.wad can be found.
    //

    if(disable_voices || M_CheckParm("-novoice"))
    {
        dialogshowtext = disable_voices = 1;
    }

    if(devparm)
        DEH_printf("  Play voices = %d\n", disable_voices == 0);

    if(devparm) // [STRIFE]
        DEH_printf("D_CheckNetGame: Checking network game status.\n");
    D_CheckNetGame ();

    PrintGameVersion();

    D_IntroTick(); // [STRIFE]

    if(devparm)
        DEH_printf("ST_Init: Init status bar.\n");
    ST_Init ();
    D_IntroTick(); // [STRIFE]

    // haleyjd [STRIFE] -statcopy used to be here...
    D_IntroTick(); // [STRIFE]

    // If Doom II without a MAP01 lump, this is a store demo.  
    // Moved this here so that MAP01 isn't constantly looked up
    // in the main loop.
    // haleyjd 08/23/2010: [STRIFE] There is no storedemo version of Strife
    /*
    if (gamemode == commercial && W_CheckNumForName("map01") < 0)
        storedemo = true;
    */

    // haleyjd [SVE] 20140914: propagate local cheat tracking to global state
    if(netgame)
        d_cheating = ((setcheating & CHEAT_MP) == CHEAT_MP);
    else
        d_cheating = ((setcheating & CHEAT_SP) == CHEAT_SP);

    //!
    // @arg <x>
    // @category demo
    // @vanilla
    //
    // Record a demo named x.lmp.
    //

    p = M_CheckParmWithArgs("-record", 1);

    if (p)
    {
        G_RecordDemo (myargv[p+1]);
        autostart = true;
    }
    D_IntroTick(); // [STRIFE]

    p = M_CheckParmWithArgs("-playdemo", 1);
    if (p)
    {
        singledemo = true;              // quit after one demo
        G_DeferedPlayDemo (demolumpname);
        D_DoomLoop ();  // never returns
    }
    D_IntroTick(); // [STRIFE]

    p = M_CheckParmWithArgs("-timedemo", 1);
    if (p)
    {
        G_TimeDemo (demolumpname);
        D_DoomLoop ();  // never returns
    }
    D_IntroTick(); // [STRIFE]

    if (startloadgame >= 0)
    {
        // [STRIFE]: different, for hubs
        M_LoadSelect(startloadgame);
    }
    D_IntroTick(); // [STRIFE]

    // Edward [SVE]: Slow things back down
    I_SetCPUHighPerformance(0);


    if (gameaction != ga_loadgame )
    {
        if (autostart || netgame)
            G_InitNew (startskill, startmap);
        else
            D_StartTitle ();                // start up intro loop
    }

    D_DoomLoop ();  // never returns
}
