# Flutter client

A separate Dart/Flutter client that accesses Choosen's shared C++ library through FFI. It is not used to build the Android WebView app or the Windows executable offered in Releases.

- [lib](lib/): Dart application code.
- [pubspec.yaml](pubspec.yaml): Flutter package and dependency configuration.
- [native library](../native/): CMake target used by the FFI client.

See the [architecture and build guide](../docs/ARCHITECTURE.md) for the distributed applications.
