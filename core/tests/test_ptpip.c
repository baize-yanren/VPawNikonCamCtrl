#include "mock_ptpip_camera.h"
#include "mock_ptp_core.h"

#include "../src/ptpip/ptpip_transport.h"
#include "../src/ptp/ptp_constants.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);           \
            g_failures++;                                                 \
        }                                                                 \
    } while (0)

static void on_event(void *ud, const cc_event_t *ev) {
    (void)ud;
    (void)ev;
}

/* 已连接 PTP/IP 会话环境。 */
typedef struct ptpip_env {
    cc_transport_t *cmd;
    cc_transport_t *evt;
    cc_transport_t *ptpip;
    cc_session_t *s;
} ptpip_env_t;

static void env_setup(ptpip_env_t *e) {
    mock_ptpip_create(&e->cmd, &e->evt);
    cc_ptpip_streams_t streams;
    streams.command = e->cmd;
    streams.event = e->evt;
    e->ptpip = cc_ptpip_transport_new(&streams, 0); /* 测试延迟 0ms */
    e->s = cc_create(e->ptpip, on_event, NULL);
}

static void env_connect(ptpip_env_t *e) {
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_PTPIP;
    cc_connect(e->s, &p);
}

static void env_teardown(ptpip_env_t *e) {
    cc_destroy(e->s);
    cc_ptpip_transport_free(e->ptpip);
    mock_ptpip_destroy(e->cmd, e->evt);
}

static void test_connect(void) {
    printf("test_ptpip_connect\n");
    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);
    CHECK(cc_ptpip_transport_event_ok(e.ptpip) == 1); /* 事件通道握手成功 */
    CHECK(cc_disconnect(e.s) == CC_OK);
    env_teardown(&e);
}

static void test_list_objects(void) {
    printf("test_ptpip_list_objects\n");
    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);

    cc_object_info_t objs[16];
    size_t n = 16;
    CHECK(cc_list_objects(e.s, objs, &n) == CC_OK);
    CHECK(n == 2);
    if (n >= 2) {
        CHECK(strcmp(objs[0].filename, "DSC_0001.JPG") == 0);
        CHECK(objs[0].compressed_size == MOCK_JPG_SIZE);
        CHECK(strcmp(objs[1].filename, "DSC_0001.NEF") == 0);
    }
    env_teardown(&e);
}

static void test_download(void) {
    printf("test_ptpip_download\n");
    const char *path = "test_ptpip_download.out";
    remove(path);

    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);

    CHECK(cc_download(e.s, MOCK_HANDLE_JPG, path) == CC_OK);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        CHECK((unsigned long)sz == (unsigned long)MOCK_JPG_SIZE);
    }

    /* 校验首字节（mock 可预测字节） */
    f = fopen(path, "rb");
    if (f) {
        unsigned char b = 0;
        fread(&b, 1, 1, f);
        fclose(f);
        CHECK(b == (unsigned char)((MOCK_HANDLE_JPG + 13u) & 0xFFu));
    }

    remove(path);
    env_teardown(&e);
}

static void test_param(void) {
    printf("test_ptpip_param\n");
    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);

    uint32_t v = 0;
    CHECK(cc_get_param(e.s, PTP_DPC_ExposureTime, &v) == CC_OK);
    CHECK(v == 10000);
    CHECK(cc_get_param(e.s, PTP_DPC_ExposureIndex, &v) == CC_OK);
    CHECK(v == 100);
    CHECK(cc_set_param(e.s, PTP_DPC_ExposureIndex, 200) == CC_OK);

    env_teardown(&e);
}

static void test_capture(void) {
    printf("test_ptpip_capture\n");
    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);

    CHECK(cc_capture(e.s) == CC_OK);

    env_teardown(&e);
}

static void test_live_view(void) {
    printf("test_ptpip_live_view\n");
    ptpip_env_t e;
    env_setup(&e);
    env_connect(&e);

    CHECK(cc_start_live_view(e.s) == CC_OK);
    uint8_t frame[256];
    size_t len = sizeof(frame);
    CHECK(cc_get_live_view_frame(e.s, frame, &len) == CC_OK);
    CHECK(len > 0);
    if (len >= 2) {
        CHECK(frame[0] == 0xFF && frame[1] == 0xD8); /* JPEG SOI */
    }
    CHECK(cc_end_live_view(e.s) == CC_OK);

    env_teardown(&e);
}

int main(void) {
    /* 无缓冲输出，避免 MinGW 下 stdout 经管道时全缓冲导致的输出丢失。 */
    setvbuf(stdout, NULL, _IONBF, 0);
    test_connect();
    test_list_objects();
    test_download();
    test_param();
    test_capture();
    test_live_view();

    if (g_failures == 0) {
        printf("\nALL PTPIP TESTS PASSED\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", g_failures);
    return 1;
}
