#ifndef RUNEC_VIEWER_LOGGING_H
#define RUNEC_VIEWER_LOGGING_H

#include <stdlib.h>
#include <string.h>

static inline int runec_viewer_env_enabled(const char *key, int fallback) {
    const char *value = getenv(key);
    if (!value || !value[0])
        return fallback;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0
            || strcmp(value, "FALSE") == 0 || strcmp(value, "off") == 0
            || strcmp(value, "OFF") == 0 || strcmp(value, "no") == 0
            || strcmp(value, "NO") == 0) {
        return 0;
    }
    return 1;
}

static inline int runec_viewer_verbose_asset_logs(void) {
    return runec_viewer_env_enabled("RUNEC_VERBOSE_ASSET_LOGS", 0);
}

#endif
