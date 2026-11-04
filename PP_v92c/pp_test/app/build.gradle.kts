plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.premiumpath"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.premiumpath.test"
        minSdk = 26
        targetSdk = 33
        versionCode = 8501
        versionName = "v85-safety-test"
        ndk { abiFilters += "arm64-v8a" }
    }
    externalNativeBuild { cmake { path = file("../native/CMakeLists.txt"); version = "3.22.1" } }
    packaging { jniLibs { useLegacyPackaging = true } }   // the .so must exist as a real file in nativeLibraryDir
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
