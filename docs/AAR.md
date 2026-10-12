# Obfuscated AAR and QNN integration

## Build

Set `qnn.sdk.dir` in this project's `local.properties` to the QAIRT 2.50.0
SDK directory, or pass `-Pqnn.sdk.dir=/path/to/QAIRT/2.50.0` to Gradle.
Without the SDK headers the native library only supports ncnn.

```bash
./gradlew :yolokit:assembleRelease
```

Output: `yolokit/build/outputs/aar/yolokit-release.aar`.
Release builds use R8 to obfuscate internal JVM implementation classes.
Public API names and the two JNI bridge classes remain stable. R8 does not
obfuscate C++ code, model files, or Qualcomm's native libraries.
Keep `yolokit/build/outputs/mapping/release/mapping.txt` with each distributed
release for decoding stack traces; it is not needed by consuming apps.

## Add to an Android app

Place the AAR in `app/libs/yolokit-release.aar`. The app's Gradle repositories
must include `google()` and `mavenCentral()`.
In the app module's `build.gradle.kts`:

```kotlin
dependencies {
    implementation(files("libs/yolokit-release.aar"))
    implementation("com.qualcomm.qti:qnn-runtime:2.50.0")
}

android {
    packaging {
        jniLibs {
            useLegacyPackaging = true
            excludes += listOf(
                "**/libQnnDsp*.so",
                "**/libQnnGpu.so",
                "**/libQnnHtpPrepare.so",
            )
        }
    }
}
```

A local AAR has no Maven dependency metadata: QNN runtime must be declared
explicitly. For an ncnn-only app, omit that dependency. Kotlin apps normally
already depend on the Kotlin standard library; Java-only apps also need a
compatible Kotlin stdlib dependency.

`useLegacyPackaging = true` makes native libraries available as extracted
files in `applicationInfo.nativeLibraryDir`, which YoloKit includes in
`ADSP_LIBRARY_PATH` for QNN's DSP loader. Keep the JNI consumer rules included
in the AAR when enabling R8 in the app; no package-wide keep rule is needed.

## Several Hexagon architectures

The cached QNN 2.50.0 runtime AAR contains HTP Stub/Skel pairs for V68, V69,
V73, V75, V79 and V81. It contains no V61 pair. Do not assume V61 support;
check the actual SoC against Qualcomm's supported-device documentation.

One YoloKit AAR can be used for these supported targets. Package the runtime
libraries for the targets you support and compatible context binaries in the
app's `src/main/assets/`, for example:

```text
models/device_v73.bin
models/device_v81.bin
```

The runtime selects its device backend. The app must select the model asset
prepared for the actual target SoC and runtime SDK version. YoloKit does not
detect the architecture or select a model automatically.

```kotlin
// Set this using the app's validated mapping of supported devices to models.
val modelAsset = "models/device_v81.bin"
val config = ModelConfig.Builder()
    .qnn(modelAsset)
    .labels(myClasses)
    .inputSize(640)
    .build()

// Load and run inference off the UI thread. Handle failure on unsupported devices.
val kit = YoloKit.create(context, config).getOrThrow()
try {
    val detections = kit.detect(bitmap)
} finally {
    kit.close()
}
```

`YoloKit.isQnnAvailable(context)` checks whether the native QNN code was
compiled in. It does not validate a particular device, installed DSP runtime,
or model; model initialization can still fail.

For a V81-only app, optionally exclude the other HTP Stub/Skel pairs from
the APK in the same `jniLibs` block:

```kotlin
excludes += listOf(
    "**/libQnnHtpV68*.so",
    "**/libQnnHtpV69*.so",
    "**/libQnnHtpV73*.so",
    "**/libQnnHtpV75*.so",
    "**/libQnnHtpV79*.so",
)
```

Retain `libQnnHtp.so`, `libQnnSystem.so`, and the V81 Stub/Skel pair.
Do not apply these exclusions to an app intended to run QNN on older targets.

References: [Android library optimization](https://developer.android.com/topic/performance/app-optimization/library-optimization),
[Qualcomm supported devices](https://docs.qualcomm.com/nav/home/QNN_general_overview.html?product=924033590759186372).
