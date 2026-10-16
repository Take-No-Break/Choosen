// Choosen for Android: the pages as assets (../../web, and Choosen V3 from ../../web3/dist under v3/),
// the C++ core from ../../src via the NDK. Build V3 first: "npm run build" in web3 (build.bat does it).
plugins {
  id("com.android.application")
}

android {
  namespace = "com.choosen.app"
  compileSdk = 37
  ndkVersion = "30.0.16248370"

  defaultConfig {
    applicationId = "com.choosen.app"
    minSdk = 29
    targetSdk = 36
    versionCode = 11
    versionName = "0.12.0"
    ndk { abiFilters += listOf("arm64-v8a") }
  }

  buildTypes {
    getByName("debug") { manifestPlaceholders["appLabel"] = "Choosen" }
    getByName("release") { manifestPlaceholders["appLabel"] = "Choosen" }
    // A separate app (own data, own name) for checks on the device without touching the owner's app.
    create("qa") {
      initWith(getByName("debug"))
      applicationIdSuffix = ".qa"
      manifestPlaceholders["appLabel"] = "Choosen QA"
    }
  }

  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }

  sourceSets["main"].assets.srcDirs("../../web", layout.buildDirectory.dir("generated/v3assets").get().asFile)

  externalNativeBuild {
    cmake {
      path = file("src/main/cpp/CMakeLists.txt")
      version = "4.1.2"
    }
  }
}

// Choosen V3's three files (web3/dist) as assets/v3/.
val copyV3 by tasks.registering(Copy::class) {
  val dist = file("../../web3/dist")
  doFirst {
    if (!File(dist, "app.js").exists()) throw GradleException("web3/dist is missing: run \"npm run build\" in web3 first")
  }
  from(dist) { include("index.html", "app.js", "app.css") }
  into(layout.buildDirectory.dir("generated/v3assets/v3"))
}
tasks.named("preBuild") { dependsOn(copyV3) }
