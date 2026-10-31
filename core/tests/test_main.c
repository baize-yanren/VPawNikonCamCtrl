#include "mock_camera.h"

#include "../src/ptp/ptp_constants.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);            \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static void on_event(void *ud, const cc_event_t *ev) {
    (void)ud;
    (void)ev;
}

static void test_connect(void) {
    printf("test_connect\n");
    cc_transport_t *t = mock_create();
    cc_session_t *s = cc_create(t, on_event, NULL);
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;

    CHECK(cc_connect(s, &p) == CC_OK);
    CHECK(cc_disconnect(s) == CC_OK);

    cc_destroy(s);
    mock_destroy(t);
}

static void test_list_objects(void) {
    printf("test_list_objects\n");
    cc_transport_t *t = mock_create();
    cc_session_t *s = cc_create(t, on_event, NULL);
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;
    cc_connect(s, &p);

    cc_object_info_t objs[16];
    size_t n = 16;
    CHECK(cc_list_objects(s, objs, &n) == CC_OK);
    CHECK(n == 2);
    if (n >= 2) {
        CHECK(strcmp(objs[0].filename, "DSC_0001.JPG") == 0);
        CHECK(objs[0].compressed_size == MOCK_JPG_SIZE);
        CHECK(strcmp(objs[1].filename, "DSC_0001.NEF") == 0);
    }

    cc_destroy(s);
    mock_destroy(t);
}

static void test_download(void) {
    printf("test_download\n");
    const char *path = "test_download.out";
    remove(path);

    cc_transport_t *t = mock_create();
    cc_session_t *s = cc_create(t, on_event, NULL);
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;
    cc_connect(s, &p);

    /* 10 MiB 文件，触发分块下载 */
    CHECK(cc_download(s, MOCK_HANDLE_JPG, path) == CC_OK);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        CHECK((unsigned long)sz == (unsigned long)MOCK_JPG_SIZE);
    }

    /* 校验首字节内容（mock 可预测字节） */
    f = fopen(path, "rb");
    if (f) {
        unsigned char b = 0;
        fread(&b, 1, 1, f);
        fclose(f);
        /* mock_byte(handle, 0) = (0*7 + handle + 13) & 0xFF */
        CHECK(b == (unsigned char)((MOCK_HANDLE_JPG + 13u) & 0xFFu));
    }

    remove(path);
    cc_destroy(s);
    mock_destroy(t);
}

static void test_param(void) {
    printf("test_param\n");
    cc_transport_t *t = mock_create();
    cc_session_t *s = cc_create(t, on_event, NULL);
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;
    cc_connect(s, &p);

    uint32_t v = 0;
    CHECK(cc_get_param(s, PTP_DPC_ExposureTime, &v) == CC_OK);
    CHECK(v == 10000);
    CHECK(cc_get_param(s, PTP_DPC_ExposureIndex, &v) == CC_OK);
    CHECK(v == 100);
    CHECK(cc_set_param(s, PTP_DPC_ExposureIndex, 200) == CC_OK);

    cc_destroy(s);
    mock_destroy(t);
}

static void test_capture(void) {
    printf("test_capture\n");
    cc_transport_t *t = mock_create();
    cc_session_t *s = cc_create(t, on_event, NULL);
    cc_connect_params_t p;
    memset(&p, 0, sizeof(p));
    p.kind = CC_TRANSPORT_USB;
    cc_connect(s, &p);

    CHECK(cc_capture(s) == CC_OK);

    cc_destroy(s);
    mock_destroy(t);
}

int main(void) {
    test_connect();
    test_list_objects();
    test_download();
    test_param();
    test_capture();

    if (g_failures == 0) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", g_failures);
    return 1;
}
