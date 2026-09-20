plugins {
    id("com.android.application")
}

android {
    namespace = "org.opencitadel.vr"
    compileSdk = 34

    defaultConfig {
        applicationId = "org.opencitadel.vr"
        minSdk = 32
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
            }
        }
    }

    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.63")
}
