Retrometer 9x 0.1.0-beta.1 - FPS counter and frame limiter for Windows 95/98/ME
===============================================================================

Small proxy DLLs that sit in a game's folder, pass every call through to the
real driver, and draw a frames-per-second number in the top-left corner.
Press F11 (configurable) to show or hide it.

No C runtime, no DirectX SDK, no installer. Each DLL imports only KERNEL32,
USER32 and GDI32 functions that exist on Windows 95, and is built for
Pentium-class CPUs (no CMOV/SSE), so it also runs on K6-2 / Pentium MMX boxes.

  File           For games that use                 Install
  -------------  ---------------------------------  -------------------------
  retrometer.exe    the manager (see below)             run it from its own folder
  opengl32.dll   OpenGL (incl. 3dfx MiniGL)         copy into game folder
  glide2x.dll    3dfx Glide 2.x                     copy into game folder
  glide3x.dll    3dfx Glide 3.x                     copy into game folder
  d3d8.dll       Direct3D 8                         copy into game folder
  fpsdd.dll      DirectDraw and Direct3D 3/5/6/7    copy + run rmpatch.exe
  rmpatch.exe   (helper for fpsdd.dll)

Using the manager (retrometer.exe)
----------------------------------
Unzip everything into a folder of its own, e.g. C:\Program Files\Retrometer,
and run retrometer.exe from there (make a shortcut to it if you like). That
folder is the tool's home: it keeps the DLLs it installs from, its list of
games (games.ini) and the "All games" settings (allgames.ini).

Add game...
  Pick the game's main .exe. Retrometer scans the game folder (the exe and any
  DLLs beside it) and shows, for each overlay:
    "Used by quake.exe"            - imported directly
    "Loaded at run time (ref_gl)"  - the name appears in a game file; it is
                                     loaded with LoadLibrary (Quake 2 etc.)
  Detected overlays are ticked. If the game ships its own driver DLL
  (MiniGL opengl32.dll, a Glide wrapper glide2x.dll, ...) the right-hand box
  suggests what to rename it to, normally <name>a.dll (glide2xa.dll); if
  that name is taken it picks the next free one (glide2xb.dll) and points
  the overlay at it with a Real= line. You can edit the name. Nothing is
  touched until you confirm the list of changes.

  For DirectDraw it patches every exe/dll in the folder that imports
  DDRAW.DLL (a .rmbak backup of each is kept).

Settings
  Select "<All games>" to edit the default settings, or a game to see its
  details. A game uses the All games settings until you untick "Use the All
  games settings for this game"; then it has its own. Saving All games
  rewrites retrometer.ini in every game that follows it. Settings always live in
  each game's own folder (retrometer.ini), which is what the DLLs read, and a
  running game picks up a save within about a second.

Change overlays...
  Same dialog: tick to add another overlay, untick to remove one.

Remove from game
  Deletes everything Retrometer 9x added (DLLs, retrometer.ini, log, backups, driver
  copies), gives the game's own DLLs back their original names, restores
  patched exes, and takes the game off the list. If the game folder has
  gone, it just takes it off the list.

Repair
  A game patch or reinstall can overwrite the overlay DLL or the patched
  exe. The list marks such games with [!] and Details says what's wrong.
  Repair puts the overlay back, re-patches, and if the game dropped a newer
  copy of its own driver DLL, offers to keep that one instead of the old.

Open folder opens the game folder in Explorer.

Frame rate cap
  Limits a game to a set frame rate: handy for old games that run too fast,
  tear, or work a Voodoo harder than needed. It is in the settings like
  everything else (All games or per game):
    Cap on when the game starts / Limit to  - the cap and its fps
    Cap hotkey (default F12)                 - with Ctrl/Alt/Shift if wanted
    Hotkey does: "Turn cap on/off"           - toggles the "Limit to" value
                 "Step: off, then each"      - Off -> 30 -> 60 -> 85 -> Off
                                               (edit the Steps list)
  Pressing the hotkey shows "CAP 60" / "CAP OFF" for two seconds, even with
  the counter hidden. Changing the cap in the manager while a game runs
  takes effect within a second.

  It times frames with the high-resolution timer and sleeps in 1 ms steps,
  finishing with a short spin, so the rate is accurate (50 means 50.0) and
  costs very little CPU. Notes:
    * DirectDraw: only fullscreen (page-flipping) games are capped; windowed
      DirectDraw games are left alone because they may copy to the screen
      several times per frame.
    * Games whose speed is tied to frame rate will slow down - which is
      usually the point.
    * With vsync on, a cap above the refresh rate has no effect.

The manual way (no manager) is described below.

Not sure which API a game uses? Look in its folder and its readme, or try the
DLLs one at a time. Only one is needed per game.


Installing
----------

OpenGL, Glide 2, Glide 3, Direct3D 8
  Copy the matching DLL into the folder that holds the game's .exe. That's
  it. (Settings: write an retrometer.ini there; see Settings below.)

  Windows 9x can't load two DLLs with the same name, so on first run the proxy
  copies the real driver from C:\WINDOWS\SYSTEM into the game folder under a
  new name and chains to that copy:

      opengl32.dll -> glreal.dll      glide2x.dll -> g2real.dll
      glide3x.dll  -> g3real.dll      d3d8.dll    -> d3d8real.dll

  If you update your video driver, delete the *real.dll copy so a fresh one is
  made.

  Game ships its own driver DLL (MiniGL, Glide wrapper, etc.):
    rename the game's file by adding an "a" before .dll, then copy the proxy in:

      glide2x.dll  -> glide2xa.dll      glide3x.dll -> glide3xa.dll
      opengl32.dll -> opengl32a.dll     d3d8.dll    -> d3d8a.dll

    The proxy always uses the "a" file first, so the game keeps its own driver
    and the system copy is never made. (Renaming to g2real.dll etc. also works,
    as does Real= in retrometer.ini for any other name.)

  Lookup order for each proxy:
    1. Real= in retrometer.ini
    2. <name>a.dll in the game folder (the game's own driver, renamed)
    3. g2real.dll / g3real.dll / glreal.dll / d3d8real.dll in the game folder
    4. copy of the system driver, made automatically into step 3's name

  Quake-engine games that load a named GL driver (e.g. +set gl_driver
  3dfxgl): point gl_driver at opengl32 and set [opengl32] Real= to the 3dfxgl
  file in retrometer.ini.

DirectDraw / Direct3D 7 and older (fpsdd.dll)
  1. Copy fpsdd.dll into the game folder.
  2. Drag the game's .exe onto rmpatch.exe.

  rmpatch changes one string in the exe so it loads FPSDD.DLL instead of
  DDRAW.DLL (fpsdd.dll then loads the real ddraw.dll itself). If the game ships
  its own ddraw.dll (a wrapper), leave it where it is: fpsdd.dll uses a
  ddraw.dll (or ddrawa.dll) in the game folder before the system one. A backup is saved
  as <game>.exe.rmbak. Drag the exe onto rmpatch.exe again to undo.

  Why the extra step: on Windows 9x, ddraw.dll is wired by name to ddraw16.dll
  and the display driver, so a renamed copy of it does not work, and a proxy
  called ddraw.dll can't reach the real one.


Settings (retrometer.ini)
---------------------
retrometer.exe edits these for you; by hand they look like this:

  [FPS]
  Hotkey=F11            F1-F12, Scroll Lock, Pause, Insert, Home, End, Page Up,
                        Page Down, Num 0-9, Num +, a letter, or a VK code
                        (0x91); add Ctrl+ / Alt+ / Shift+ for a combination
  StartVisible=1
  Corner=TopLeft        TopLeft, TopRight, BottomLeft, BottomRight
  X=8  Y=8              offset in pixels from that corner
  Scale=3               size of each font dot, 1-8
  Color=FFFF00          text colour, RRGGBB
  Background=1          box behind the text
  BackgroundColor=000000
  ShowLabel=0           1 = "60 FPS"
  ShowFrameTime=0       1 = add the average frame time, e.g. "16.7MS"
  UpdateMs=500          how often the number updates (100-5000)
  CapOn=0               frame cap on at start
  CapFps=60             cap value (10-500)
  CapHotkey=F12         key for the cap, or None
  CapHotkeyMode=Toggle  Toggle (on/off) or Cycle (steps through CapCycle)
  CapCycle=30,60,85     up to 6 values for Cycle
  Log=0                 1 writes retrometer.log (start here if something doesn't work)

  [opengl32] / [glide2x] / [glide3x] / [d3d8] / [ddraw]
  Real=<path>           chain to this DLL instead of the default

Changes are picked up by a running game within about a second. Windows 9x's
timer is only ~55 ms accurate, so the counter averages over UpdateMs.

If the real driver can't be found (e.g. glide2x.dll with no Voodoo card), the
proxy fails to load quietly and logs why, so games that probe for Glide just
fall back as they normally would.


How each one draws
------------------
  OpenGL     Before wglSwapBuffers: saves state (glPushAttrib when the driver
             has it, otherwise saves the common enables by hand, for MiniGLs),
             draws untextured quads, restores.
  Glide      Before grBufferSwap: locks the back buffer (grLfbLock, write-only,
             no pixel pipeline) and writes the pixels. 16-bit and 32-bit
             modes.
  Direct3D 8 In IDirect3DDevice8::Present: captures a state block, draws a few
             untextured pre-transformed triangles on the back buffer,
             re-applies the state block. Handles Reset.
  DirectDraw Fullscreen: on Flip, draws on the back buffer via the surface's GDI
             DC. Windowed (no Flip for 2 s): on a Blt to the primary surface,
             draws on the source surface. GDI drawing means it works in
             8/16/24/32-bit modes; in 8-bit modes the colour is the nearest
             palette entry.


Known limits
------------
  * DirectDraw games that create DirectDraw through COM (CoCreateInstance) or
    LoadLibrary rather than importing DDRAW.DLL won't be caught;
    rmpatch tells you when there's no import to patch.
  * Windowed DirectDraw games that Blt to the screen several times per frame
    will over-count.
  * The DirectDraw and Direct3D 8 hooks patch the driver's COM function tables
    in place. That is refused if the table lives in Windows 9x shared memory
    (above 2 GB), in which case there's simply no counter (see retrometer.log).
  * Direct3D 9 is not covered (it needs Windows 98 SE/ME with DirectX 9 and
    would be a d3d9.dll built the same way as the d3d8 one).
  * Tested under Wine (Glide via a fake driver, Direct3D 8 and DirectDraw
    with real rendering, OpenGL draw calls and state restore). Not yet
    tested on real Windows 98 hardware.


Building
--------
  On Linux with mingw-w64 (gcc + windres) and Python 3:   sh build.sh
  Output goes to out/. The export forwarders are generated from the lists in
  exports/ by gen/gen_stubs.py.
