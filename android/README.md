# Android application

Packages Choosen as an Android app using WebView, the photo interface and the shared C++ engine. Java handles Android permissions, image access, screen behavior and communication between the interface and native code.

## Entry points

- [MainActivity.java](app/src/main/java/com/choosen/app/MainActivity.java): app lifecycle and WebView integration.
- [Core.java](app/src/main/java/com/choosen/app/Core.java): native method declarations.
- [jni_bridge.cpp](app/src/main/cpp/jni_bridge.cpp): connection to the C++ engine.
- [app/build.gradle.kts](app/build.gradle.kts): app, native toolchain and asset configuration.

The app targets Android 10 or later on ARM64. The build requires JDK17, Android SDK/NDK and Node.js; toolchain versions are specified in Gradle. Build the [Svelte interface](../web3/) before running gradlew.bat assembleDebug.

See the [architecture and build guide](../docs/ARCHITECTURE.md) for data storage and platform responsibilities.
