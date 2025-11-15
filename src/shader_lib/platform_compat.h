/* Platform Compatibility Header for NeoWall
 * Provides compatibility layer between gleditor shader library and neowall
 */

#ifndef PLATFORM_COMPAT_H
#define PLATFORM_COMPAT_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include neowall's logging functions */
#include "neowall.h"

/* Prevent shader_log.h from redefining neowall's logging system */
#define SHADER_LIB_LOG_H

/* The shader library expects these logging macros to be defined
 * They are already defined in neowall.h, so we don't need to redefine them */

#endif /* PLATFORM_COMPAT_H */
