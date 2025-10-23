#include <stddef.h>
#include "staticwall.h"
#include "transitions.h"

/**
 * Transition Registry
 * 
 * Central registry for all transition effects. New transitions can be added
 * simply by implementing the transition_render_func signature and registering
 * it in the transitions array.
 * 
 * This modular architecture makes it easy to:
 * - Add new transitions without modifying core render code
 * - Maintain transitions in separate, focused files
 * - Enable/disable transitions at compile time
 * - Test transitions independently
 */

static const struct transition transitions[] = {
    { TRANSITION_FADE,        "fade",        transition_fade_render },
    { TRANSITION_SLIDE_LEFT,  "slide_left",  transition_slide_left_render },
    { TRANSITION_SLIDE_RIGHT, "slide_right", transition_slide_right_render },
    { TRANSITION_GLITCH,      "glitch",      transition_glitch_render },
};

static const size_t transition_count = sizeof(transitions) / sizeof(transitions[0]);

/**
 * Initialize transitions system
 * 
 * Currently no initialization needed, but provides hook for future
 * enhancements like dynamic registration or shader precompilation.
 */
void transitions_init(void) {
    log_debug("Transition system initialized with %zu transitions", transition_count);
}

/**
 * Render a transition effect
 * 
 * Dispatches to the appropriate transition renderer based on type.
 * If the transition type is not found, returns false.
 * 
 * @param output Output state containing images and textures
 * @param type Type of transition to render
 * @param progress Transition progress (0.0 to 1.0)
 * @return true on success, false on error or unknown transition
 */
bool transition_render(struct output_state *output, enum transition_type type, float progress) {
    if (!output) {
        log_error("Invalid output for transition render");
        return false;
    }

    /* Find and execute the transition renderer */
    for (size_t i = 0; i < transition_count; i++) {
        if (transitions[i].type == type) {
            log_debug("Rendering transition: %s (progress=%.2f)", 
                     transitions[i].name, progress);
            return transitions[i].render(output, progress);
        }
    }

    log_error("Unknown transition type: %d", type);
    return false;
}
