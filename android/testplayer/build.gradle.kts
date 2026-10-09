// Development-only helper: a plain third-party app that plays music, used to verify that
// SpatialEQ captures, processes and silences other apps. Not part of the release.
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.savannahdsp.testplayer"
    compileSdk = 35
    defaultConfig {
        applicationId = "com.savannahdsp.testplayer"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
