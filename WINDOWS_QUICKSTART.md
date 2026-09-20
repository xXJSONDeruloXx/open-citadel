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
W/A/S/D movement is confirmed working. F1 shows controls, and F2 opens desktop
settings. Choose
**Controls...** there to rebind movement keys; Escape cancels a rebind and
**Reset to WASD** restores defaults. Escape outside key capture sends Back.
Settings are saved under `%APPDATA%\OpenCitadel\EpicCitadel\settings.ini`.

The game defaults to its 60 FPS cap. In F2, choose **Toggle next-launch FPS
cap** and restart to uncap it, or set `OPEN_CITADEL_UNCAP_FPS=1` before
launching. VSync is a separate setting; uncapped mode with VSync off can use
substantially more CPU/GPU.

The host runs as a 32-bit Windows process and supports 64-bit Windows. Keep the
donor folder beside the executable, or set `OPEN_CITADEL_GAME_DIR` to its path.
No game data is uploaded, included in the ZIP, or redistributed by this project.
