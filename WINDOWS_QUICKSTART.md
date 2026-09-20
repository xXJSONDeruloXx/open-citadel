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
W/A/S/D movement is confirmed working. F1 shows controls, and F2 opens the
in-game settings overlay. Its Game tab displays live FPS/frame time and
controls mouse, VSync, and frame-cap settings. Use Controls to rebind movement
keys; Escape cancels a rebind and **Reset movement keys to WASD** restores
defaults. Display accepts custom width/height and fullscreen settings for the
next launch. The window can also be resized live (320x240 minimum); UE3 receives
the new drawable size, mouse/touch coordinates are rescaled, and the windowed
size is saved after resizing settles. F11/Alt+Enter request borderless
fullscreen; that mode switch still needs visual validation. Shift+F2 opens the
legacy native settings dialog. Settings are saved under
`%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`.

The game defaults to its 60 FPS cap. In the Game tab, enable **Disable the
game's 60 FPS cap next launch** and restart to uncap it, or set
`OPEN_CITADEL_UNCAP_FPS=1` before launching. VSync is a separate setting;
uncapped mode with VSync off can use
substantially more CPU/GPU.
For a benchmark run with both the game limit and VSync disabled, choose
**Uncapped benchmark mode next launch** in the Game tab or set
`OPEN_CITADEL_UNCAPPED_BENCHMARK=1`. This preserves your saved VSync preference.

The host runs as a 32-bit Windows process and supports 64-bit Windows. Keep the
donor folder beside the executable, or set `OPEN_CITADEL_GAME_DIR` to its path.
No game data is uploaded, included in the ZIP, or redistributed by this project.
