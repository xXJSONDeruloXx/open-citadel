# Open Citadel web preview

Modern browser experiment for Epic Citadel.

The repository does not copy the Citadel dataset. The preview currently loads the public glTF export from `Phyronnaz/DAG_Compression` at runtime. This lets us validate camera/navigation/rendering independently of the historical 2013 Emscripten build.

## Controls

- Click: capture mouse
- Mouse: look
- WASD: horizontal movement
- Space / Ctrl or C: up/down
- Shift: sprint
- Mouse wheel: movement speed

## Direction

This is a bootstrap renderer, not a claim of gameplay parity. Next steps are material/camera correction, collision, mobile controls, then a client-side donor importer so a user can supply the original Epic Citadel APK/XAPK rather than relying on third-party extracted content.
