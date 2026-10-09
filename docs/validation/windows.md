# Windows build and renderer validation

## Build and tests

Open **Developer PowerShell for Visual Studio** with the x64 C++ workload installed, then run from the repository root:

```powershell
./scripts/verify-windows.ps1 -Configuration Debug
```

The script configures the `x64-debug` CMake preset, builds all targets, and runs the registered CTest suite. Use `Release` to select the corresponding preset. Debug builds enable the D3D11 debug device and the NVRHI validation layer when available.

## GPU correctness scenarios

Run the Debug editor under the validation layer and check these cases after changing rendering or lifetime code:

1. Submit at least four simultaneous viewports. Use distinct scenes and light arrangements in each, then confirm each viewport retains its own lighting while all views render in one frame.
2. Place a masked foliage or fence asset between a spot light and a receiver. Confirm the shadow has the same alpha cutouts as the forward image. Repeat with a double-sided masked material and verify both faces cast.
3. Add a blended material with `CastShadows` enabled. It should remain absent from the shadow map; the current depth-only path does not approximate partial coverage.
4. Reimport or release textures and meshes while several frames are in flight, resize viewports, and close the editor. Check the D3D11/NVRHI debug output for stale-resource, state, or shutdown errors. Ordinary successful frame retirement should not call `waitForIdle()`.
5. Force a graph allocation or pass setup failure in a debugger. Every submitted viewport should show the defined magenta error clear instead of partially rendered contents.

The automated CTest suite does not create a graphics device, so these GPU scenarios remain manual validation steps.
