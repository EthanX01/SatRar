# ABC used for CEC

Phase 0 of `docs/SatRAR_ABC_CEC_Plan.md` uses the Windows build of
[OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build/releases), which
ships `yosys-abc.exe`. The executable, the extracted suite, and the downloaded
archive are gitignored. `ABC_VERSION.txt` records the exact build.

Refresh:

```powershell
curl.exe -L --fail -o satrar-cpp\tools\abc\oss-cad-suite-windows-x64-YYYYMMDD.tgz `
  https://github.com/YosysHQ/oss-cad-suite-build/releases/download/YYYY-MM-DD/oss-cad-suite-windows-x64-YYYYMMDD.tgz
tar -xzf satrar-cpp\tools\abc\oss-cad-suite-windows-x64-YYYYMMDD.tgz -C satrar-cpp\tools\abc
```

`yosys-abc.exe` needs these DLLs beside it (they live in `oss-cad-suite\lib`):
`libgcc_s_seh-1.dll`, `libreadline8.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`,
`libtermcap-0.dll`. `scripts/abc_probe.ps1` also puts `bin` and `lib` on `PATH`.
Set `SATRAR_ABC_EXE` to override the path. The checked-in build is recorded in
`ABC_VERSION.txt`: OSS CAD Suite 2026-11-25, ABC 1.01 compiled Nov 25 2026.
