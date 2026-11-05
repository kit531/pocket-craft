plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// CI sets PC_VERSION_CODE to github.run_number. The package is the existing "Pocket Client" launcher's
// (com.movtery.zalithlauncher) so this APK installs as an update over it, which needs a higher versionCode:
// the launcher's own codes are 300000 + its run number, so ours is offset by 1000000.
val pcVersionCode: Int = System.getenv("PC_VERSION_CODE")?.trim()?.toIntOrNull() ?: 1

android {
    namespace = "com.pocketcraft.game"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    // Same key as the Pocket Client launcher, which Android requires for an update to install over it.
    // It is the public debug key from ZalithLauncher at a pinned commit; CI copies it in before the build
    // (see .github/workflows/build-apk.yml) and it is never committed here (*.keystore is git-ignored).
    signingConfigs {
        create("zalith") {
            storeFile = file("zalith-debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    defaultConfig {
        applicationId = "com.movtery.zalithlauncher"
        minSdk = 26
        targetSdk = 35
        versionCode = 1000000 + pcVersionCode
        versionName = "0.1.$pcVersionCode"
        ndk {
            abiFilters += "arm64-v8a"
            // No separate symbol files in the release APK (CI builds release only).
            debugSymbolLevel = "NONE"
        }
        externalNativeBuild {
            cmake {
                // libc++ statically linked: no libc++_shared.so to ship or load.
                // CMAKE_BUILD_TYPE=Release: CI only produces release builds.
                arguments += listOf("-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../engine/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("zalith")
        }
        release {
            isMinifyEnabled = false
            isDebuggable = false
            signingConfig = signingConfigs.getByName("zalith")
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
