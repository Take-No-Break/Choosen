package com.choosen.app;

/** The shared C++ core (src/core*.cpp), built by the NDK into libchoosen_core.so. */
final class Core {
  static {
    System.loadLibrary("choosen_core");
  }

  private Core() {}

  /** Loads settings, index and ratings from dataDir and starts scanning the photo folders. */
  static native void init(String dataDir);

  /** One API call of the page; returns "status\njson". */
  static native String api(String method, String path, String query, String body);

  /** Absolute path of an image, or "" if the id/fingerprint is unknown. */
  static native String itemPath(int id, String fp);

  /** Changes whenever anything the page shows changed. */
  static native long version();

  static native void saveIndex();

  static native void dailyBackup();

  static native void rescan();
}
