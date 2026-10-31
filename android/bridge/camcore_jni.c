/*
 * camcore_jni.c —— CamCore 的 Android JNI 桥
 *
 * 设计：Kotlin 层实现 Transport（UsbDeviceConnection.bulkTransfer / Socket），
 * 通过 nativeSetTransport 把 Kotlin 对象的回调方法注入 native；
 * native 侧用这些回调实现 cc_transport_ops_t，再调用 CamCore 的 C API。
 * 事件经 nativeEventCallback 回传 Kotlin。
 *
 * USB-PTP：Kotlin 传输即「裸 PTP 容器字节流」，直接注入核心。
 * PTP/IP：Kotlin 传输是「命令连接 TCP 字节流」，native 侧经 cc_ptpip_transport
 *        完成 PTP/IP 握手与帧编解码后再注入核心（复用 C 实现，两端零差异）。
 *
 * 编译：NDK（CMake 链接 CamCore 静态库）。本文件仅依赖 JNI 头 + camcore.h + ptpip_transport.h。
 */

#include <jni.h>
#include <string.h>
#include <stdlib.h>

#include "camcore.h"
#include "ptpip/ptpip_transport.h"

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */
static JavaVM *g_vm = NULL;

static jobject g_transport = NULL;          /* Kotlin NativeTransport 实例 */
static jmethodID g_mid_connect = NULL;
static jmethodID g_mid_disconnect = NULL;
static jmethodID g_mid_send = NULL;
static jmethodID g_mid_recv = NULL;
static jmethodID g_mid_setTimeout = NULL;

static jobject g_listener = NULL;           /* Kotlin 事件监听器 */
static jmethodID g_mid_onEvent = NULL;

static cc_session_t *g_session = NULL;      /* 会话实例 */
static cc_transport_t *g_raw = NULL;        /* 直接包装 Kotlin 传输的 transport */
static cc_transport_t *g_ptpip = NULL;      /* PTP/IP 适配 transport（可为 NULL） */
static int g_kind = CC_TRANSPORT_USB;       /* 当前传输类型 */

/* ------------------------------------------------------------------ */
/* 事件桥                                                              */
/* ------------------------------------------------------------------ */
static void jni_emit(int type, unsigned int a0, unsigned int a1) {
    if (!g_listener) return;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return;
    (*env)->CallVoidMethod(env, g_listener, g_mid_onEvent, (jint)type, (jint)a0, (jint)a1);
}

static void on_cam_event(void *userdata, const cc_event_t *ev) {
    (void)userdata;
    if (!ev) return;
    jni_emit((int)ev->type, ev->arg0, ev->arg1);
}

/* ------------------------------------------------------------------ */
/* Transport ops（经 JNI 回调 Kotlin）                                  */
/* ------------------------------------------------------------------ */
static int t_connect(void *ctx, const void *params) {
    (void)ctx; (void)params;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    if (!g_transport || !g_mid_connect) return -1;
    return (*env)->CallIntMethod(env, g_transport, g_mid_connect);
}

static int t_disconnect(void *ctx) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    if (!g_transport || !g_mid_disconnect) return -1;
    return (*env)->CallIntMethod(env, g_transport, g_mid_disconnect);
}

static int t_send(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    if (!g_transport || !g_mid_send) return -1;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr) return -1;
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)data);
    jint n = (*env)->CallIntMethod(env, g_transport, g_mid_send, arr);
    (*env)->DeleteLocalRef(env, arr);
    return (int)n;
}

static int t_recv(void *ctx, uint8_t *data, size_t len) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    if (!g_transport || !g_mid_recv) return -1;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)len);
    if (!arr) return -1;
    jint n = (*env)->CallIntMethod(env, g_transport, g_mid_recv, arr);
    if (n > 0) {
        (*env)->GetByteArrayRegion(env, arr, 0, n, (jbyte *)data);
    }
    (*env)->DeleteLocalRef(env, arr);
    return (int)n;
}

static int t_set_timeout(void *ctx, int timeout_ms) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    if (!g_transport || !g_mid_setTimeout) return -1;
    return (*env)->CallIntMethod(env, g_transport, g_mid_setTimeout, (jint)timeout_ms);
}

static const cc_transport_ops_t k_ops = {
    t_connect, t_disconnect, t_send, t_recv, t_set_timeout,
};

/* ------------------------------------------------------------------ */
/* JNI_OnLoad                                                          */
/* ------------------------------------------------------------------ */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

/* ------------------------------------------------------------------ */
/* 内部辅助：释放会话与 transport                                        */
/* ------------------------------------------------------------------ */
static void jni_destroy_session(void) {
    if (g_session) { cc_destroy(g_session); g_session = NULL; }
    if (g_ptpip) { cc_ptpip_transport_free(g_ptpip); g_ptpip = NULL; }
    if (g_raw) { free(g_raw); g_raw = NULL; }
}

static void jni_clear_refs(JNIEnv *env) {
    if (g_transport) { (*env)->DeleteGlobalRef(env, g_transport); g_transport = NULL; }
    if (g_listener) { (*env)->DeleteGlobalRef(env, g_listener); g_listener = NULL; }
    g_mid_connect = g_mid_disconnect = g_mid_send = g_mid_recv = g_mid_setTimeout = NULL;
    g_mid_onEvent = NULL;
}

/* 缓存 Kotlin 传输与监听器的全局引用与方法 ID；失败返回 0。 */
static int jni_set_transport(JNIEnv *env, jobject transport, jobject listener) {
    if (transport) {
        jobject g = (*env)->NewGlobalRef(env, transport);
        jclass cls = (*env)->GetObjectClass(env, transport);
        g_mid_connect = (*env)->GetMethodID(env, cls, "nativeConnect", "()I");
        g_mid_disconnect = (*env)->GetMethodID(env, cls, "nativeDisconnect", "()I");
        g_mid_send = (*env)->GetMethodID(env, cls, "nativeSend", "([B)I");
        g_mid_recv = (*env)->GetMethodID(env, cls, "nativeRecv", "([B)I");
        g_mid_setTimeout = (*env)->GetMethodID(env, cls, "nativeSetTimeout", "(I)I");
        (*env)->DeleteLocalRef(env, cls);
        if (!g_mid_connect || !g_mid_disconnect || !g_mid_send ||
            !g_mid_recv || !g_mid_setTimeout) {
            (*env)->DeleteGlobalRef(env, g);
            return 0;
        }
        g_transport = g;
    }
    if (listener) {
        jobject g = (*env)->NewGlobalRef(env, listener);
        jclass cls = (*env)->GetObjectClass(env, listener);
        g_mid_onEvent = (*env)->GetMethodID(env, cls, "onNativeEvent", "(III)V");
        (*env)->DeleteLocalRef(env, cls);
        if (!g_mid_onEvent) {
            (*env)->DeleteGlobalRef(env, g);
            return 0;
        }
        g_listener = g;
    }
    return 1;
}

/* 用「直接包装 Kotlin 传输的 raw transport」创建会话（USB 场景）。 */
static cc_status_t create_usb_session(void) {
    jni_destroy_session();
    g_kind = CC_TRANSPORT_USB;

    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) return CC_ERR_NO_MEMORY;
    t->ops = &k_ops;
    t->ctx = NULL;
    t->kind = CC_TRANSPORT_USB;

    cc_session_t *s = cc_create(t, on_cam_event, NULL);
    if (!s) { free(t); return CC_ERR_NO_MEMORY; }

    g_raw = t;
    g_session = s;
    return CC_OK;
}

/* ------------------------------------------------------------------ */
/* native 方法                                                          */
/* ------------------------------------------------------------------ */
JNIEXPORT void JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeSetTransport(
        JNIEnv *env, jobject thiz, jobject transport, jobject listener) {
    (void)thiz;
    jni_clear_refs(env);
    if (transport || listener) {
        if (!jni_set_transport(env, transport, listener)) {
            jni_clear_refs(env);
        }
    }
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeCreate(JNIEnv *env, jobject thiz, jint kind) {
    (void)env; (void)thiz; (void)kind;
    return (jint)create_usb_session();
}

/* PTP/IP：包装命令连接字节流 → cc_ptpip_transport → 会话。事件通道暂降级(NULL)。 */
JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeCreatePtpIp(
        JNIEnv *env, jobject thiz, jobject transport, jobject listener) {
    (void)thiz;

    if (!transport) return CC_ERR_INVALID_ARG;

    jni_clear_refs(env);
    if (!jni_set_transport(env, transport, listener)) {
        jni_clear_refs(env);
        return CC_ERR_INVALID_ARG;
    }
    jni_destroy_session();
    g_kind = CC_TRANSPORT_PTPIP;

    /* raw transport 直接包装 Kotlin TCP 命令字节流 */
    cc_transport_t *raw = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!raw) return CC_ERR_NO_MEMORY;
    raw->ops = &k_ops;
    raw->ctx = NULL;
    raw->kind = CC_TRANSPORT_PTPIP;

    cc_ptpip_streams_t streams;
    streams.command = raw;
    streams.event = NULL; /* 事件通道降级：拍摄后走 CheckEvent 轮询 */

    cc_transport_t *ptpip = cc_ptpip_transport_new(&streams, 350);
    if (!ptpip) { free(raw); return CC_ERR_NO_MEMORY; }

    cc_session_t *s = cc_create(ptpip, on_cam_event, NULL);
    if (!s) { cc_ptpip_transport_free(ptpip); free(raw); return CC_ERR_NO_MEMORY; }

    g_raw = raw;
    g_ptpip = ptpip;
    g_session = s;
    return CC_OK;
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeConnect(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = (cc_transport_kind_t)g_kind;
    p.port = 15740;
    return (jint)cc_connect(g_session, &p);
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeDisconnect(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_OK;
    return (jint)cc_disconnect(g_session);
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeCapture(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    return (jint)cc_capture(g_session);
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeStartLiveView(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    return (jint)cc_start_live_view(g_session);
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeGetLiveViewFrame(JNIEnv *env, jobject thiz, jbyteArray out) {
    (void)thiz;
    if (!g_session || !out) return CC_ERR_NOT_CONNECTED;
    jsize cap = (*env)->GetArrayLength(env, out);
    uint8_t *buf = (uint8_t *)malloc((size_t)cap);
    if (!buf) return CC_ERR_NO_MEMORY;
    size_t len = (size_t)cap;
    cc_status_t st = cc_get_live_view_frame(g_session, buf, &len);
    if (st == CC_OK) {
        (*env)->SetByteArrayRegion(env, out, 0, (jsize)len, (const jbyte *)buf);
    }
    free(buf);
    return (jint)st;
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeEndLiveView(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    return (jint)cc_end_live_view(g_session);
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeDownload(JNIEnv *env, jobject thiz, jint handle, jstring path) {
    (void)thiz;
    if (!g_session || !path) return CC_ERR_NOT_CONNECTED;
    const char *p = (*env)->GetStringUTFChars(env, path, NULL);
    if (!p) return CC_ERR_NO_MEMORY;
    cc_status_t st = cc_download(g_session, (uint32_t)handle, p);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return (jint)st;
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeGetParam(JNIEnv *env, jobject thiz, jint prop) {
    (void)env; (void)thiz;
    if (!g_session) return 0;
    uint32_t v = 0;
    cc_status_t st = cc_get_param(g_session, (uint16_t)prop, &v);
    (void)st;
    return (jint)v;
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeSetParam(JNIEnv *env, jobject thiz, jint prop, jint value) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    return (jint)cc_set_param(g_session, (uint16_t)prop, (uint32_t)value);
}

JNIEXPORT jobjectArray JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeListObjects(JNIEnv *env, jobject thiz) {
    (void)thiz;
    if (!g_session) return NULL;

    cc_object_info_t objs[512];
    size_t n = 512;
    cc_status_t st = cc_list_objects(g_session, objs, &n);
    if (st != CC_OK) n = 0;

    jclass cls = (*env)->FindClass(env, "com/nikoncam/connector/ObjectInfo");
    if (!cls) return NULL;
    jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(IJLjava/lang/String;)V");
    if (!ctor) { (*env)->DeleteLocalRef(env, cls); return NULL; }

    jobjectArray arr = (*env)->NewObjectArray(env, (jsize)n, cls, NULL);
    for (size_t i = 0; i < n; i++) {
        jstring name = (*env)->NewStringUTF(env, objs[i].filename);
        jobject o = (*env)->NewObject(env, cls, ctor,
                                      (jint)objs[i].handle,
                                      (jlong)objs[i].compressed_size,
                                      name);
        (*env)->SetObjectArrayElement(env, arr, (jsize)i, o);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, o);
    }
    (*env)->DeleteLocalRef(env, cls);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeDestroy(JNIEnv *env, jobject thiz) {
    (void)thiz;
    jni_destroy_session();
    jni_clear_refs(env);
}
