plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// CI sets PC_VERSION_CODE to github.run_number so every build is newer than the last one.
val pcVersionCode: Int = System.getenv("PC_VERSION_CODE")?.trim()?.toIntOrNull() ?: 1

android {
    namespace = "com.pocketcraft.game"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    // One fixed key for every build (debug and release), so each new APK installs as an update over an old one.
    // The keystore is committed on purpose: it is a test key, not a store upload key.
    signingConfigs {
        create("pocketcraft") {
            storeFile = file("pocketcraft-debug.keystore")
            storePassword = "pocketcraft"
            keyAlias = "pocketcraft"
            keyPassword = "pocketcraft"
        }
    }

    defaultConfig {
        applicationId = "com.pocketcraft.game"
        minSdk = 26
        targetSdk = 35
        versionCode = pcVersionCode
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
            signingConfig = signingConfigs.getByName("pocketcraft")
        }
        release {
            isMinifyEnabled = false
            isDebuggable = false
            signingConfig = signingConfigs.getByName("pocketcraft")
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
