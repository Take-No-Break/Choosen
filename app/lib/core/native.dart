// Binding to the Choosen core (src/capi.h), loaded as choosen_core.dll (Windows) or
// libchoosen_core.so (Android). All functions are synchronous; Api runs the slow ones off the UI.
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

typedef _InitC = Pointer<Utf8> Function(Pointer<Utf8>);
typedef _ApiC = Pointer<Utf8> Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>);
typedef _PathC = Pointer<Utf8> Function(Int32, Pointer<Utf8>);
typedef _PathD = Pointer<Utf8> Function(int, Pointer<Utf8>);
typedef _VersionC = Uint64 Function();
typedef _VersionD = int Function();
typedef _VoidC = Void Function();
typedef _VoidD = void Function();
typedef _FreeC = Void Function(Pointer<Utf8>);
typedef _FreeD = void Function(Pointer<Utf8>);

class NativeResult {
  const NativeResult(this.status, this.body);
  final int status;
  final String body;
}

class Native {
  Native._();

  static final DynamicLibrary _lib = Platform.isWindows
      ? DynamicLibrary.open('choosen_core.dll')
      : DynamicLibrary.open('libchoosen_core.so');
  static final _init = _lib.lookupFunction<_InitC, _InitC>('choosen_init');
  static final _api = _lib.lookupFunction<_ApiC, _ApiC>('choosen_api');
  static final _itemPath = _lib.lookupFunction<_PathC, _PathD>('choosen_item_path');
  static final _version = _lib.lookupFunction<_VersionC, _VersionD>('choosen_version');
  static final _saveIndex = _lib.lookupFunction<_VoidC, _VoidD>('choosen_save_index');
  static final _dailyBackup = _lib.lookupFunction<_VoidC, _VoidD>('choosen_daily_backup');
  static final _free = _lib.lookupFunction<_FreeC, _FreeD>('choosen_free');

  static String _take(Pointer<Utf8> p) {
    final s = p.toDartString();
    _free(p);
    return s;
  }

  static T _with<T>(List<String> args, T Function(List<Pointer<Utf8>>) f) {
    final ptrs = [for (final a in args) a.toNativeUtf8()];
    try {
      return f(ptrs);
    } finally {
      for (final p in ptrs) {
        malloc.free(p);
      }
    }
  }

  /// Loads the data in [dataDir] and starts scanning the photo folders (once per process).
  /// Returns "" when the library is open, else the app that has [dataDir] open (Version 1 or
  /// another Version 2); then nothing is loaded and init can be tried again later.
  static String init(String dataDir) => _with([dataDir], (p) => _take(_init(p[0])));

  static NativeResult api(String method, String path, String query, String body) {
    final res = _with([method, path, query, body], (p) => _take(_api(p[0], p[1], p[2], p[3])));
    final nl = res.indexOf('\n');
    return NativeResult(int.parse(res.substring(0, nl)), res.substring(nl + 1));
  }

  static String itemPath(int id, String fp) => _with([fp], (p) => _take(_itemPath(id, p[0])));
  static int version() => _version();
  static void saveIndex() => _saveIndex();
  static void dailyBackup() => _dailyBackup();
}
