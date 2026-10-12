#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>
#include <android/log.h>

#include <mutex>
#include <vector>

#include <gpu.h>

#include "yolo.h"

#define LOG_TAG "YoloNcnn"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativeInit(
        JNIEnv* env, jobject, jobject assetManager,
        jstring paramAsset, jstring binAsset, jstring inputName, jstring outputName,
        jint targetSize, jint numClass, jboolean decoded, jboolean bgr, jboolean useGpu)
{
    std::lock_guard<std::mutex> lk(g_lock);
    ensure_gpu_instance();

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);

    ModelSpec spec;
    spec.param_asset = jstr(env, paramAsset);
    spec.bin_asset = jstr(env, binAsset);
    spec.input_name = jstr(env, inputName);
    spec.output_name = jstr(env, outputName);
    spec.target_size = targetSize;
    spec.num_class = numClass;
    spec.decoded = decoded;
    spec.bgr = bgr;

    if (!g_yolo)
        g_yolo = new Yolo();

    int ret = g_yolo->load(mgr, spec, useGpu);

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

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativePrecision(JNIEnv* env, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    const char* precision = g_yolo ? g_yolo->weight_precision().c_str() : "UNKNOWN";
    return env->NewStringUTF(precision);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativeRelease(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lk(g_lock);
    delete g_yolo;
    g_yolo = nullptr;
}

// Destroys the Vulkan instance. Does nothing while a model is loaded, because the model's
// Vulkan objects must be freed first (nativeRelease). The instance is recreated by the next nativeInit.
extern "C" JNIEXPORT void JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativeReleaseGpu(JNIEnv*, jobject)
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

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativeHasGpu(JNIEnv*, jobject)
{
#if NCNN_VULKAN
    std::lock_guard<std::mutex> lk(g_lock);
    ensure_gpu_instance();
    return ncnn::get_gpu_count() > 0 ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_io_github_gd182_yolokit_internal_NcnnNative_nativeDetect(
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
