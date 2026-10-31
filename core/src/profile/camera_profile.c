#include "camera_profile.h"
#include <string.h>

/* 尼康 Z 系（EXPEED 6/7）+ D 系（EXPEED 5）机型档案。 */
static const camera_profile_t k_profiles[] = {
    {"D500", 0x043A, 5, 100, 51200},
    {"D7500", 0x0445, 5, 100, 51200},
    {"D850", 0x044A, 5, 64, 25600},
    {"Z7", 0x0442, 6, 64, 25600},
    {"Z6", 0x0443, 6, 100, 51200},
    {"Z50", 0x0444, 6, 100, 51200},
    {"D780", 0x0446, 6, 100, 51200},
    {"D6", 0x0447, 6, 100, 102400},
    {"Z5", 0x0448, 6, 100, 51200},
    {"Z7II", 0x044B, 6, 64, 25600},
    {"Z6II", 0x044C, 6, 100, 51200},
    {"Z fc", 0x044F, 6, 100, 51200},
    {"Z30", 0x0452, 6, 100, 51200},
    {"Z9", 0x0450, 7, 64, 25600},
    {"Z8", 0x0451, 7, 64, 25600},
    {"Z f", 0x0453, 7, 100, 64000},
    {"Z6III", 0x0454, 7, 100, 64000},
    {"Z50II", 0x0455, 7, 100, 51200},
    {"Z5II", 0x0456, 7, 100, 64000},
    {"ZR", 0x0457, 7, 100, 51200},
};

const camera_profile_t *camera_profile_by_pid(uint16_t pid) {
    for (size_t i = 0; i < camera_profile_count(); i++) {
        if (k_profiles[i].product_id == pid) return &k_profiles[i];
    }
    return NULL;
}

const camera_profile_t *camera_profile_by_name(const char *model) {
    if (!model) return NULL;
    for (size_t i = 0; i < camera_profile_count(); i++) {
        /* 大小写不敏感的子串匹配，兼容 "NIKON Z 50II" 这类型号串。 */
        const char *n = k_profiles[i].name;
        const char *found = model;
        size_t nlen = strlen(n);
        while ((found = strchr(found, n[0])) != NULL) {
            /* 简单子串比较（忽略大小写） */
            int ok = 1;
            for (size_t j = 0; j < nlen; j++) {
                char a = n[j];
                char b = found[j];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) { ok = 0; break; }
            }
            if (ok) return &k_profiles[i];
            found++;
        }
    }
    return NULL;
}

size_t camera_profile_count(void) {
    return sizeof(k_profiles) / sizeof(k_profiles[0]);
}
