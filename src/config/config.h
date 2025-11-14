#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
struct neowall_state;
struct wallpaper_config;

/* Configuration parsing */
bool config_load(struct neowall_state *state, const char *config_path);
bool config_parse_wallpaper(struct wallpaper_config *config, const char *output_name);
void config_free_wallpaper(struct wallpaper_config *config);
const char *config_get_default_path(void);
char **load_images_from_directory(const char *dir_path, size_t *count);
char **load_shaders_from_directory(const char *dir_path, size_t *count);

#endif /* CONFIG_H */
