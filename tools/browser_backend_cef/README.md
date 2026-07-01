# CEF Bridge Build

This directory builds the Windows bridge that the MinGW game executable loads at runtime.

Outputs:
- `lib/cef/win64/bridge/browser_backend_cef.dll`
- `lib/cef/win64/bridge/browser_subprocess.dll`

Build with Visual Studio 2022 x64:

```powershell
cmake -S tools/browser_backend_cef -B build/browser_backend_cef -G "Visual Studio 17 2022" -A x64
cmake --build build/browser_backend_cef --config Release
```

After that, the normal game build will copy:
- `lib/cef/win64/Release/*`
- `lib/cef/win64/Resources/*`
- `lib/cef/win64/bridge/browser_backend_cef.dll`
- `lib/cef/win64/bridge/browser_subprocess.dll`

into `build/us_pc` when `ENABLE_BROWSER=1`.
