# Building the IGI 1 MVP

The host build validates portable scheduler and catalogue code:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Build the injection DLL with an x86 Windows toolchain:

```sh
cmake -S . -B build-win32 -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ -DCMAKE_BUILD_TYPE=Release
cmake --build build-win32
```

This produces `IGI-Natives-SDK-IGI1.dll`. After a loader has completed DLL
loading, call the exported `IGI_SDK_Initialize`; call `IGI_SDK_Shutdown` before
unloading. The hook only installs for the supported Windows x86 executable
when its primary signature matches.
