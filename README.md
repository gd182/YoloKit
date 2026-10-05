# YoloKit

Demo app that uses this library: [YoloKitMobile](../YoloKitMobile).

On-device YOLO object detection for Android. Two engines behind one API:

- **NCNN** (Vulkan GPU, CPU fallback) on any Android 7+ device.
- **QNN** (Hexagon NPU through the QNN HTP backend) on arm64 Qualcomm devices.

## Install (git)

Add the repository as a git submodule and include the module through a composite build:

```bash
git submodule add https://github.com/gd182/YoloKitMobile.git vendor/YoloKitMobile
```

```kotlin
// settings.gradle.kts of your app
includeBuild("vendor/YoloKitMobile") {
    dependencySubstitution {
        substitute(module("io.github.gd182:yolokit")).using(project(":yolokit"))
    }
}
```

```kotlin
// app/build.gradle.kts
dependencies {
    implementation("io.github.gd182:yolokit")
}
```

Or clone it anywhere and point `includeBuild` at that folder. Requirements: Android Studio with
AGP 9.4+, NDK and CMake 3.22.1. For QNN, set `qnn.sdk.dir` in `local.properties`.

## Use

```kotlin
val config = ModelConfig.Builder()
    .ncnn(param = "yolov8n.param", bin = "yolov8n.bin")   // assets/
    .labels(YoloKit.COCO_LABELS)
    .inputSize(640)
    .thresholds(confidence = 0.25f, nms = 0.45f)
    .build()

val kit = YoloKit.create(context, config).getOrThrow()
val detections = kit.detect(bitmap)   // ARGB_8888 bitmap
detections.forEach { d -> Log.d("yolo", "${d.labelName} ${d.score} ${d.rect}") }
kit.close()
```

Ultralytics native export (`yolo export format=ncnn`) uses `decoded = true`:

```kotlin
ModelConfig.Builder().ncnn("model.ncnn.param", "model.ncnn.bin", decoded = true)
```

QNN (Snapdragon NPU), pre-built context binary from `scripts/build_qnn_model.sh`:

```kotlin
if (YoloKit.isQnnAvailable(context)) {
    ModelConfig.Builder().qnn("model_v81.bin").labels(myClasses).build()
}
```

## Model requirements

- `targetSize` (`inputSize`) must equal the `imgsz` used at export.
- `labels` must list the classes in training order, one per model output class.
- ncnn: DFL single-output YOLOv8 (`decoded = false`, default) or Ultralytics native (`decoded = true`).
- QNN: detector output `[1, 4+nc, N]` decoded, no NMS in the graph.

## Notes and limits

- `detect` calls are serialized internally. One `YoloKit` per process is supported for now:
  the native layer keeps one model per backend.
- The QNN runtime libraries come with the artifact as a dependency; their license allows
  use only inside your application, not redistribution on their own.
- Model weights are not part of the library. Ultralytics-derived weights are AGPL-3.0.

## License

Apache-2.0. Bundled third-party components: ncnn (BSD-3-Clause), QNN runtime (Qualcomm AI Stack license).
