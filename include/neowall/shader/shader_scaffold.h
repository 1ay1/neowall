/* Shader scaffolding — `neowall new <name>`.
 *
 * The standard library and scene kit are only useful if an author knows they
 * exist. Discovering them currently means reading SHADER_NOTES.md and typing
 * the boilerplate from memory, which is exactly the friction that stops people
 * trying an idea at all.
 *
 * So: one command writes a commented, working shader into the user's shader
 * directory and prints the two lines needed to run it. The templates are not
 * empty stubs -- each is a real wallpaper that looks like something the moment
 * it loads, because a starting point you immediately want to modify beats one
 * you have to fill in first.
 */

#ifndef NEOWALL_SHADER_SCAFFOLD_H
#define NEOWALL_SHADER_SCAFFOLD_H

#include <stdbool.h>
#include <stdio.h>

/* Create a new shader from a template.
 *
 * `name` is the shader name; a `.glsl` suffix is optional and added if absent.
 * `template_name` selects the starting point (see neowall_scaffold_templates);
 * NULL means the default. `force` overwrites an existing file.
 *
 * The file lands in ~/.config/neowall/shaders/, which is created if needed.
 * Returns true on success; on failure the reason is already printed.
 */
bool neowall_scaffold_create(const char *name, const char *template_name, bool force);

/* Print the available templates, one per line, with a short description.
 * Takes the stream so a usage message can put everything on stderr and keep
 * its ordering, while the successful `--list` path writes to stdout. */
void neowall_scaffold_list_templates(FILE *out);

#endif /* NEOWALL_SHADER_SCAFFOLD_H */
