# Open Citadel for Windows

This ZIP contains the native Windows host and its redistributable runtime
libraries. It does not contain Epic Citadel's proprietary game files; supply
your own Epic Citadel 1.07 XAPK.

1. Extract the ZIP to a writable folder.
2. From that folder, import your donor (Python 3 is required):

   ```powershell
   python .\open_citadel_import.py "C:\path\to\Epic+Citadel_1.07_APKPure.xapk" `
       ".\gamedata\epic-citadel-1.07"
   ```

3. Start the game from the same folder:

   ```powershell
   .\open-citadel.exe
   ```

Click the ground to walk and drag with either mouse button to look around.
W/A/S/D movement is confirmed working. F3 captures the pointer for clickless
relative camera look; F3 or Escape releases it. F2/settings and focus loss also
release capture. F1 shows controls, and F2 opens the in-game settings overlay.
Its Game tab displays live FPS/frame time and
controls mouse, VSync, frame-cap/benchmark settings, and the game's 3D render
scale (50%, 75%, or 100%; 100% by default, applied next launch), plus native
relative mouse look (enabled by default) and **Capture mouse on launch** (off
by default). Set
`OPEN_CITADEL_RESOLUTION_SCALE` to `0.50`, `0.75`, or `1.00` to override it. Use
Controls to rebind movement keys; Escape cancels a rebind and **Reset movement
keys to WASD** restores defaults. Display accepts custom width/height and
fullscreen settings for the next launch. The window can also be resized live
(320x240 minimum); UE3 receives the new drawable size, mouse/touch coordinates
are rescaled, and the windowed
size is saved after resizing settles. Startup fullscreen was visually verified
at 3440x1440. F11/Alt+Enter request borderless fullscreen; the live hotkey
transition still needs visual validation. Shift+F2 opens the
legacy native settings dialog. Settings are saved under
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`.
Set `OPEN_CITADEL_NATIVE_MOUSE_LOOK=0` to disable F3 capture mode.
The Game tab's **Capture mouse on launch** option is saved as
`capture_mouse_on_launch=true`; Escape releases the pointer. The environment
variable `OPEN_CITADEL_NATIVE_MOUSE_LOOK_CAPTURE` overrides the saved option.

The game defaults to its 60 FPS cap. In the Game tab, enable **Disable the
game's 60 FPS cap next launch** and restart to uncap it, or set
`OPEN_CITADEL_UNCAP_FPS=1` before launching. VSync is a separate setting;
with VSync on, presentation can still be limited to your display's refresh
rate even though the game's 60 FPS cap is disabled. Uncapped mode with VSync
off can use substantially more CPU/GPU.
For a benchmark run with both the game limit and VSync disabled, choose
**Uncapped benchmark mode next launch** in the Game tab or set
`OPEN_CITADEL_UNCAPPED_BENCHMARK=1`. This preserves your saved VSync preference.

The host runs as a 32-bit Windows process and supports 64-bit Windows. Keep the
donor folder beside the executable, or set `OPEN_CITADEL_GAME_DIR` to its path.
No game data is uploaded, included in the ZIP, or redistributed by this project.
