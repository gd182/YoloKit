plugins {
    alias(libs.plugins.android.library)
}

val qnnSdkRoot: String? = providers.gradleProperty("qnn.sdk.dir").orNull
    ?: providers.environmentVariable("QNN_SDK_ROOT").orNull
    ?: providers.fileContents(rootProject.layout.projectDirectory.file("local.properties"))
        .asText.orNull
        ?.lineSequence()
        ?.map { it.trim() }
        ?.firstOrNull { it.startsWith("qnn.sdk.dir=") }
        ?.substringAfter("=")
        ?.trim()

android {
    namespace = "io.github.gd182.yolokit"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
                if (!qnnSdkRoot.isNullOrEmpty()) arguments += "-DQNN_SDK_ROOT=$qnnSdkRoot"
                cppFlags += "-std=c++17"
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }


    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += listOf("**/libQnnDsp*.so", "**/libQnnGpu.so", "**/libQnnHtpPrepare.so")
        }
    }
}

dependencies {
    implementation(libs.qnn.runtime)
}
