<h1 align="center">YoloKit</h1>

<p align="center">YOLO object detection for Android.</p>

<div align="center">
  <img src="https://skillicons.dev/icons?i=kotlin" height="40" alt="Kotlin logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=cpp" height="40" alt="C++ logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=cmake" height="40" alt="CMake logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=gradle" height="40" alt="Gradle logo" />
  <img width="12" />
  <img src="https://skillicons.dev/icons?i=androidstudio" height="40" alt="Android Studio logo" />
</div>

<p align="center"><b>English</b> · <a href="README.ru.md">Русский</a></p>

YoloKit runs YOLO object detection on Android. You pass it a bitmap and get back
bounding boxes, class names, and confidence scores. The Kotlin API is the same
whether inference runs through ncnn on Vulkan or through Qualcomm QNN on the
Hexagon NPU.

The [YoloKitMobile camera app](https://github.com/gd182/YoloKitMobile) shows how
to feed frames into the detector, draw results, switch models, and stop detection.
Library releases will include an AAR without model weights. See
[Releases](https://github.com/gd182/YoloKit/releases) for published versions.

## Requirements

The library supports Android 7.0 and newer (`minSdk 24`). ncnn builds are
included for `arm64-v8a`, `armeabi-v7a`, and `x86_64`. Vulkan is used when
available; `.useGpu(false)` selects CPU inference. QNN requires an arm64
Qualcomm device and a library built with the QAIRT SDK.

For source builds, use Android Studio with support for AGP 9.4.0, JDK 17 or
newer, Android SDK 37, the Android NDK, and CMake 3.22.1. Use the supplied
Gradle wrapper (9.6.0). The native code uses C++17, ncnn 20260526, and QNN
runtime 2.50.0. Build versions are in
[the version catalog](gradle/libs.versions.toml) and
[the library build file](yolokit/build.gradle.kts).

## Add the library

### From source

Add YoloKit to your app repository as a submodule, then include it as a Gradle
composite build:

```bash
git submodule add https://github.com/gd182/YoloKit.git vendor/YoloKit
```

`settings.gradle.kts`:

```kotlin
includeBuild("vendor/YoloKit") {
    dependencySubstitution {
        substitute(module("io.github.gd182:yolokit")).using(project(":yolokit"))
    }
}
```

`app/build.gradle.kts`:

```kotlin
dependencies {
    implementation("io.github.gd182:yolokit")
}
```

Keep `google()` and `mavenCentral()` in the app's dependency repositories.
For an existing clone, initialize the submodule with
`git submodule update --init --recursive`. The app records a specific library
commit; pulling the library separately does not update that recorded revision.

### From an AAR

Use the AAR attached to a [library release](https://github.com/gd182/YoloKit/releases),
or build it locally. From the YoloKit repository root:

```bash
./gradlew :yolokit:assembleRelease
```

The result is `yolokit/build/outputs/aar/yolokit-release.aar`. Put it in
`app/libs/` and add:

```kotlin
dependencies {
    implementation(files("libs/yolokit-release.aar"))
}
```

A local AAR does not carry Maven dependency metadata. For QNN, also declare
`implementation("com.qualcomm.qti:qnn-runtime:2.50.0")`; the SDK used to
build the AAR must match this version. An ncnn-only app can omit that dependency.
When using the composite build, the runtime dependency is brought in by YoloKit.

Release builds use R8 for internal JVM classes while keeping the public API
and JNI bridge names. This does not obfuscate native code or model weights.
Keep the generated `yolokit/build/outputs/mapping/release/mapping.txt` for
interpreting release stack traces.

See [AAR integration](docs/AAR.md) for native-library packaging, consumer
rules, and selecting QNN models for different devices.

## Run detection

Put an object-detection model in your app's `src/main/assets/` directory.
This example uses an Ultralytics ncnn export with two classes. Replace the
labels and input size with the ones used to train and export your model.

```kotlin
import io.github.gd182.yolokit.ModelConfig
import io.github.gd182.yolokit.YoloKit

val config = ModelConfig.Builder()
    .ncnn("model.ncnn.param", "model.ncnn.bin", decoded = true)
    .labels(listOf("person", "car"))
    .inputSize(640)
    .thresholds(confidence = 0.25f, nms = 0.45f)
    .build()

val kit = YoloKit.create(context, config).getOrThrow()
try {
    val detections = kit.detect(bitmap)
    detections.forEach { detection ->
        val bounds = detection.rect
        val name = detection.labelName
        val confidence = detection.score
    }
} finally {
    kit.close()
}
```

Run model loading and detection off the UI thread. The bitmap must be
`ARGB_8888`. `Detection.rect` is a `RectF` in the input bitmap's pixel
coordinates; `label` is the class index, `labelName` the class name, and
`score` the confidence. Map the rectangle to your preview's scale and crop
before drawing it.

For a camera stream, create the detector once, reuse it for each frame, and
close it when the stream ends. `create()` returns `Result<YoloKit>`; handle
loading failures in your app instead of relying on `getOrThrow()` as in the
short example. `backendLabel` reports the active backend and detected precision.

### Stop and resume

```kotlin
kit.stop()
kit.start().getOrThrow()

kit.stop(releaseGpu = true)
kit.start().getOrThrow()

kit.close()
```

`stop()` unloads the model and keeps the configuration. `start()` reloads it
and returns `Result<Unit>`; check the result before calling `detect()` again.
Starting an already running instance succeeds without another load. A reported
load failure leaves it stopped so you can retry.

Both stopping and closing wait for an in-flight detection; they do not cancel
inference. After either operation, `detect()` throws `IllegalStateException`.
`close()` is permanent: restarting a closed instance returns failure.

For ncnn, `releaseGpu = true` also releases the process-wide Vulkan instance
once no ncnn model is loaded. The next start recreates it, so startup takes
longer. QNN ignores this flag. Stopping leaves model files intact.

The native layer shares one loaded model per backend. Keep one active instance
per backend and close it before replacing the model. Calls on an instance are
serialized, but several instances do not provide independent native models.

### Load from memory

The builder also accepts `ByteArray` sources for ncnn and QNN. This is useful
when your app downloads or decrypts a model before loading it:

```kotlin
val config = ModelConfig.Builder()
    .ncnn(param = paramBytes, bin = weightBytes, decoded = true)
    .labels(myClasses)
    .inputSize(640)
    .build()

val qnnConfig = ModelConfig.Builder()
    .qnn(binary = contextBytes)
    .labels(myClasses)
    .build()
```

The arrays are retained by the configuration, not copied when the builder is
called. Keep them unchanged for the lifetime of the instance: they are read
again on every load, including `start()`. JNI copies the data during loading,
so allow for additional memory use. `stop()` unloads the native model but keeps
the byte arrays with the configuration.

YoloKit does not implement downloading, encryption, or key management. Your app
supplies the bytes; encryption alone does not prevent extracting a running model.

## Model requirements

This is an object detector: classification, segmentation, pose, and OBB outputs
are not decoded by this API. Compatibility depends on the exported tensor
layout, not just the YOLO model name.

For ncnn, `inputSize` must match the export's `imgsz` and be a positive
multiple of 32. Labels must match the model's class count and training order.

| ncnn setting | Raw single-output YOLOv8 DFL | Ultralytics decoded export |
|---|---|---|
| `decoded` | `false` (default) | `true` |
| Input / output | `images` / `output` | `in0` / `out0` |
| Color order | BGR | RGB |
| Padding | To a multiple of 32 | To the full input square |

`.ncnn(..., decoded = true)` sets the usual tensor names and RGB order.
Override names in `ncnn()` and colors with `.rgbOrder(...)` if your export
differs. Use `YoloKit.COCO_LABELS` only for models trained on COCO's 80 classes.

QNN reads its input size, layout, dtype, and quantization from the context binary.
It accepts decoded `xywh` boxes and class scores, without NMS in the graph:
one tensor `[1, 4+nc, N]` / `[1, N, 4+nc]`, or separate box and score tensors
with matching anchor counts. Use `boxesNormalized = true` only for box
coordinates in the 0–1 range. NMS runs in YoloKit.

The [sample's assets guide](https://github.com/gd182/YoloKitMobile/blob/main/app/src/main/assets/README.md)
has export commands and model-configuration examples.

## QNN setup

Download the [QAIRT SDK](https://www.qualcomm.com/developer/software/qualcomm-ai-engine-direct-sdk)
and point the library's `local.properties` at its root:

```properties
qnn.sdk.dir=/path/to/QAIRT/2.50.0
```

The root must contain `include/QNN/QnnInterface.h`. You can also set
`QNN_SDK_ROOT` or pass `-Pqnn.sdk.dir=/path/to/QAIRT/2.50.0` to Gradle.
Without the headers, QNN is not compiled in. To build explicitly without QNN:

```bash
./gradlew -Pqnn.sdk.dir= :yolokit:assembleRelease
```

Keep the SDK headers, model-conversion tools, and Maven runtime at the same
version. A context binary must also match the target device's HTP architecture.
YoloKit does not automatically choose a model for each phone.

```kotlin
if (YoloKit.isQnnAvailable(context)) {
    val config = ModelConfig.Builder()
        .qnn("models/model_v81.bin", boxesNormalized = false)
        .labels(myClasses)
        .build()

    val result = YoloKit.create(context, config)
    // Handle result before starting the frame loop.
}
```

`isQnnAvailable()` checks arm64 support, Qualcomm device hints, and whether
QNN was compiled in. It does not validate the context binary or guarantee that
the DSP runtime can load it. Always handle initialization failure.

In the consuming app, use the native-library packaging settings:

```kotlin
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

Extracted libraries are needed for QNN's DSP loader. The composite build
includes the QNN runtime dependency even when QNN is disabled at compile time;
the SDK setting controls the native backend, not the dependency declaration.

Conversion instructions and scripts are in the sample repository:
[QNN guide](https://github.com/gd182/YoloKitMobile/blob/main/docs/QNN.md),
[macOS/device script](https://github.com/gd182/YoloKitMobile/blob/main/scripts/build_qnn_model.sh),
[Ubuntu x86_64 script](https://github.com/gd182/YoloKitMobile/blob/main/scripts/build_qnn_model_ubuntu_x86.sh).

## Troubleshooting

| Symptom | Check |
|---|---|
| Model does not load | Asset names, tensor names, class count, and matching SDK/runtime |
| Boxes have the wrong size or position | Export input size, color order, and preview coordinate mapping |
| Detection after stopping fails | Call `start()` and handle its result |
| QNN is unavailable | arm64 Qualcomm device, SDK headers at build time, packaged runtime |
| QNN initialization fails | Context binary for the device and runtime; see the conversion guide |
| App minification breaks JNI | Keep the consumer rules bundled in the AAR |

## Source and references

The public API is in [YoloKit.kt](yolokit/src/main/java/io/github/gd182/yolokit/YoloKit.kt).
The [native code](yolokit/src/main/cpp/) handles preprocessing, inference,
decoding, and NMS without OpenCV.

<p align="center"><img src="docs/stack.svg" alt="YoloKit architecture" width="720"/></p>

Upstream documentation: [ncnn](https://github.com/Tencent/ncnn),
[Ultralytics ncnn export](https://docs.ultralytics.com/integrations/ncnn/),
[Android library optimization](https://developer.android.com/topic/performance/app-optimization/library-optimization).

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for the project and
third-party components. Model weights are not included and have their own licenses.
