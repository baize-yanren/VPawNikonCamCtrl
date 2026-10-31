#ifndef CAMERA_PROFILE_H
#define CAMERA_PROFILE_H

/*
 * 尼康机型档案表：承载各机型 PID 与世代（EXPEED）信息。
 * 数据来自 docs/PROTOCOL.md 的 clean-room 对照笔记（与 ZENCHE 机型表一致）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct camera_profile {
    const char *name;    /* 机型名，如 "Z50II" */
    uint16_t product_id; /* USB PID */
    uint8_t expeed;      /* 世代：5 / 6 / 7，0 未知 */
    uint32_t min_iso;
    uint32_t max_iso;
} camera_profile_t;

/* 按 PID 查找机型档案；未命中返回 NULL。 */
const camera_profile_t *camera_profile_by_pid(uint16_t pid);

/* 按名称模糊匹配（如型号字符串含 "Z50II"）；未命中返回 NULL。 */
const camera_profile_t *camera_profile_by_name(const char *model);

/* 内置机型档案数量。 */
size_t camera_profile_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_PROFILE_H */
