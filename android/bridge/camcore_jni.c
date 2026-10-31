/*
 * camcore_jni.c —— CamCore 的 Android JNI 桥
 *
 * 设计：Kotlin 层实现 Transport（UsbDeviceConnection.bulkTransfer / Socket），
 * 通过 nativeSetTransport 把 Kotlin 对象的回调方法注入 native；
 * native 侧用这些回调实现 cc_transport_ops_t，再调用 CamCore 的 C API。
 * 事件经 nativeEventCallback 回传 Kotlin。
 *
 * 编译：NDK（CMake 链接 CamCore 静态库）。本文件仅依赖 JNI 头 + camcore.h。
 */

#include <jni.h>
#include <string.h>
#include <stdlib.h>

#include "camcore.h"

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
    return (*env)->CallIntMethod(env, g_transport, g_mid_connect);
}

static int t_disconnect(void *ctx) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
    return (*env)->CallIntMethod(env, g_transport, g_mid_disconnect);
}

static int t_send(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    JNIEnv *env = NULL;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return -1;
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
    return (*env)->CallIntMethod(env, g_transport, g_mid_setTimeout, (jint)timeout_ms);
}

static const cc_transport_ops_t k_ops = {
    t_connect, t_disconnect, t_send, t_recv, t_set_timeout,
};

/* 会话实例（每 Java 侧一个控制器对应一个） */
static cc_session_t *g_session = NULL;

/* ------------------------------------------------------------------ */
/* JNI_OnLoad                                                          */
/* ------------------------------------------------------------------ */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

/* ------------------------------------------------------------------ */
/* native 方法                                                          */
/* ------------------------------------------------------------------ */
JNIEXPORT void JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeSetTransport(
        JNIEnv *env, jobject thiz, jobject transport, jobject listener) {
    (void)thiz;

    if (g_transport) (*env)->DeleteGlobalRef(env, g_transport);
    if (g_listener) (*env)->DeleteGlobalRef(env, g_listener);
    g_transport = NULL;
    g_listener = NULL;

    if (transport) {
        g_transport = (*env)->NewGlobalRef(env, transport);
        jclass cls = (*env)->GetObjectClass(env, transport);
        g_mid_connect = (*env)->GetMethodID(env, cls, "nativeConnect", "()I");
        g_mid_disconnect = (*env)->GetMethodID(env, cls, "nativeDisconnect", "()I");
        g_mid_send = (*env)->GetMethodID(env, cls, "nativeSend", "([B)I");
        g_mid_recv = (*env)->GetMethodID(env, cls, "nativeRecv", "([B)I");
        g_mid_setTimeout = (*env)->GetMethodID(env, cls, "nativeSetTimeout", "(I)I");
    }
    if (listener) {
        g_listener = (*env)->NewGlobalRef(env, listener);
        jclass cls = (*env)->GetObjectClass(env, listener);
        g_mid_onEvent = (*env)->GetMethodID(env, cls, "onNativeEvent", "(III)V");
    }
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeCreate(JNIEnv *env, jobject thiz, jint kind) {
    (void)env; (void)thiz;
    if (g_session) {
        cc_destroy(g_session);
        g_session = NULL;
    }
    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) return CC_ERR_NO_MEMORY;
    t->ops = &k_ops;
    t->ctx = NULL;
    t->kind = (cc_transport_kind_t)kind;
    g_session = cc_create(t, on_cam_event, NULL);
    if (!g_session) { free(t); return CC_ERR_NO_MEMORY; }
    return CC_OK;
}

JNIEXPORT jint JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeConnect(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (!g_session) return CC_ERR_NOT_CONNECTED;
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = g_session->transport ? g_session->transport->kind : CC_TRANSPORT_USB;
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
    if (!ctor) return NULL;

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
    return arr;
}

JNIEXPORT void JNICALL
Java_com_nikoncam_connector_CamCoreJni_nativeDestroy(JNIEnv *env, jobject thiz) {
    (void)env; (void)thiz;
    if (g_session) {
        cc_destroy(g_session);
        g_session = NULL;
    }
}
