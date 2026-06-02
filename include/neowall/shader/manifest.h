/* .neowall manifest — explicit channel bindings, reactive uniforms, presets.
 *
 * A manifest is an optional sidecar that turns a bare .glsl into a *scene*: it
 * names which live source feeds each iChannel (killing the binding heuristic),
 * declares custom float uniforms bound to live system/audio signals, and can
 * carry named presets. It reuses the project's VIBE parser so the syntax is the
 * same as config.vibe.
 *
 *   shader   train_journey.glsl     # optional; defaults to <name>.glsl
 *   channel0 audio                  # iChannel0 = live audio texture
 *   channel1 self                   # iChannel1 = previous frame
 *   uniform  uWind   cpu            # float uWind = live CPU load
 *   uniform  uGlow   audio_bass     # float uGlow = low-band audio energy
 *   uniform  uExposure 0.8          # float uExposure = constant 0.8
 *
 * Resolution: given a shader path "foo.glsl", manifest_for_shader() looks for
 * "foo.neowall" beside it. If the shader path itself ends in ".neowall" it is
 * treated as the manifest directly (and its `shader` key names the .glsl). */

#ifndef NEOWALL_SHADER_MANIFEST_H
#define NEOWALL_SHADER_MANIFEST_H

#include <stdbool.h>
#include "neowall/shader/shader_multipass.h"

/* Apply a manifest (if one exists) to an already-created multipass shader,
 * before compilation. `shader_path` is the path the engine was asked to load.
 *
 * If shader_path ends in ".neowall" the file is the manifest. Otherwise a
 * sidecar "<base>.neowall" is looked up next to the .glsl. If no manifest is
 * found this is a no-op returning false (the heuristic stays in charge).
 *
 * Returns true if a manifest was found and applied. */
bool manifest_apply(multipass_shader_t *shader, const char *shader_path);

/* If shader_path is a ".neowall" manifest, resolve the .glsl it references and
 * copy it into out (size cap). Returns true if shader_path was a manifest and a
 * shader path was resolved; false if shader_path is an ordinary shader. */
bool manifest_resolve_shader_path(const char *shader_path, char *out, size_t out_size);

#endif /* NEOWALL_SHADER_MANIFEST_H */
