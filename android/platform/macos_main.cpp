/*
 *  macos_main.cpp -- the macOS entry point: window, GL context and frame loop.
 *
 *  The counterpart of android_main.cpp.  Android gets its surface from
 *  NativeActivity and its context from EGL; macOS gets both from SDL2, which
 *  also gives us the Cocoa run loop, mouse and keyboard without any Objective-C.
 *
 *  Two modes, as on Android:
 *    RUN_CONSOLE -- the boot report, drawn by CBootConsole.
 *    RUN_GAME    -- a5_game_step() drives the engine, which renders through the
 *                   D3D9 shim and calls back into HookPresent() to swap.
 *  Press TAB to switch between them when the game started.
 *
 *  Note the GL profile: macOS offers 2.1 (legacy) or 3.2+ *core* and nothing
 *  between.  The engine's 175 translated shaders need GLSL 330, so core it is --
 *  which is why the boot console grew a VAO/VBO, core-profile shaders and an
 *  R8 font texture under A5_MACOS.
 *
 *      silentstorm_mac [game-data-directory]
 */
#define GL_SILENCE_DEPRECATION 1

#include <SDL.h>
#include <OpenGL/gl3.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "boot_harness.h"
#include "gles_present.h"
#include "data_mount.h"

#ifdef A5_HAVE_MAIN
#include "game_entry.h"
#include "a5_input.h"
extern "C" void a5_audio_log_stats( void );   // [macos] platform/audio_android.cpp
#include "d3d9.h"
/*  Implemented in compat/src/win32compat.cpp; the engine's cursor reads it. */
extern "C" void a5_set_pointer_position( long x, long y );
#endif

namespace
{

const int kDefaultWidth  = 1024;
const int kDefaultHeight = 768;

SDL_Window *g_pWindow   = 0;
int         g_nDrawW    = kDefaultWidth;   /* drawable, in pixels   */
int         g_nDrawH    = kDefaultHeight;
float       g_fPointToPixel = 1.0f;        /* Retina: 2.0 typically */
bool        g_bAlive    = true;
bool        g_bFullscreen = false;

/*  SDL reports mouse motion in window POINTS; the renderer works in drawable
 *  PIXELS.  On a Retina display those differ by 2x, and feeding points straight
 *  through makes the in-game cursor travel at half speed -- it drifts up and to
 *  the left of the real pointer. */
void RefreshDrawableMetrics()
{
    if ( !g_pWindow )
        return;
    SDL_GL_GetDrawableSize( g_pWindow, &g_nDrawW, &g_nDrawH );
    int nPointW = 0, nPointH = 0;
    SDL_GetWindowSize( g_pWindow, &nPointW, &nPointH );
    g_fPointToPixel = ( nPointW > 0 ) ? (float)g_nDrawW / (float)nPointW : 1.0f;
}

#ifdef A5_HAVE_MAIN
/*  The four hooks the D3D shim needs from the platform.  On Android these wrap
 *  EGL; here they wrap SDL.  The shim never touches the context itself. */
int  HookWindowWidth()  { return g_nDrawW; }
int  HookWindowHeight() { return g_nDrawH; }
void HookPresent()      { if ( g_pWindow ) SDL_GL_SwapWindow( g_pWindow ); }
int  HookSurfaceAlive() { return g_bAlive ? 1 : 0; }

/*  SDL -> Android keycodes, for the keys the engine's Bind.cpp actually reads.
 *  Only the common ones; anything unmapped is ignored rather than guessed. */
int SdlToAndroidKey( SDL_Keycode k )
{
    if ( k >= SDLK_a && k <= SDLK_z ) return 29 + ( k - SDLK_a );   /* KEYCODE_A  */
    if ( k >= SDLK_0 && k <= SDLK_9 ) return  7 + ( k - SDLK_0 );   /* KEYCODE_0  */
    switch ( k )
    {
        case SDLK_ESCAPE:     return 111;  /* KEYCODE_ESCAPE    */
        case SDLK_RETURN:     return  66;  /* KEYCODE_ENTER     */
        case SDLK_SPACE:      return  62;  /* KEYCODE_SPACE     */
        case SDLK_TAB:        return  61;  /* KEYCODE_TAB       */
        case SDLK_BACKSPACE:  return  67;  /* KEYCODE_DEL       */
        case SDLK_LEFT:       return  21;  /* KEYCODE_DPAD_LEFT */
        case SDLK_RIGHT:      return  22;
        case SDLK_UP:         return  19;
        case SDLK_DOWN:       return  20;
        case SDLK_LSHIFT:     return  59;  /* KEYCODE_SHIFT_LEFT */
        case SDLK_RSHIFT:     return  60;
        case SDLK_LCTRL:      return 113;  /* KEYCODE_CTRL_LEFT  */
        case SDLK_RCTRL:      return 114;
        /*  [macos] The engine's control table already carries F1..F12 under the
         *  DirectInput names the retail config binds against ("F5" quicksave,
         *  "F10" menu) -- they were simply never produced here, so every
         *  function key was swallowed and the in-mission menu looked dead. */
        case SDLK_LALT:       return  57;  /* KEYCODE_ALT_LEFT   */
        case SDLK_RALT:       return  58;
        case SDLK_HOME:       return 122;  /* KEYCODE_MOVE_HOME  */
        case SDLK_END:        return 123;
        case SDLK_PAGEUP:     return  92;
        case SDLK_PAGEDOWN:   return  93;
        case SDLK_INSERT:     return 124;
        case SDLK_DELETE:     return 112;  /* KEYCODE_FORWARD_DEL */
        case SDLK_MINUS:      return  69;
        case SDLK_EQUALS:     return  70;
        case SDLK_LEFTBRACKET:  return 71;
        case SDLK_RIGHTBRACKET: return 72;
        case SDLK_BACKSLASH:  return  73;
        case SDLK_SEMICOLON:  return  74;
        case SDLK_QUOTE:      return  75;
        case SDLK_SLASH:      return  76;
        case SDLK_COMMA:      return  55;
        case SDLK_PERIOD:     return  56;
        case SDLK_BACKQUOTE:  return  68;  /* KEYCODE_GRAVE      */
        default:
            /*  SDL_SCANCODE_F1..F12 are contiguous (58..69), so the keycodes
             *  are too; AKEYCODE_F1..F12 are 131..142. */
            if ( k >= SDLK_F1 && k <= SDLK_F12 )
                return 131 + ( k - SDLK_F1 );
            return 0;
    }
}
#endif

void PrintReport( const SBootReport &report )
{
    for ( size_t i = 0; i < report.lines.size(); ++i )
    {
        const SBootLine &line = report.lines[ i ];
        const char *pszPrefix = "";
        switch ( line.status )
        {
            case BOOT_OK:      pszPrefix = "  ok   "; break;
            case BOOT_WARN:    pszPrefix = "  warn "; break;
            case BOOT_FAIL:    pszPrefix = "  FAIL "; break;
            case BOOT_DETAIL:  pszPrefix = "       "; break;
            case BOOT_HEADING: pszPrefix = "== ";     break;
        }
        printf( "%s%s\n", pszPrefix, line.szText.c_str() );
    }
    printf( "\n%d passed, %d failed, %d warnings\n",
            report.nPassed, report.nFailed, report.nWarnings );
}

} // namespace

extern "C" int a5_export_geometry( const char *pszPath, const char *pszOutBase );

int main( int argc, char **argv )
{
    /*  A5_EXPORT_GEOM=<geometry file> dumps that mesh as Wavefront OBJ and
     *  exits without starting the game.  The file is one of the numbered
     *  entries in the Geometries resource (the source release ships them
     *  unpacked under Complete/Geometries/).  A5_EXPORT_OUT sets the output
     *  base path; bone weights are written beside it as a text table.  */
    if ( const char *pszGeom = getenv( "A5_EXPORT_GEOM" ) )
    {
        const char *pszOut = getenv( "A5_EXPORT_OUT" );
        return a5_export_geometry( pszGeom, pszOut ? pszOut : "/tmp/ss_model" );
    }
    const char *pszDataRoot = argc > 1 ? argv[ 1 ] : "";
    for ( int i = 1; i < argc; ++i )
        if ( strcmp( argv[ i ], "--fullscreen" ) == 0 )
            g_bFullscreen = true;

    if ( argc > 1 && strcmp( argv[ 1 ], "--help" ) == 0 )
    {
        printf( "usage: %s [game-data-directory]\n", argv[ 0 ] );
        return 0;
    }

    if ( SDL_Init( SDL_INIT_VIDEO ) != 0 )
    {
        fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
        return 1;
    }

    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

    g_pWindow = SDL_CreateWindow(
        "Silent Storm -- macOS port",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kDefaultWidth, kDefaultHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_ALLOW_HIGHDPI |
        ( g_bFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0 ) );
    if ( !g_pWindow )
    {
        fprintf( stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError() );
        SDL_Quit();
        return 1;
    }

    SDL_GLContext ctx = SDL_GL_CreateContext( g_pWindow );
    if ( !ctx )
    {
        fprintf( stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError() );
        SDL_DestroyWindow( g_pWindow );
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent( g_pWindow, ctx );
    SDL_GL_SetSwapInterval( 1 );
    RefreshDrawableMetrics();

    printf( "Silent Storm -- macOS port\n" );
    printf( "GL_VERSION  : %s\n", (const char*)glGetString( GL_VERSION ) );
    printf( "GL_RENDERER : %s\n", (const char*)glGetString( GL_RENDERER ) );
    printf( "GLSL        : %s\n", (const char*)glGetString( GL_SHADING_LANGUAGE_VERSION ) );
    printf( "data root   : %s\n\n", pszDataRoot[ 0 ] ? pszDataRoot : "(none given)" );

    const SBootReport report = RunBootHarness( "", "", pszDataRoot );
    PrintReport( report );

    CBootConsole console;
    const bool bConsole = console.Init();
    if ( bConsole )
    {
        console.SetReport( report );
        console.SetViewport( g_nDrawW, g_nDrawH );
    }

    bool bGameRunning = false;
#ifdef A5_HAVE_MAIN
    if ( report.nFailed == 0 )
    {
        A5D3DPlatformHooks hooks = { HookWindowWidth, HookWindowHeight,
                                     HookPresent, HookSurfaceAlive };
        A5D3DSetPlatformHooks( &hooks );

        /*  The shipped start.cfg plays .\cfg\intro.seq -- a Bink video, and Bink
         *  is the proprietary middleware the source release does not include --
         *  and has "mainmenu" commented out.  Followed literally that leaves the
         *  engine with nothing on screen.  Default to a cfg that goes straight
         *  to the menu; A5_START_CFG in the environment still wins (the 0 flag
         *  means "do not overwrite"). */
        setenv( "A5_START_CFG", "mac_mainmenu.cfg", 0 );
        printf( "game: start cfg = %s\n", getenv( "A5_START_CFG" ) );

        const char *pszError = 0;
        const int nResult = a5_game_init( &pszError );
        if ( nResult != 0 )
            fprintf( stderr, "\ngame: init failed (%d): %s\n",
                     nResult, pszError ? pszError : "?" );
        else
        {
            bGameRunning = true;
            /*  The engine draws its own cursor; the OS one on top of it is a
             *  distracting double pointer.  Re-shown when TAB returns to the
             *  boot console, and on exit. */
            SDL_ShowCursor( SDL_DISABLE );
            printf( "\ngame: init OK -- running.  F12 switches to the boot report.\n" );
        }
    }
    else
        printf( "\ngame: not starting (%d checks failed)\n", report.nFailed );
#else
    printf( "\ngame: built without the Main module (-DA5_BUILD_MAIN=ON to enable)\n" );
#endif

    bool bRunning  = true;
    bool bShowGame = bGameRunning;
    Uint64 nPrev   = SDL_GetPerformanceCounter();
    const double fFreq = (double)SDL_GetPerformanceFrequency();

    while ( bRunning )
    {
        SDL_Event ev;
        while ( SDL_PollEvent( &ev ) )
        {
            switch ( ev.type )
            {
                case SDL_QUIT:
                    bRunning = false;
                    break;

                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    const bool bDown = ( ev.type == SDL_KEYDOWN );
                    if ( bDown && ev.key.keysym.sym == SDLK_ESCAPE && !bShowGame )
                        bRunning = false;
                    /*  [macos] The boot report used to toggle on TAB, which the
                     *  game binds to hero_next -- so cycling squad members
                     *  popped up the debug overlay instead.  F12 is free: the
                     *  retail input.cfg binds F1..F10 and F11 is fullscreen. */
                    else if ( bDown && ev.key.keysym.sym == SDLK_F12 && bGameRunning )
                    {
                        bShowGame = !bShowGame;
                        SDL_ShowCursor( bShowGame ? SDL_DISABLE : SDL_ENABLE );
                    }
                    /*  Fullscreen: F11, or Cmd+F as macOS users expect.
                     *  FULLSCREEN_DESKTOP keeps the display mode and just fills
                     *  the screen, so there is no mode switch to go wrong --
                     *  the renderer already letterboxes to the game's aspect. */
                    else if ( bDown && ( ev.key.keysym.sym == SDLK_F11 ||
                                         ( ev.key.keysym.sym == SDLK_f &&
                                           ( ev.key.keysym.mod & KMOD_GUI ) ) ) )
                    {
                        g_bFullscreen = !g_bFullscreen;
                        SDL_SetWindowFullscreen( g_pWindow,
                            g_bFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0 );
                        RefreshDrawableMetrics();
                        if ( bConsole )
                            console.SetViewport( g_nDrawW, g_nDrawH );
                        printf( "fullscreen: %s (%dx%d)\n",
                                g_bFullscreen ? "on" : "off", g_nDrawW, g_nDrawH );
                    }
#ifdef A5_HAVE_MAIN
                    else if ( bShowGame )
                    {
                        const int nKey = SdlToAndroidKey( ev.key.keysym.sym );
                        if ( nKey )
                            a5_input_key( nKey, bDown ? 1 : 0 );
                    }
#endif
                    break;
                }

#ifdef A5_HAVE_MAIN
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    if ( bShowGame )
                    {
                        int nButton = 0;
                        if ( ev.button.button == SDL_BUTTON_RIGHT )  nButton = 1;
                        if ( ev.button.button == SDL_BUTTON_MIDDLE ) nButton = 2;
                        a5_input_mouse_button( nButton,
                                               ev.type == SDL_MOUSEBUTTONDOWN ? 1 : 0 );
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if ( bShowGame )
                    {
                        /*  ABSOLUTE positioning, as android_main does for touch.
                         *  Relative deltas can only ever approximate the real
                         *  pointer: any rounding drifts and never self-corrects,
                         *  which is why the cursor lagged up and to the left.
                         *  A5D3DWindowToBackBuffer maps a window pixel through
                         *  the Present scaling, so this is also correct in
                         *  fullscreen, where the image is letterboxed. */
                        const float fWinX = ev.motion.x * g_fPointToPixel;
                        const float fWinY = ev.motion.y * g_fPointToPixel;
                        float fBackX = 0.0f, fBackY = 0.0f;
                        if ( A5D3DWindowToBackBuffer( fWinX, fWinY, &fBackX, &fBackY ) )
                            a5_set_pointer_position( (long)fBackX, (long)fBackY );
                    }
                    break;
#endif

                case SDL_MOUSEWHEEL:
#ifdef A5_HAVE_MAIN
                    if ( bShowGame )
                    {
                        /*  Two-finger trackpad panning.  macOS delivers a two-finger
                         *  swipe as a stream of wheel events with fractional precise
                         *  deltas (a real wheel notch arrives as +/-1.0), and the
                         *  engine already knows how to pan from a gesture: the Android
                         *  build feeds A5CameraMotion.panX/panY -- fractions of the
                         *  viewport height -- and CCamera::Update applies them against
                         *  the visible span.  Nothing fed it on macOS, so the camera
                         *  could only be moved by pushing the pointer into a screen
                         *  edge.  Signs follow the map-under-the-fingers convention.
                         *  A5_TRACKPAD_PAN=0 disables it for mouse-only setups.  */
                        static const float fPanScale = []{
                            const char *pszEnv = getenv( "A5_TRACKPAD_PAN" );
                            return pszEnv ? (float)atof( pszEnv ) : 0.02f;
                        }();
                        if ( fPanScale != 0.0f &&
                             ( ev.wheel.preciseX != 0.0f || ev.wheel.preciseY != 0.0f ) )
                        {
                            A5CameraMotion motion;
                            motion.panX = -ev.wheel.preciseX * fPanScale;
                            motion.panY = -ev.wheel.preciseY * fPanScale;
                            a5_input_camera_motion( &motion );
                        }
                        /*  Still report the notch so UI lists keep scrolling.  */
                        a5_input_wheel( ev.wheel.y * 120 );
                    }
                    else
#endif
                    if ( bConsole )
                        console.Scroll( (float)ev.wheel.y * -40.0f );
                    break;

                case SDL_WINDOWEVENT:
                    if ( ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                         ev.window.event == SDL_WINDOWEVENT_RESIZED )
                    {
                        RefreshDrawableMetrics();
                        if ( bConsole )
                            console.SetViewport( g_nDrawW, g_nDrawH );
                    }
                    break;

                default:
                    break;
            }
        }

        const Uint64 nNow = SDL_GetPerformanceCounter();
        const double fElapsed = (double)( nNow - nPrev ) / fFreq;
        nPrev = nNow;

#ifdef A5_HAVE_MAIN
        if ( bShowGame && bGameRunning )
        {
            /*  [macos] The mixer reports what it is actually doing -- voices,
             *  streams, output rate, peak level.  Worth having in the log at a
             *  low rate: "peak 0.00" is the difference between a device that
             *  opened and a device that is making sound. */
            static int nAudioStatsTick = 0;
            if ( ++nAudioStatsTick >= 600 )
            {
                nAudioStatsTick = 0;
                a5_audio_log_stats();
            }
            /*  The engine renders through the D3D shim and calls HookPresent()
             *  itself, so this branch must not swap or clear. */
            if ( !a5_game_step( 1 ) )
            {
                printf( "game: asked to exit\n" );
                a5_game_shutdown();
                bGameRunning = false;
                bShowGame    = false;
            }
            continue;
        }
#endif

        glViewport( 0, 0, g_nDrawW, g_nDrawH );
        glClearColor( 0.05f, 0.06f, 0.09f, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        if ( bConsole )
            console.Render( fElapsed );
        SDL_GL_SwapWindow( g_pWindow );
    }

    g_bAlive = false;
    SDL_ShowCursor( SDL_ENABLE );
#ifdef A5_HAVE_MAIN
    if ( bGameRunning )
        a5_game_shutdown();
#endif
    if ( bConsole )
        console.Shutdown();
    SDL_GL_DeleteContext( ctx );
    SDL_DestroyWindow( g_pWindow );
    SDL_Quit();
    return report.nFailed ? 1 : 0;
}
