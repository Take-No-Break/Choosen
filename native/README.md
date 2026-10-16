# Shared library for the Flutter client

Builds the shared C++ core as a dynamic library for the Flutter client. CMake selects the Windows or POSIX platform adapter and exports the C interface implemented in src/capi.cpp.

- [CMakeLists.txt](CMakeLists.txt): library sources and platform configuration.
- [C interface](../src/capi.h): exported API.
- [Flutter client](../app/): consumer of this library.
