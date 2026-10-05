package io.github.gd182.yolokit.internal

import android.content.Context
import android.graphics.Bitmap
import android.graphics.RectF
import android.os.Build
import io.github.gd182.yolokit.Detection
import io.github.gd182.yolokit.ModelConfig

internal interface Detector : AutoCloseable {
    val backendLabel: String
    fun detect(bitmap: Bitmap): List<Detection>
}

/** Converts the flat `[x, y, w, h, label, score]*` array returned by the native layer. */
internal fun flatToDetections(flat: FloatArray, labels: List<String>): List<Detection> {
    val out = ArrayList<Detection>(flat.size / 6)
    var i = 0
    while (i + 5 < flat.size) {
        val label = flat[i + 4].toInt()
        out.add(
            Detection(
                rect = RectF(flat[i], flat[i + 1], flat[i] + flat[i + 2], flat[i + 1] + flat[i + 3]),
                label = label,
                labelName = labels.getOrElse(label) { "id$label" },
                score = flat[i + 5],
            )
        )
        i += 6
    }
    return out
}

internal class NcnnDetector private constructor(
    private val config: ModelConfig,
    override val backendLabel: String,
) : Detector {

    override fun detect(bitmap: Bitmap): List<Detection> = flatToDetections(
        NcnnNative.nativeDetect(bitmap, config.confidenceThreshold, config.nmsThreshold),
        config.labels,
    )

    override fun close() = NcnnNative.nativeRelease()

    companion object {
        fun create(context: Context, config: ModelConfig): Result<Detector> {
            val param = config.paramAsset!!
            val bin = config.binAsset!!
            if (!assetExists(context, param) || !assetExists(context, bin)) {
                return Result.failure(IllegalStateException("missing $param or $bin in assets"))
            }
            val ok = NcnnNative.nativeInit(
                context.assets, param, bin, config.inputName, config.outputName,
                config.inputSize, config.labels.size, config.decoded, !config.rgbOrder, config.useGpu,
            )
            if (!ok) return Result.failure(IllegalStateException("ncnn could not load $param"))
            val gpu = config.useGpu && NcnnNative.nativeHasGpu()
            val runtime = when {
                gpu -> "GPU · Vulkan"
                NcnnNative.nativeHasGpu() -> "CPU (GPU available)"
                else -> "CPU (no Vulkan GPU)"
            }
            return Result.success(NcnnDetector(config, "$runtime · ${NcnnNative.nativePrecision()}"))
        }
    }
}

internal class QnnDetector private constructor(
    private val config: ModelConfig,
    override val backendLabel: String,
) : Detector {

    override fun detect(bitmap: Bitmap): List<Detection> = flatToDetections(
        QnnNative.nativeDetect(bitmap, config.confidenceThreshold, config.nmsThreshold),
        config.labels,
    )

    override fun close() = QnnNative.nativeRelease()

    companion object {
        private val SOC_HINTS = listOf("qcom", "qualcomm", "kona", "lahaina", "taro", "kalama", "pineapple")

        /** arm64 Qualcomm device with the QNN native code compiled in. */
        fun isAvailable(): Boolean {
            val arm64 = Build.SUPPORTED_ABIS.any { it == "arm64-v8a" }
            val manufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MANUFACTURER else ""
            val hardware = listOf(Build.HARDWARE, Build.BOARD, Build.DEVICE).joinToString(" ").lowercase()
            val qualcomm = manufacturer.contains("qualcomm", ignoreCase = true) ||
                SOC_HINTS.any { it in hardware }
            return arm64 && qualcomm && runCatching { QnnNative.nativeIsBuilt() }.getOrDefault(false)
        }

        fun create(context: Context, config: ModelConfig): Result<Detector> {
            if (!isAvailable()) {
                return Result.failure(UnsupportedOperationException("QNN needs an arm64 Qualcomm device with the QNN build"))
            }
            val asset = config.binaryAsset!!
            if (!assetExists(context, asset)) {
                return Result.failure(IllegalStateException("missing $asset in assets"))
            }
            val error = QnnNative.nativeInit(
                context.assets, asset, context.applicationInfo.nativeLibraryDir,
                config.labels.size, config.boxesNormalized,
            )
            if (error.isNotEmpty()) return Result.failure(IllegalStateException(error))
            val precision = Regex("""\bin \[[^]]+] (u8q|i8q|u16q|i16q|fp16|fp32)\b""")
                .find(QnnNative.nativeDescribe())?.groupValues?.get(1)
                ?.let { if (it == "fp16") "FP16" else if (it == "fp32") "FP32" else it.uppercase() }
                ?: "unknown precision"
            return Result.success(QnnDetector(config, "NPU · Qualcomm HTP (QNN) · $precision"))
        }
    }
}

internal fun assetExists(context: Context, name: String): Boolean =
    runCatching { context.assets.open(name).use { } }.isSuccess
