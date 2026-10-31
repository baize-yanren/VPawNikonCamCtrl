#include "internal.h"

#include "ptp/ptp_constants.h"
#include "ptp/bytebuf.h"
#include "ptp/dataset.h"

#include <stdio.h>
#include <string.h>

/* 解析 GetObjectInfo 的 Data 阶段为一个 cc_object_info_t。 */
static cc_status_t parse_object_info(uint8_t *buf, size_t len, cc_object_info_t *out) {
    ptp_buf_t b;
    ptp_buf_init_read(&b, buf, len);

    uint32_t storage_id, compressed_size, thumb_size, thumb_w, thumb_h;
    uint32_t img_w, img_h, bit_depth, parent, assoc_desc, seq;
    uint16_t format, protection, thumb_format, assoc_type;

    if (ptp_buf_get_u32(&b, &storage_id) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u16(&b, &format) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u16(&b, &protection) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &compressed_size) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u16(&b, &thumb_format) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &thumb_size) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &thumb_w) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &thumb_h) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &img_w) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &img_h) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &bit_depth) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &parent) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u16(&b, &assoc_type) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &assoc_desc) != 0) return CC_ERR_PROTOCOL;
    if (ptp_buf_get_u32(&b, &seq) != 0) return CC_ERR_PROTOCOL;

    if (out) {
        memset(out, 0, sizeof(*out));
        out->compressed_size = compressed_size;
        out->format = format;
        out->is_association = (assoc_type != 0);
    }
    if (ptp_read_string(&b, out ? out->filename : NULL,
                        out ? sizeof(out->filename) : 0) != 0)
        return CC_ERR_PROTOCOL;
    if (ptp_read_string(&b, out ? out->capture_date : NULL,
                        out ? sizeof(out->capture_date) : 0) != 0)
        return CC_ERR_PROTOCOL;
    /* modificationDate 忽略 */
    (void)storage_id; (void)protection; (void)thumb_format;
    (void)thumb_size; (void)thumb_w; (void)thumb_h;
    (void)img_w; (void)img_h; (void)bit_depth; (void)parent;
    (void)assoc_desc; (void)seq;
    return CC_OK;
}

/* 获取对象信息；handle 由调用方提供，out->handle 会被填充。 */
static cc_status_t get_object_info(cc_session_t *s, uint32_t handle, cc_object_info_t *out) {
    uint32_t params[1] = {handle};
    uint16_t rc = PTP_RC_Undefined;
    size_t len = 0;
    cc_status_t st = cc_transact_data(s, PTP_OC_GetObjectInfo, params, 1,
                                      NULL, 0, s->rx_buf, CC_RX_BUF_SIZE, &len, &rc);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    if (out) out->handle = handle;
    return parse_object_info(s->rx_buf, len, out);
}

/* 获取对象大小（compressedSize）。 */
static cc_status_t get_object_size(cc_session_t *s, uint32_t handle, uint32_t *size) {
    cc_object_info_t info;
    cc_status_t st = get_object_info(s, handle, &info);
    if (st != CC_OK) return st;
    *size = info.compressed_size;
    return CC_OK;
}

cc_status_t cc_list_objects(cc_session_t *s, cc_object_info_t *out, size_t *count) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    if (!count) return CC_ERR_INVALID_ARG;
    size_t capacity = *count;
    *count = 0;

    /* GetStorageIDs */
    uint16_t rc = PTP_RC_Undefined;
    size_t len = 0;
    cc_status_t st = cc_transact_data(s, PTP_OC_GetStorageIDs, NULL, 0,
                                      NULL, 0, s->rx_buf, CC_RX_BUF_SIZE, &len, &rc);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);

    uint32_t storages[16];
    ptp_buf_t b;
    ptp_buf_init_read(&b, s->rx_buf, len);
    int nstor = ptp_read_u32_array(&b, storages, 16);
    if (nstor < 0) return CC_ERR_PROTOCOL;

    /* 遍历每个存储卷 */
    for (int si = 0; si < nstor; si++) {
        uint32_t params[3] = {storages[si], 0, 0xFFFFFFFFu};
        rc = PTP_RC_Undefined;
        len = 0;
        st = cc_transact_data(s, PTP_OC_GetObjectHandles, params, 3,
                              NULL, 0, s->rx_buf, CC_RX_BUF_SIZE, &len, &rc);
        if (st != CC_OK) return st;
        if (rc != PTP_RC_OK) continue;

        uint32_t handles[1024];
        ptp_buf_init_read(&b, s->rx_buf, len);
        int nh = ptp_read_u32_array(&b, handles, 1024);
        if (nh < 0) continue;

        for (int hi = 0; hi < nh; hi++) {
            cc_object_info_t info;
            if (get_object_info(s, handles[hi], &info) != CC_OK) continue;
            if (*count < capacity && out) {
                out[*count] = info;
            }
            (*count)++;
        }
    }
    return CC_OK;
}

/* 下载 sink：写入 FILE*，并上报进度。 */
typedef struct file_sink {
    FILE *fp;
    cc_session_t *s;
    uint64_t base;   /* 已存在的偏移 */
    uint64_t total;
} file_sink_t;

static int file_sink_write(void *ctx, const uint8_t *buf, size_t len) {
    file_sink_t *f = (file_sink_t *)ctx;
    if (fwrite(buf, 1, len, f->fp) != len) return -1;
    f->base += len;
    cc_emit_event(f->s, CC_EVENT_PROGRESS,
                  (uint32_t)f->base, (uint32_t)f->total);
    return 0;
}

cc_status_t cc_download(cc_session_t *s, uint32_t handle, const char *path) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    if (!path) return CC_ERR_INVALID_ARG;

    uint32_t size = 0;
    cc_status_t st = get_object_size(s, handle, &size);
    if (st != CC_OK) return st;

    char part_path[1024];
    snprintf(part_path, sizeof(part_path), "%s.part", path);

    /* 打开续传临时文件，定位到已下载偏移 */
    FILE *fp = fopen(part_path, "r+b");
    if (!fp) fp = fopen(part_path, "w+b");
    if (!fp) return CC_ERR_IO;
    fseek(fp, 0, SEEK_END);
    uint64_t offset = (uint64_t)ftell(fp);
    if (offset > size) {
        fclose(fp);
        remove(part_path);
        fp = fopen(part_path, "w+b");
        if (!fp) return CC_ERR_IO;
        offset = 0;
    }

    /* 优先 GetPartialObject 分块；不支持则回退 GetObject 全量 */
    int use_partial = 1;
    uint64_t remaining = (uint64_t)size - offset;

    if (use_partial) {
        while (remaining > 0) {
            uint32_t want = remaining < CC_CHUNK_SIZE ? (uint32_t)remaining : CC_CHUNK_SIZE;
            uint32_t params[3] = {handle, (uint32_t)offset, want};
            uint16_t rc = PTP_RC_Undefined;
            file_sink_t fs = {fp, s, offset, size};
            fseek(fp, (long)offset, SEEK_SET);
            st = cc_transact(s, PTP_OC_GetPartialObject, params, 3,
                             NULL, 0, file_sink_write, &fs, &rc, NULL, NULL);
            if (st != CC_OK) { fclose(fp); return st; }
            if (rc == PTP_RC_OperationNotSupported) {
                use_partial = 0;
                break;
            }
            if (rc != PTP_RC_OK) { fclose(fp); return cc_map_response(rc); }
            offset += want;
            remaining -= want;
        }
    }

    if (!use_partial) {
        /* 回退全量 GetObject：从头覆盖下载 */
        fclose(fp);
        fp = fopen(part_path, "w+b");
        if (!fp) return CC_ERR_IO;
        file_sink_t fs = {fp, s, 0, size};
        uint32_t params[1] = {handle};
        uint16_t rc = PTP_RC_Undefined;
        st = cc_transact(s, PTP_OC_GetObject, params, 1,
                         NULL, 0, file_sink_write, &fs, &rc, NULL, NULL);
        if (st != CC_OK) { fclose(fp); return st; }
        if (rc != PTP_RC_OK) { fclose(fp); return cc_map_response(rc); }
    }

    fclose(fp);
    if (rename(part_path, path) != 0) return CC_ERR_IO;
    return CC_OK;
}
