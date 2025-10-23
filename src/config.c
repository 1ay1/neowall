#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <dirent.h>
#include "../include/vibe.h"
#include "staticwall.h"

/* Get default configuration file path */
const char *config_get_default_path(void) {
    static char path[MAX_PATH_LENGTH];

    /* Try XDG_CONFIG_HOME first */
    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home) {
        snprintf(path, sizeof(path), "%s/staticwall/config.vibe", xdg_config_home);
        if (access(path, F_OK) == 0) {
            return path;
        }
    }

    /* Try ~/.config */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/staticwall/config.vibe", home);
        if (access(path, F_OK) == 0) {
            return path;
        }
    }

    /* Try /etc */
    snprintf(path, sizeof(path), "/etc/staticwall/config.vibe");
    if (access(path, F_OK) == 0) {
        return path;
    }

    /* Return user config path even if it doesn't exist */
    if (home) {
        snprintf(path, sizeof(path), "%s/.config/staticwall/config.vibe", home);
        return path;
    }

    return NULL;
}

/* Parse wallpaper mode from string */
enum wallpaper_mode wallpaper_mode_from_string(const char *str) {
    if (!str) {
        return MODE_FILL;
    }

    if (strcasecmp(str, "center") == 0) {
        return MODE_CENTER;
    } else if (strcasecmp(str, "stretch") == 0) {
        return MODE_STRETCH;
    } else if (strcasecmp(str, "fit") == 0) {
        return MODE_FIT;
    } else if (strcasecmp(str, "fill") == 0) {
        return MODE_FILL;
    } else if (strcasecmp(str, "tile") == 0) {
        return MODE_TILE;
    }

    log_error("Unknown wallpaper mode '%s', using 'fill'", str);
    return MODE_FILL;
}

/* Convert wallpaper mode to string */
const char *wallpaper_mode_to_string(enum wallpaper_mode mode) {
    switch (mode) {
        case MODE_CENTER:  return "center";
        case MODE_STRETCH: return "stretch";
        case MODE_FIT:     return "fit";
        case MODE_FILL:    return "fill";
        case MODE_TILE:    return "tile";
        default:           return "unknown";
    }
}

/* Parse transition type from string */
static enum transition_type transition_type_from_string(const char *str) {
    if (!str) {
        return TRANSITION_NONE;
    }

    if (strcasecmp(str, "none") == 0) {
        return TRANSITION_NONE;
    } else if (strcasecmp(str, "fade") == 0) {
        return TRANSITION_FADE;
    } else if (strcasecmp(str, "slide-left") == 0 || strcasecmp(str, "slide_left") == 0) {
        return TRANSITION_SLIDE_LEFT;
    } else if (strcasecmp(str, "slide-right") == 0 || strcasecmp(str, "slide_right") == 0) {
        return TRANSITION_SLIDE_RIGHT;
    } else if (strcasecmp(str, "glitch") == 0) {
        return TRANSITION_GLITCH;
    } else if (strcasecmp(str, "pixelate") == 0) {
        return TRANSITION_PIXELATE;
    }

    log_error("Unknown transition type '%s', using 'none'", str);
    return TRANSITION_NONE;
}

/* Check if a string is a supported image extension */
static bool is_image_file(const char *filename) {
    if (!filename) return false;

    const char *ext = strrchr(filename, '.');
    if (!ext) return false;

    ext++; /* Skip the dot */

    return (strcasecmp(ext, "png") == 0 ||
            strcasecmp(ext, "jpg") == 0 ||
            strcasecmp(ext, "jpeg") == 0);
}

/* Load all image files from a directory */
/* Helper to check if a file is a shader */
static bool is_shader_file(const char *name) {
    if (!name) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 5) {
        return false;
    }

    /* Check for .glsl extension */
    return (strcasecmp(name + len - 5, ".glsl") == 0);
}

/* Load shader files from a directory */
char **load_shaders_from_directory(const char *dir_path, size_t *count) {
    *count = 0;

    /* Expand path if needed */
    char expanded_path[MAX_PATH_LENGTH];
    if (dir_path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~: HOME not set");
            return NULL;
        }
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, dir_path + 1);
    } else {
        strncpy(expanded_path, dir_path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }
    
    /* Remove trailing slash to prevent double slashes in paths */
    size_t len = strlen(expanded_path);
    if (len > 1 && expanded_path[len - 1] == '/') {
        expanded_path[len - 1] = '\0';
    }

    /* Check if it's a directory */
    struct stat st;
    if (stat(expanded_path, &st) != 0) {
        log_error("Cannot access path %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    if (!S_ISDIR(st.st_mode)) {
        /* Not a directory, return NULL to use as single file */
        return NULL;
    }

    /* Open directory */
    DIR *dir = opendir(expanded_path);
    if (!dir) {
        log_error("Cannot open directory %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    /* First pass: count shader files */
    size_t shader_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            if (is_shader_file(entry->d_name)) {
                shader_count++;
            }
        }
    }

    if (shader_count == 0) {
        log_error("No shader files found in directory %s", expanded_path);
        closedir(dir);
        return NULL;
    }

    /* Allocate array for paths */
    char **paths = calloc(shader_count, sizeof(char *));
    if (!paths) {
        log_error("Failed to allocate memory for shader paths");
        closedir(dir);
        return NULL;
    }

    /* Second pass: collect shader paths */
    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < shader_count) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            if (is_shader_file(entry->d_name)) {
                /* Build full path */
                size_t path_len = strlen(expanded_path) + strlen(entry->d_name) + 2;
                paths[idx] = malloc(path_len);
                if (paths[idx]) {
                    snprintf(paths[idx], path_len, "%s/%s", expanded_path, entry->d_name);
                    idx++;
                }
            }
        }
    }

    closedir(dir);

    *count = idx;
    log_info("Loaded %zu shaders from directory %s", idx, expanded_path);

    return paths;
}

char **load_images_from_directory(const char *dir_path, size_t *count) {
    *count = 0;

    /* Expand path if needed */
    char expanded_path[MAX_PATH_LENGTH];
    if (dir_path[0] == '~') {
        const char *home = getenv("HOME");
        if (!home) {
            log_error("Cannot expand ~: HOME not set");
            return NULL;
        }
        snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, dir_path + 1);
    } else {
        strncpy(expanded_path, dir_path, sizeof(expanded_path) - 1);
        expanded_path[sizeof(expanded_path) - 1] = '\0';
    }
    
    /* Remove trailing slash to prevent double slashes in paths */
    size_t len = strlen(expanded_path);
    if (len > 1 && expanded_path[len - 1] == '/') {
        expanded_path[len - 1] = '\0';
    }

    /* Check if it's a directory */
    struct stat st;
    if (stat(expanded_path, &st) != 0) {
        log_error("Cannot access path %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    if (!S_ISDIR(st.st_mode)) {
        /* Not a directory, return NULL to use as single file */
        return NULL;
    }

    /* Open directory */
    DIR *dir = opendir(expanded_path);
    if (!dir) {
        log_error("Cannot open directory %s: %s", expanded_path, strerror(errno));
        return NULL;
    }

    /* First pass: count image files */
    size_t img_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            if (is_image_file(entry->d_name)) {
                img_count++;
            }
        }
    }

    if (img_count == 0) {
        log_error("No image files found in directory %s", expanded_path);
        closedir(dir);
        return NULL;
    }

    /* Allocate array for paths */
    char **paths = calloc(img_count, sizeof(char *));
    if (!paths) {
        log_error("Failed to allocate memory for image paths");
        closedir(dir);
        return NULL;
    }

    /* Second pass: collect image paths */
    rewinddir(dir);
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < img_count) {
        if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
            if (is_image_file(entry->d_name)) {
                /* Build full path */
                size_t path_len = strlen(expanded_path) + strlen(entry->d_name) + 2;
                paths[idx] = malloc(path_len);
                if (paths[idx]) {
                    snprintf(paths[idx], path_len, "%s/%s", expanded_path, entry->d_name);
                    idx++;
                }
            }
        }
    }

    closedir(dir);

    *count = idx;
    log_info("Loaded %zu images from directory %s", idx, expanded_path);

    return paths;
}

/* Parse wallpaper configuration from VIBE object */
static bool parse_wallpaper_config(VibeValue *obj, struct wallpaper_config *config) {
    if (!obj || !config || obj->type != VIBE_TYPE_OBJECT) {
        return false;
    }

    /* Initialize with defaults */
    config->type = WALLPAPER_IMAGE;
    config->path[0] = '\0';
    config->shader_path[0] = '\0';
    config->mode = MODE_FILL;
    config->duration = 0;
    config->transition = TRANSITION_NONE;
    config->transition_duration = 300;
    config->cycle = false;
    config->cycle_paths = NULL;
    config->cycle_count = 0;
    config->current_cycle_index = 0;

    /* Parse shader (takes precedence over path) */
    VibeValue *shader = vibe_object_get(obj->as_object, "shader");
    if (shader && shader->type == VIBE_TYPE_STRING) {
        config->type = WALLPAPER_SHADER;
        
        /* Check if shader path is a directory */
        size_t shader_count = 0;
        char **shader_paths = load_shaders_from_directory(shader->as_string, &shader_count);
        
        if (shader_paths && shader_count > 0) {
            /* Directory with shaders - enable cycling */
            config->cycle = true;
            config->cycle_count = shader_count;
            config->cycle_paths = shader_paths;
            /* Use first shader as initial shader_path */
            strncpy(config->shader_path, shader_paths[0], sizeof(config->shader_path) - 1);
            config->shader_path[sizeof(config->shader_path) - 1] = '\0';
            log_info("Loaded %zu shaders from directory for cycling", shader_count);
        } else {
            /* Single shader file */
            strncpy(config->shader_path, shader->as_string, sizeof(config->shader_path) - 1);
            config->shader_path[sizeof(config->shader_path) - 1] = '\0';
        }
    }

    /* Parse path (for image wallpapers) */
    VibeValue *path = vibe_object_get(obj->as_object, "path");
    if (path && path->type == VIBE_TYPE_STRING && config->type == WALLPAPER_IMAGE) {
        strncpy(config->path, path->as_string, sizeof(config->path) - 1);
        config->path[sizeof(config->path) - 1] = '\0';
    }

    /* Parse mode */
    VibeValue *mode = vibe_object_get(obj->as_object, "mode");
    if (mode && mode->type == VIBE_TYPE_STRING) {
        config->mode = wallpaper_mode_from_string(mode->as_string);
    }

    /* Parse duration */
    VibeValue *duration = vibe_object_get(obj->as_object, "duration");
    if (duration && duration->type == VIBE_TYPE_INTEGER) {
        config->duration = (uint32_t)duration->as_integer;
    }

    /* Parse transition */
    VibeValue *transition = vibe_object_get(obj->as_object, "transition");
    if (transition && transition->type == VIBE_TYPE_STRING) {
        config->transition = transition_type_from_string(transition->as_string);
    }

    /* Parse transition_duration */
    VibeValue *trans_dur = vibe_object_get(obj->as_object, "transition_duration");
    if (trans_dur && trans_dur->type == VIBE_TYPE_INTEGER) {
        config->transition_duration = (uint32_t)trans_dur->as_integer;
    }

    /* Parse cycle - can be an array of paths OR a single directory path */
    VibeValue *cycle = vibe_object_get(obj->as_object, "cycle");
    if (cycle) {
        if (cycle->type == VIBE_TYPE_ARRAY) {
            /* Array of paths */
            size_t count = cycle->as_array->count;
            if (count > 0) {
                config->cycle = true;
                config->cycle_count = count;
                config->cycle_paths = calloc(count, sizeof(char *));

                if (!config->cycle_paths) {
                    log_error("Failed to allocate cycle paths array");
                    return false;
                }

                for (size_t i = 0; i < count; i++) {
                    VibeValue *elem = cycle->as_array->values[i];
                    if (elem && elem->type == VIBE_TYPE_STRING) {
                        config->cycle_paths[i] = strdup(elem->as_string);
                    } else {
                        log_error("Cycle path at index %zu is not a string", i);
                        config->cycle_paths[i] = strdup("");
                    }
                }

                log_debug("Loaded %zu wallpapers for cycling from array", count);
            }
        } else if (cycle->type == VIBE_TYPE_STRING) {
            /* Single path - could be a directory */
            const char *cycle_path = cycle->as_string;
            size_t dir_count = 0;
            char **dir_paths = load_images_from_directory(cycle_path, &dir_count);

            if (dir_paths && dir_count > 0) {
                /* Successfully loaded from directory */
                config->cycle = true;
                config->cycle_count = dir_count;
                config->cycle_paths = dir_paths;
                log_info("Loaded %zu wallpapers from directory: %s", dir_count, cycle_path);
            } else {
                /* Not a directory or failed, treat as single file */
                config->cycle = true;
                config->cycle_count = 1;
                config->cycle_paths = calloc(1, sizeof(char *));
                if (config->cycle_paths) {
                    config->cycle_paths[0] = strdup(cycle_path);
                    log_debug("Using single file for cycling: %s", cycle_path);
                }
            }
        }
    }

    return true;
}

/* Free wallpaper configuration */
void config_free_wallpaper(struct wallpaper_config *config) {
    if (!config) {
        return;
    }

    if (config->cycle_paths) {
        for (size_t i = 0; i < config->cycle_count; i++) {
            if (config->cycle_paths[i]) {
                free(config->cycle_paths[i]);
            }
        }
        free(config->cycle_paths);
        config->cycle_paths = NULL;
    }

    config->cycle_count = 0;
}

/* Create default configuration file */
static bool config_create_default(const char *config_path) {
    if (!config_path) {
        return false;
    }

    /* Get directory path */
    char dir_path[MAX_PATH_LENGTH];
    strncpy(dir_path, config_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    /* Find last slash to get directory */
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir_path, &st) == -1) {
        /* Create directory recursively */
        char tmp[MAX_PATH_LENGTH];
        char *p = NULL;
        snprintf(tmp, sizeof(tmp), "%s", dir_path);

        for (p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
                    log_error("Failed to create directory %s: %s", tmp, strerror(errno));
                    return false;
                }
                *p = '/';
            }
        }

        if (mkdir(tmp, 0755) == -1 && errno != EEXIST) {
            log_error("Failed to create directory %s: %s", tmp, strerror(errno));
            return false;
        }
    }

    /* Get the installation path or use built-in default */
    const char *default_wallpaper_path = NULL;

    /* Copy default wallpaper to user's local directory if it doesn't exist */
    const char *home = getenv("HOME");
    if (home) {
        char user_wallpaper_dir[MAX_PATH_LENGTH];
        char user_wallpaper_path[MAX_PATH_LENGTH];
        int ret1 = snprintf(user_wallpaper_dir, sizeof(user_wallpaper_dir), "%s/.local/share/staticwall", home);
        if (ret1 < 0 || (size_t)ret1 >= sizeof(user_wallpaper_dir)) {
            log_error("Path too long for user wallpaper directory");
            default_wallpaper_path = "~/Pictures/wallpaper.png";
        } else {
            int ret2 = snprintf(user_wallpaper_path, sizeof(user_wallpaper_path), "%s/default.png", user_wallpaper_dir);
            if (ret2 < 0 || (size_t)ret2 >= sizeof(user_wallpaper_path)) {
                log_error("Path too long for user wallpaper path");
                default_wallpaper_path = "~/Pictures/wallpaper.png";
            } else {
                /* Check if user already has the default wallpaper */
                if (access(user_wallpaper_path, F_OK) != 0) {
                    /* Try to find and copy the default wallpaper from installation */
                    const char *source_paths[] = {
                        "/usr/share/staticwall/default.png",
                        "/usr/local/share/staticwall/default.png",
                        NULL
                    };
                    
                    for (int i = 0; source_paths[i] != NULL; i++) {
                        if (access(source_paths[i], R_OK) == 0) {
                            /* Create user wallpaper directory recursively */
                            char tmp[MAX_PATH_LENGTH];
                            snprintf(tmp, sizeof(tmp), "%s/.local", home);
                            mkdir(tmp, 0755);
                            snprintf(tmp, sizeof(tmp), "%s/.local/share", home);
                            mkdir(tmp, 0755);
                            mkdir(user_wallpaper_dir, 0755);
                            
                            /* Copy the file */
                            FILE *src = fopen(source_paths[i], "rb");
                            if (src) {
                                FILE *dst = fopen(user_wallpaper_path, "wb");
                                if (dst) {
                                    char buffer[4096];
                                    size_t bytes;
                                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                                            log_error("Failed to write to %s", user_wallpaper_path);
                                            break;
                                        }
                                    }
                                    fclose(dst);
                                    log_info("Copied default wallpaper to %s", user_wallpaper_path);
                                }
                                fclose(src);
                            }
                            break;
                        }
                    }
                }
                
                /* Use the user's local copy if it exists */
                if (access(user_wallpaper_path, F_OK) == 0) {
                    default_wallpaper_path = "~/.local/share/staticwall/default.png";
                } else {
                    default_wallpaper_path = "~/Pictures/wallpaper.png";
                }
            }
        }
    } else {
        default_wallpaper_path = "~/Pictures/wallpaper.png";
    }

    /* Try to copy example config from installation as the main config */
    const char *example_config_paths[] = {
        "/usr/share/staticwall/config.vibe.example",
        "/usr/local/share/staticwall/config.vibe.example",
        NULL
    };
    
    bool copied_config = false;
    for (int i = 0; example_config_paths[i] != NULL; i++) {
        if (access(example_config_paths[i], R_OK) == 0) {
            /* Copy example config as the main config file */
            FILE *src = fopen(example_config_paths[i], "rb");
            if (src) {
                FILE *dst = fopen(config_path, "wb");
                if (dst) {
                    char buffer[4096];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                            log_error("Failed to write to %s", config_path);
                            break;
                        }
                    }
                    fclose(dst);
                    log_info("Created configuration file from example: %s", config_path);
                    copied_config = true;
                }
                fclose(src);
            }
            break;
        }
    }
    
    /* Try to copy example shaders from installation if available */
    const char *shader_install_paths[] = {
        "/usr/share/staticwall/shaders",
        "/usr/local/share/staticwall/shaders",
        NULL
    };
    
    bool copied_shaders = false;
    for (int i = 0; shader_install_paths[i] != NULL; i++) {
        if (access(shader_install_paths[i], R_OK) == 0) {
            /* Create user shader directory */
            char user_shader_dir[MAX_PATH_LENGTH];
            snprintf(user_shader_dir, sizeof(user_shader_dir), "%s/.config/staticwall/shaders", home ? home : "");
            
            /* Create directory structure */
            char tmp[MAX_PATH_LENGTH];
            snprintf(tmp, sizeof(tmp), "%s/.config", home);
            mkdir(tmp, 0755);
            snprintf(tmp, sizeof(tmp), "%s/.config/staticwall", home);
            mkdir(tmp, 0755);
            mkdir(user_shader_dir, 0755);
            
            /* Copy all shader files */
            DIR *dir = opendir(shader_install_paths[i]);
            if (dir) {
                struct dirent *entry;
                int shader_count = 0;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
                        /* Check if it's a .glsl or README file */
                        size_t len = strlen(entry->d_name);
                        if ((len > 5 && strcmp(entry->d_name + len - 5, ".glsl") == 0) ||
                            strcmp(entry->d_name, "README.md") == 0) {
                            
                            /* Check if paths will fit in buffer */
                            size_t install_len = strlen(shader_install_paths[i]);
                            size_t shader_dir_len = strlen(user_shader_dir);
                            size_t name_len = strlen(entry->d_name);
                            
                            if (install_len + name_len + 2 > MAX_PATH_LENGTH ||
                                shader_dir_len + name_len + 2 > MAX_PATH_LENGTH) {
                                log_error("Shader path too long: %s", entry->d_name);
                                continue;
                            }
                            
                            char src_path[MAX_PATH_LENGTH];
                            char dst_path[MAX_PATH_LENGTH];
                            int src_len = snprintf(src_path, sizeof(src_path), "%s/%s", shader_install_paths[i], entry->d_name);
                            int dst_len = snprintf(dst_path, sizeof(dst_path), "%s/%s", user_shader_dir, entry->d_name);
                            
                            if (src_len < 0 || src_len >= (int)sizeof(src_path) ||
                                dst_len < 0 || dst_len >= (int)sizeof(dst_path)) {
                                log_error("Failed to build shader path: %s", entry->d_name);
                                continue;
                            }
                            
                            FILE *src = fopen(src_path, "rb");
                            if (src) {
                                FILE *dst = fopen(dst_path, "wb");
                                if (dst) {
                                    char buffer[4096];
                                    size_t bytes;
                                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                                        if (fwrite(buffer, 1, bytes, dst) != bytes) {
                                            log_error("Failed to write shader to %s", dst_path);
                                            break;
                                        }
                                    }
                                    fclose(dst);
                                    shader_count++;
                                }
                                fclose(src);
                            }
                        }
                    }
                }
                closedir(dir);
                
                if (shader_count > 0) {
                    log_info("Copied %d example shader(s) to %s", shader_count, user_shader_dir);
                    copied_shaders = true;
                }
            }
            break;
        }
    }
    
    /* If we couldn't copy the example config, create a minimal fallback */
    if (!copied_config) {
        log_info("Could not find example config, creating minimal fallback");
        
        const char *fallback_config =
            "# Staticwall Configuration\n"
            "# Minimal fallback config\n\n"
            "default {\n"
            "  path %s\n"
            "  mode fill\n"
            "}\n";

        FILE *fp = fopen(config_path, "w");
        if (!fp) {
            log_error("Failed to create default config file: %s", strerror(errno));
            return false;
        }

        fprintf(fp, fallback_config, default_wallpaper_path);
        fclose(fp);
        
        log_info("Created minimal configuration file: %s", config_path);
    }
    if (copied_shaders) {
        log_info("Example shaders available at ~/.config/staticwall/shaders/");
    }
    if (copied_config) {
        log_info("Edit %s to customize your wallpaper setup", config_path);
    }

    return true;
}

/* Load configuration file */
bool config_load(struct staticwall_state *state, const char *config_path) {
    if (!state || !config_path) {
        log_error("Invalid parameters for config_load");
        return false;
    }

    log_info("Loading configuration from: %s", config_path);

    /* Check if file exists, create default if not */
    struct stat st;
    if (stat(config_path, &st) == -1) {
        log_info("Configuration file not found, creating default: %s", config_path);
        if (!config_create_default(config_path)) {
            log_error("Failed to create default configuration");
            return false;
        }
        /* Re-stat to get the new file */
        if (stat(config_path, &st) == -1) {
            log_error("Failed to stat newly created config file");
            return false;
        }
    }

    /* Store modification time for watching */
    state->config_mtime = st.st_mtime;

    /* Read file content */
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        log_error("Failed to open config file: %s", strerror(errno));
        return false;
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        log_error("Config file is empty");
        fclose(fp);
        return false;
    }

    /* Read entire file */
    char *content = malloc(file_size + 1);
    if (!content) {
        log_error("Failed to allocate memory for config");
        fclose(fp);
        return false;
    }

    size_t read_size = fread(content, 1, file_size, fp);
    content[read_size] = '\0';
    fclose(fp);

    /* Parse VIBE */
    VibeParser *parser = vibe_parser_new();
    if (!parser) {
        log_error("Failed to create VIBE parser");
        free(content);
        return false;
    }

    VibeValue *root = vibe_parse_string(parser, content);
    free(content);

    if (!root) {
        VibeError error = vibe_get_last_error(parser);
        if (error.has_error) {
            log_error("Failed to parse VIBE config at line %d, column %d: %s",
                     error.line, error.column, error.message);
        } else {
            log_error("Failed to parse VIBE config");
        }
        vibe_parser_free(parser);
        return false;
    }

    if (root->type != VIBE_TYPE_OBJECT) {
        log_error("Config root must be an object");
        vibe_value_free(root);
        vibe_parser_free(parser);
        return false;
    }

    /* Parse default configuration */
    VibeValue *default_obj = vibe_object_get(root->as_object, "default");
    struct wallpaper_config default_config = {0};

    if (default_obj && default_obj->type == VIBE_TYPE_OBJECT) {
        if (!parse_wallpaper_config(default_obj, &default_config)) {
            log_error("Failed to parse default configuration");
            vibe_value_free(root);
            vibe_parser_free(parser);
            return false;
        }
        if (default_config.type == WALLPAPER_SHADER) {
            log_info("Loaded default configuration: shader=%s, mode=%s",
                     default_config.shader_path, wallpaper_mode_to_string(default_config.mode));
        } else {
            log_info("Loaded default configuration: path=%s, mode=%s",
                     default_config.path, wallpaper_mode_to_string(default_config.mode));
        }
    } else {
        log_debug("No default configuration found");
    }

    /* Apply default config to all outputs */
    struct output_state *output = state->outputs;
    while (output) {
        /* Apply config if it has either a path or shader */
        if (default_config.path[0] != '\0' || default_config.shader_path[0] != '\0') {
            /* Copy default config */
            struct wallpaper_config config_copy;
            memcpy(&config_copy, &default_config, sizeof(struct wallpaper_config));

            /* Duplicate cycle paths if present */
            if (default_config.cycle && default_config.cycle_count > 0) {
                config_copy.cycle_paths = calloc(default_config.cycle_count, sizeof(char *));
                if (config_copy.cycle_paths) {
                    for (size_t i = 0; i < default_config.cycle_count; i++) {
                        config_copy.cycle_paths[i] = strdup(default_config.cycle_paths[i]);
                    }
                }
            }

            output_apply_config(output, &config_copy);
        }
        output = output->next;
    }

    /* Parse output-specific configurations */
    VibeValue *output_obj = vibe_object_get(root->as_object, "output");
    if (output_obj && output_obj->type == VIBE_TYPE_OBJECT) {
        /* Iterate through all keys in output object */
        for (size_t i = 0; i < output_obj->as_object->count; i++) {
            const char *output_name = output_obj->as_object->entries[i].key;
            VibeValue *output_config_obj = output_obj->as_object->entries[i].value;

            if (output_config_obj->type != VIBE_TYPE_OBJECT) {
                log_error("Output configuration for '%s' must be an object", output_name);
                continue;
            }

            struct wallpaper_config output_config;
            if (!parse_wallpaper_config(output_config_obj, &output_config)) {
                log_error("Failed to parse configuration for output %s", output_name);
                continue;
            }

            log_info("Loaded configuration for output '%s': path=%s, mode=%s",
                     output_name, output_config.path,
                     wallpaper_mode_to_string(output_config.mode));

            /* Find matching output and apply config */
            struct output_state *target = state->outputs;
            while (target) {
                if (strcmp(target->model, output_name) == 0) {
                    output_apply_config(target, &output_config);
                    log_info("Applied configuration to output %s", output_name);
                    break;
                }
                target = target->next;
            }

            if (!target) {
                log_debug("Output '%s' not found, configuration will be applied when it appears",
                         output_name);
                config_free_wallpaper(&output_config);
            }
        }
    }

    /* Clean up */
    config_free_wallpaper(&default_config);
    vibe_value_free(root);
    vibe_parser_free(parser);

    log_info("Configuration loaded successfully");

    return true;
}

/* Check if configuration file has changed */
bool config_has_changed(struct staticwall_state *state) {
    if (!state || state->config_path[0] == '\0') {
        return false;
    }

    struct stat st;
    if (stat(state->config_path, &st) == -1) {
        log_error("Failed to stat config file: %s", strerror(errno));
        return false;
    }

    return st.st_mtime != state->config_mtime;
}

/* Reload configuration */
void config_reload(struct staticwall_state *state) {
    if (!state) {
        return;
    }

    log_info("Reloading configuration...");

    pthread_mutex_lock(&state->state_mutex);

    /* Free existing wallpaper configs */
    struct output_state *output = state->outputs;
    while (output) {
        config_free_wallpaper(&output->config);
        output = output->next;
    }

    /* Reload config file */
    if (!config_load(state, state->config_path)) {
        log_error("Failed to reload configuration");
    } else {
        log_info("Configuration reloaded successfully");
        
        /* Mark all outputs for redraw to apply new configuration */
        output = state->outputs;
        while (output) {
            output->needs_redraw = true;
            log_debug("Marked output %s for redraw after config reload", 
                     output->model[0] ? output->model : "unknown");
            output = output->next;
        }
    }

    pthread_mutex_unlock(&state->state_mutex);

    state->reload_requested = false;
}

/* Configuration watch thread */
void *config_watch_thread(void *arg) {
    struct staticwall_state *state = arg;

    if (!state) {
        return NULL;
    }

    log_info("Configuration watcher thread started");

    while (state->running) {
        sleep(CONFIG_WATCH_INTERVAL);

        if (!state->running) {
            break;
        }

        if (config_has_changed(state)) {
            log_info("Configuration file changed, triggering reload");
            state->reload_requested = true;
            
            /* Wake up the event loop by writing to eventfd */
            if (state->wakeup_fd >= 0) {
                uint64_t value = 1;
                ssize_t s = write(state->wakeup_fd, &value, sizeof(value));
                if (s != sizeof(value)) {
                    log_error("Failed to wake event loop: %s", strerror(errno));
                } else {
                    log_debug("Event loop woken for config reload");
                }
            }
        }
    }

    log_info("Configuration watcher thread stopped");

    return NULL;
}

/* Parse wallpaper config by output name (for external use) */
bool config_parse_wallpaper(struct wallpaper_config *config, const char *output_name) {
    /* This is a simplified version for API compatibility */
    (void)config;
    (void)output_name;
    return true;
}