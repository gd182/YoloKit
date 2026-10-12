package io.github.gd182.yolokit

import android.content.Context
import android.graphics.Bitmap
import android.graphics.RectF
import io.github.gd182.yolokit.internal.Detector
import io.github.gd182.yolokit.internal.NcnnDetector
import io.github.gd182.yolokit.internal.NcnnNative
import io.github.gd182.yolokit.internal.QnnDetector

/** One detected object. [rect] is in the pixel space of the bitmap passed to [YoloKit.detect]. */
data class Detection(
    val rect: RectF,
    val label: Int,
    val labelName: String,
    val score: Float,
)

/** Which inference engine runs the model. */
enum class Backend {
    /** ncnn with a Vulkan GPU backend (falls back to CPU if no GPU). Any Android device. */
    NCNN,

    /** Qualcomm Hexagon NPU through the QNN HTP backend. Snapdragon arm64 devices only. */
    QNN,
}

/**
 * Describes one model and how to run it.
 *
 * Build it with [Builder]:
 * ```
 * val config = ModelConfig.Builder()
 *     .ncnn(param = "yolov8n.param", bin = "yolov8n.bin")
 *     .labels(YoloKit.COCO_LABELS)
 *     .build()
 * ```
 */
class ModelConfig private constructor(
    val backend: Backend,
    val paramAsset: String?,
    val binAsset: String?,
    val binaryAsset: String?,
    val labels: List<String>,
    val inputSize: Int,
    val inputName: String,
    val outputName: String,
    val decoded: Boolean,
    val rgbOrder: Boolean,
    val boxesNormalized: Boolean,
    val confidenceThreshold: Float,
    val nmsThreshold: Float,
    val useGpu: Boolean,
) {

    class Builder {
        private var backend: Backend = Backend.NCNN
        private var param: String? = null
        private var bin: String? = null
        private var binary: String? = null
        private var labels: List<String> = emptyList()
        private var inputSize: Int = 640
        private var inputName: String = "images"
        private var outputName: String = "output"
        private var decoded: Boolean = false
        private var rgbOrder: Boolean = false
        private var boxesNormalized: Boolean = false
        private var confidenceThreshold: Float = 0.25f
        private var nmsThreshold: Float = 0.45f
        private var useGpu: Boolean = true

        /**
         * An ncnn model from assets. [decoded] = true for the Ultralytics native
         * `yolo export format=ncnn` graph (input `in0`, output `out0`); false for the
         * raw DFL single-output YOLOv8 graph (input `images`, output `output`).
         */
        fun ncnn(
            param: String,
            bin: String,
            decoded: Boolean = false,
            inputName: String = if (decoded) "in0" else "images",
            outputName: String = if (decoded) "out0" else "output",
        ) = apply {
            backend = Backend.NCNN
            this.param = param
            this.bin = bin
            this.decoded = decoded
            this.inputName = inputName
            this.outputName = outputName
            rgbOrder = decoded
        }

        /** A pre-compiled QNN HTP context binary from assets (see docs/QNN.md). */
        fun qnn(binaryAsset: String, boxesNormalized: Boolean = false) = apply {
            backend = Backend.QNN
            binary = binaryAsset
            this.boxesNormalized = boxesNormalized
            rgbOrder = true
        }

        /** Class names in training order. Must match the number of classes in the model. */
        fun labels(names: List<String>) = apply { labels = names }

        /** Network input side in pixels, as used at export time (`imgsz`). */
        fun inputSize(size: Int) = apply { inputSize = size }

        /** Feed pixels as RGB (true) or BGR (false). Set by [ncnn]/[qnn] for the common exports. */
        fun rgbOrder(enabled: Boolean) = apply { rgbOrder = enabled }

        fun thresholds(confidence: Float, nms: Float) = apply {
            confidenceThreshold = confidence
            nmsThreshold = nms
        }

        fun useGpu(enabled: Boolean) = apply { useGpu = enabled }

        fun build(): ModelConfig {
            require(labels.isNotEmpty()) { "labels are required" }
            require(inputSize > 0 && inputSize % 32 == 0) { "inputSize must be a positive multiple of 32" }
            when (backend) {
                Backend.NCNN -> require(param != null && bin != null) { "ncnn model needs param and bin" }
                Backend.QNN -> require(binary != null) { "qnn model needs a binary asset" }
            }
            return ModelConfig(
                backend = backend,
                paramAsset = param,
                binAsset = bin,
                binaryAsset = binary,
                labels = labels,
                inputSize = inputSize,
                inputName = inputName,
                outputName = outputName,
                decoded = decoded,
                rgbOrder = rgbOrder,
                boxesNormalized = boxesNormalized,
                confidenceThreshold = confidenceThreshold,
                nmsThreshold = nmsThreshold,
                useGpu = useGpu,
            )
        }
    }
}

/**
 * Runs a YOLO model on bitmaps. Create once, call [detect] per frame, [close] when done.
 *
 * Calls to [detect] are serialized internally; one instance can be shared across threads,
 * but a single frame should not be processed twice in parallel.
 */
class YoloKit private constructor(
    private val context: Context,
    private val config: ModelConfig,
    initialDetector: Detector,
) : AutoCloseable {
    private val lock = Any()
    private var detector: Detector? = initialDetector
    private var closed = false
    /** Human-readable engine, e.g. "GPU · Vulkan" or "NPU · Qualcomm HTP (QNN) · FP16". */
    val backendLabel: String get() = synchronized(lock) {
            detector?.backendLabel ?: "Stopped"
        }

    /**
     * Detects objects in an ARGB_8888 [bitmap]. Boxes are in the bitmap's pixel space.
     * @throws IllegalStateException if this instance is stopped or closed.
     */
    fun detect(bitmap: Bitmap): List<Detection> = synchronized(lock) {
        check(!closed) { "YoloKit is closed" }
        val active = checkNotNull(detector) { "Call start() first" }
        active.detect(bitmap)
    }

    /**
     * Unloads the detector while keeping its configuration for future reloading.
     * Waits for any detection currently holding this instance's lock to finish.
     * Repeated calls are safe; this does not reopen a closed instance.
     * Call [start] to reload the model using the saved configuration.
     *
     * With [releaseGpu] = true, an NCNN instance also destroys the process-wide Vulkan instance,
     * which frees driver memory that otherwise stays allocated. The next [start] recreates it, so
     * the reload is slower. The Vulkan instance is kept if any NCNN model is still loaded, because
     * native models are shared (see README). QNN has no process-wide state, so the flag has no effect.
     */
    fun stop(releaseGpu: Boolean = false): Unit = synchronized(lock) {
        detector?.close()
        detector = null
        if (releaseGpu && config.backend == Backend.NCNN) NcnnNative.nativeReleaseGpu()
    }

    /**
     * Reloads the model using the saved configuration after [stop].
     * Returns success without reloading if the detector is already running.
     * Returns failure if this instance is closed or the detector reports a loading error.
     * A failed load leaves this instance stopped so loading can be retried.
     * Loading is synchronous; call this off the UI thread.
     */
    fun start(): Result<Unit> = synchronized(lock) {
        if (closed) {
            return@synchronized Result.failure(
                IllegalStateException("YoloKit is closed")
            )
        }
        if (detector != null) {
            return@synchronized Result.success(Unit)
        }
        val result = when (config.backend) {
            Backend.NCNN -> NcnnDetector.create(context, config)
            Backend.QNN -> QnnDetector.create(context, config)
        }
        result.map {
            detector = it
            Unit
        }
    }

    /**
     * Unloads the detector and permanently closes this instance.
     * Waits for any detection currently holding this instance's lock to finish.
     * Repeated calls are safe.
     */
    override fun close(): Unit = synchronized(lock) {
        detector?.close()
        detector = null
        closed = true
    }

    companion object {
        /** Class names of the COCO-80 dataset, the training set of the standard YOLOv8 weights. */
        val COCO_LABELS: List<String> = listOf(
            "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
            "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog",
            "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
            "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
            "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
            "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
            "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant",
            "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
            "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors",
            "teddy bear", "hair drier", "toothbrush",
        )

        /** True when the device can run the QNN backend (arm64 Qualcomm SoC with the native libs). */
        fun isQnnAvailable(context: Context): Boolean = QnnDetector.isAvailable()

        /**
         * Loads the model. Returns failure with a readable message if the model files are missing,
         * the backend is unavailable on this device, or the model does not match the config.
         */
        fun create(context: Context, config: ModelConfig): Result<YoloKit> {
            val result = when (config.backend) {
                Backend.NCNN -> NcnnDetector.create(context, config)
                Backend.QNN -> QnnDetector.create(context, config)
            }
            return result.map { YoloKit(context.applicationContext, config, it) }
        }
    }
}
