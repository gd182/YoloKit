#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>
#include <android/log.h>

#include <mutex>
#include <vector>

#include <gpu.h>

#include "yolo.h"
#include "jni_registration.h"
#include "logging.h"

#define LOG_TAG "YoloNcnn"
#define LOGI(...) YOLOKIT_LOG_INFO(LOG_TAG, __VA_ARGS__)
#define LOGE(...) YOLOKIT_LOG_ERROR(LOG_TAG, __VA_ARGS__)

static Yolo* g_yolo = nullptr;
static std::mutex g_lock;
// The Vulkan instance is created on first use and can be destroyed by nativeReleaseGpu()
// once no model is loaded. Guarded by g_lock.
static bool g_gpu_instance = false;

// Must be called with g_lock held.
static void ensure_gpu_instance()
{
#if NCNN_VULKAN
    if (!g_gpu_instance)
    {
        ncnn::create_gpu_instance();
        g_gpu_instance = true;
    }
#endif
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
{
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    if (!register_ncnn_natives(env) || !register_qnn_natives(env))
        return JNI_ERR;
    LOGI("JNI_OnLoad");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNI_OnUnload(JavaVM* vm, void*)
{
    std::lock_guard<std::mutex> lk(g_lock);
    delete g_yolo;
    g_yolo = nullptr;
#if NCNN_VULKAN
    if (g_gpu_instance)
    {
        ncnn::destroy_gpu_instance();
        g_gpu_instance = false;
    }
#endif
}

static std::string jstr(JNIEnv* env, jstring s)
{
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out(c ? c : "");
    env->ReleaseStringUTFChars(s, c);
    return out;
}

// Copies Java model bytes into a temporary native buffer for loading.
static std::vector<unsigned char> jbytes(JNIEnv* env, jbyteArray a)
{
    const jsize n = env->GetArrayLength(a);
    std::vector<unsigned char> out((size_t)n);
    if (n > 0)
        env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

static ModelSpec make_spec(JNIEnv* env, jstring inputName, jstring outputName,
                           jint targetSize, jint numClass, jboolean decoded, jboolean bgr)
{
    ModelSpec spec;
    spec.input_name = jstr(env, inputName);
    spec.output_name = jstr(env, outputName);
    spec.target_size = targetSize;
    spec.num_class = numClass;
    spec.decoded = decoded;
    spec.bgr = bgr;
    return spec;
}

// Must be called with g_lock held.
static jboolean finish_load(int ret)
{
    if (ret != 0)
    {
        LOGE("Yolo::load failed (%d)", ret);
        delete g_yolo;
        g_yolo = nullptr;
        return JNI_FALSE;
    }
    LOGI("model loaded, gpu=%d, weight_precision=%s",
         g_yolo->has_gpu(), g_yolo->weight_precision().c_str());
    return JNI_TRUE;
}

static jboolean JNICALL ncnn_InitAsset(
        JNIEnv* env, jobject, jobject assetManager,
        jstring paramAsset, jstring binAsset, jstring inputName, jstring outputName,
        jint targetSize, jint numClass, jboolean decoded, jboolean bgr, jboolean useGpu)
{
    std::lock_guard<std::mutex> lk(g_lock);
    ensure_gpu_instance();

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    ModelSpec spec = make_spec(env, inputName, outputName, targetSize, numClass, decoded, bgr);

    if (!g_yolo)
        g_yolo = new Yolo();
    return finish_load(g_yolo->load(mgr, jstr(env, paramAsset), jstr(env, binAsset), spec, useGpu));
}

static jboolean JNICALL ncnn_InitBytes(
        JNIEnv* env, jobject, jbyteArray paramBytes, jbyteArray binBytes,
        jstring inputName, jstring outputName,
        jint targetSize, jint numClass, jboolean decoded, jboolean bgr, jboolean useGpu)
{
    std::lock_guard<std::mutex> lk(g_lock);
    ensure_gpu_instance();

    const std::vector<unsigned char> param = jbytes(env, paramBytes);
    const std::vector<unsigned char> bin = jbytes(env, binBytes);
    const std::string paramText(param.begin(), param.end());
    ModelSpec spec = make_spec(env, inputName, outputName, targetSize, numClass, decoded, bgr);

    if (!g_yolo)
        g_yolo = new Yolo();
    return finish_load(g_yolo->load(paramText, bin.data(), bin.size(), spec, useGpu));
}

static jstring JNICALL ncnn_Precision(JNIEnv* env, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    const char* precision = g_yolo ? g_yolo->weight_precision().c_str() : "UNKNOWN";
    return env->NewStringUTF(precision);
}

static void JNICALL ncnn_Release(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    delete g_yolo;
    g_yolo = nullptr;
}

// Destroys the Vulkan instance. Does nothing while a model is loaded, because the model's
// Vulkan objects must be freed first (nativeRelease). The instance is recreated by the next nativeInit.
static void JNICALL ncnn_ReleaseGpu(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    if (g_yolo) return;
#if NCNN_VULKAN
    if (g_gpu_instance)
    {
        ncnn::destroy_gpu_instance();
        g_gpu_instance = false;
        LOGI("Vulkan instance released");
    }
#endif
}

static jboolean JNICALL ncnn_HasGpu(JNIEnv*, jobject)
{
#if NCNN_VULKAN
    std::lock_guard<std::mutex> lk(g_lock);
    ensure_gpu_instance();
    return ncnn::get_gpu_count() > 0 ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

static jfloatArray JNICALL ncnn_Detect(
        JNIEnv* env, jobject, jobject bitmap, jfloat probThreshold, jfloat nmsThreshold)
{
    std::lock_guard<std::mutex> lk(g_lock);
    if (!g_yolo)
        return env->NewFloatArray(0);

    AndroidBitmapInfo info;
    if (AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS)
        return env->NewFloatArray(0);
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888)
        return env->NewFloatArray(0);

    void* pixels = nullptr;
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS)
        return env->NewFloatArray(0);

    std::vector<Object> objects;
    g_yolo->detect((const unsigned char*)pixels, (int)info.width, (int)info.height,
                   objects, probThreshold, nmsThreshold);

    AndroidBitmap_unlockPixels(env, bitmap);

    jfloatArray result = env->NewFloatArray((jsize)(objects.size() * 6));
    std::vector<float> buf(objects.size() * 6);
    for (size_t i = 0; i < objects.size(); i++)
    {
        buf[i * 6 + 0] = objects[i].x;
        buf[i * 6 + 1] = objects[i].y;
        buf[i * 6 + 2] = objects[i].w;
        buf[i * 6 + 3] = objects[i].h;
        buf[i * 6 + 4] = (float)objects[i].label;
        buf[i * 6 + 5] = objects[i].prob;
    }
    env->SetFloatArrayRegion(result, 0, (jsize)buf.size(), buf.data());
    return result;
}

bool register_ncnn_natives(JNIEnv* env)
{
    const JNINativeMethod methods[] = {
        native_method("nativeHasGpu", "()Z", ncnn_HasGpu),
        native_method("nativeInitAsset",
                      "(Landroid/content/res/AssetManager;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZZZ)Z",
                      ncnn_InitAsset),
        native_method("nativeInitBytes",
                      "([B[BLjava/lang/String;Ljava/lang/String;IIZZZ)Z", ncnn_InitBytes),
        native_method("nativeDetect", "(Landroid/graphics/Bitmap;FF)[F", ncnn_Detect),
        native_method("nativeRelease", "()V", ncnn_Release),
        native_method("nativeReleaseGpu", "()V", ncnn_ReleaseGpu),
        native_method("nativePrecision", "()Ljava/lang/String;", ncnn_Precision),
    };
    return register_native_methods(env, "io/github/gd182/yolokit/internal/NcnnNative",
                                   methods, sizeof(methods) / sizeof(methods[0]));
}
