/*
 * camcore_napi.cpp —— CamCore 的 HarmonyOS NAPI 桥
 *
 * 与 Android JNI 桥同构：ArkTS 实现 Transport（@ohos.usbManager / socket），
 * 经 napi 回调注入 CamCore；CamCore 事件经 napi 回调回传 ArkTS。
 *
 * 构建：DevEco Studio 的 CMake（链接 CamCore 静态库 + libace_napi.z.so）。
 * 注意：相机 USB 控制本身也可纯 ArkTS（见 ZENCHE PtpCamera.ets），
 * 本桥的价值在于复用 CamCore 的协议/状态机，两端共享同一套核心。
 */

#include <napi/native_api.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "camcore.h"
}

/* ------------------------------------------------------------------ */
/* 全局状态                                                            */
/* ------------------------------------------------------------------ */
static napi_env g_env = nullptr;
static napi_ref g_transport_ref = nullptr;   /* ArkTS NativeTransport */
static napi_ref g_listener_ref = nullptr;    /* ArkTS 事件监听器 */
static cc_session_t *g_session = nullptr;
static cc_transport_t g_transport{};

static napi_value call_arkts(napi_ref ref, const char *method, napi_value arg) {
    napi_value obj, fn, result;
    napi_get_reference_value(g_env, ref, &obj);
    napi_get_named_property(g_env, obj, method, &fn);
    napi_call_function(g_env, obj, fn, 1, &arg, &result);
    return result;
}

static int t_connect(void *ctx, const void *params) {
    (void)ctx; (void)params;
    return 0;
}
static int t_disconnect(void *ctx) {
    (void)ctx;
    return 0;
}
static int t_send(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    napi_value buf;
    void *p = nullptr;
    napi_create_arraybuffer(g_env, len, &p, &buf);
    std::memcpy(p, data, len);
    napi_value r = call_arkts(g_transport_ref, "nativeSend", buf);
    int32_t n = 0;
    napi_get_value_int32(g_env, r, &n);
    return n;
}
static int t_recv(void *ctx, uint8_t *data, size_t len) {
    (void)ctx;
    napi_value buf;
    void *p = nullptr;
    napi_create_arraybuffer(g_env, len, &p, &buf);
    napi_value r = call_arkts(g_transport_ref, "nativeRecv", buf);
    int32_t n = 0;
    napi_get_value_int32(g_env, r, &n);
    if (n > 0) std::memcpy(data, p, (size_t)n);
    return n;
}
static int t_set_timeout(void *ctx, int timeout_ms) {
    (void)ctx; (void)timeout_ms;
    return 0;
}

static const cc_transport_ops_t k_ops = {
    t_connect, t_disconnect, t_send, t_recv, t_set_timeout,
};

static void on_cam_event(void *userdata, const cc_event_t *ev) {
    (void)userdata;
    if (!ev || !g_listener_ref) return;
    napi_value argv[3];
    napi_create_int32(g_env, (int32_t)ev->type, &argv[0]);
    napi_create_uint32(g_env, ev->arg0, &argv[1]);
    napi_create_uint32(g_env, ev->arg1, &argv[2]);
    napi_value obj, fn, result;
    napi_get_reference_value(g_env, g_listener_ref, &obj);
    napi_get_named_property(g_env, obj, "onNativeEvent", &fn);
    napi_call_function(g_env, obj, fn, 3, argv, &result);
}

/* ------------------------------------------------------------------ */
/* NAPI 导出方法                                                        */
/* ------------------------------------------------------------------ */
static napi_value set_transport(napi_env env, napi_callback_info info) {
    g_env = env;
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    if (argc >= 1) {
        if (g_transport_ref) napi_delete_reference(env, g_transport_ref);
        napi_create_reference(env, argv[0], 1, &g_transport_ref);
    }
    if (argc >= 2) {
        if (g_listener_ref) napi_delete_reference(env, g_listener_ref);
        napi_create_reference(env, argv[1], 1, &g_listener_ref);
    }
    napi_value r;
    napi_get_undefined(env, &r);
    return r;
}

static napi_value create_session(napi_env env, napi_callback_info info) {
    if (g_session) { cc_destroy(g_session); g_session = nullptr; }
    g_transport.ops = &k_ops;
    g_transport.ctx = nullptr;
    g_transport.kind = CC_TRANSPORT_USB;
    g_session = cc_create(&g_transport, on_cam_event, nullptr);
    napi_value r;
    napi_create_int32(env, g_session ? 0 : -9, &r);
    return r;
}

static napi_value connect_cam(napi_env env, napi_callback_info info) {
    (void)info;
    cc_connect_params_t p;
    std::memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;
    int st = g_session ? (int)cc_connect(g_session, &p) : -2;
    napi_value r;
    napi_create_int32(env, st, &r);
    return r;
}

static napi_value capture(napi_env env, napi_callback_info info) {
    (void)info;
    int st = g_session ? (int)cc_capture(g_session) : -2;
    napi_value r;
    napi_create_int32(env, st, &r);
    return r;
}

static napi_value download(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    int32_t handle = 0;
    napi_get_value_int32(env, argv[0], &handle);
    size_t len = 0;
    napi_get_value_string_utf8(env, argv[1], nullptr, 0, &len);
    char *path = (char *)malloc(len + 1);
    napi_get_value_string_utf8(env, argv[1], path, len + 1, &len);
    int st = g_session ? (int)cc_download(g_session, (uint32_t)handle, path) : -2;
    free(path);
    napi_value r;
    napi_create_int32(env, st, &r);
    return r;
}

static napi_value set_param(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2];
    napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
    int32_t prop = 0, value = 0;
    napi_get_value_int32(env, argv[0], &prop);
    napi_get_value_int32(env, argv[1], &value);
    int st = g_session ? (int)cc_set_param(g_session, (uint16_t)prop, (uint32_t)value) : -2;
    napi_value r;
    napi_create_int32(env, st, &r);
    return r;
}

static napi_value destroy_session(napi_env env, napi_callback_info info) {
    (void)info;
    if (g_session) { cc_destroy(g_session); g_session = nullptr; }
    napi_value r;
    napi_get_undefined(env, &r);
    return r;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"setTransport", nullptr, set_transport, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"createSession", nullptr, create_session, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"connect", nullptr, connect_cam, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"capture", nullptr, capture, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"download", nullptr, download, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setParam", nullptr, set_param, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"destroy", nullptr, destroy_session, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "camcore",
    .nm_priv = nullptr,
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterCamcoreModule(void) {
    napi_module_register(&demoModule);
}
