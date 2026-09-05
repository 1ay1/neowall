/* Standalone mutation-fuzz driver for neowall's libFuzzer harnesses.
 * Lets us run tests/fuzz_vibe.c and tests/fuzz_multipass.c under GCC+ASan
 * without clang/libFuzzer. Seeds from real corpus files, then mutates.
 *
 * Build example:
 *   gcc -g -O1 -fsanitize=address,undefined -I include -I build/include \
 *       tests/fuzz_vibe.c tests/fuzz_driver.c src/config/vibe_impl.c -o fuzz_vibe_drv
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint64_t rng_state = 0x2545F4914F6CDD1DULL;
static uint64_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

#define MAX_SEEDS 512
#define MAX_SEED_BYTES (512 * 1024)
static uint8_t *seeds[MAX_SEEDS];
static size_t seed_lens[MAX_SEEDS];
static int seed_count;

static void add_seed(const char *path) {
    if (seed_count >= MAX_SEEDS) return;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint8_t *buf = malloc(MAX_SEED_BYTES);
    if (!buf) { fclose(f); return; }
    size_t n = fread(buf, 1, MAX_SEED_BYTES, f);
    fclose(f);
    seeds[seed_count] = buf;
    seed_lens[seed_count] = n;
    seed_count++;
}

static void scan_dir(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char path[4096];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        const char *dot = strrchr(e->d_name, '.');
        if (ext && (!dot || strcmp(dot, ext) != 0)) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) add_seed(path);
    }
    closedir(d);
}

/* Random byte-level mutations: flips, splices, truncation, injection. */
static size_t mutate(const uint8_t *in, size_t in_len, uint8_t *out, size_t cap) {
    size_t len = in_len;
    if (len > cap) len = cap;
    memcpy(out, in, len);

    int rounds = (int)(rnd() % 12) + 1;
    for (int i = 0; i < rounds; i++) {
        if (len == 0) break;
        switch (rnd() % 7) {
        case 0: out[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: out[rnd() % len] = (uint8_t)(rnd() & 0xFF); break;
        case 2: len = (size_t)(rnd() % len) + 1; break;   /* truncate */
        case 3: {                                          /* inject brace/bracket */
            static const char toks[] = "{}[]()\"'#,;\\\n\t/*";
            out[rnd() % len] = (uint8_t)toks[rnd() % (sizeof(toks) - 1)];
            break;
        }
        case 4: {                                          /* duplicate a chunk */
            size_t off = rnd() % len, n = rnd() % 64 + 1;
            if (off + n > len) n = len - off;
            if (len + n < cap) { memmove(out + off + n, out + off, len - off); memcpy(out + off, out + off, n); len += n; }
            break;
        }
        case 5: {                                          /* delete a chunk */
            size_t off = rnd() % len, n = rnd() % 32 + 1;
            if (off + n > len) n = len - off;
            memmove(out + off, out + off + n, len - off - n);
            len -= n;
            break;
        }
        case 6: {                                          /* splice another seed */
            if (seed_count > 1) {
                int s = (int)(rnd() % (uint64_t)seed_count);
                size_t n = seed_lens[s] < 256 ? seed_lens[s] : 256;
                if (n && len + n < cap) { memcpy(out + len, seeds[s], n); len += n; }
            }
            break;
        }
        }
    }
    return len;
}

int main(int argc, char **argv) {
    long iters = (argc > 1) ? atol(argv[1]) : 20000;
    for (int i = 2; i < argc; i++) scan_dir(argv[i], NULL);

    if (seed_count == 0) {
        static const uint8_t s1[] = "default { path /tmp/a.png\n mode fill\n}\n";
        static const uint8_t s2[] = "void mainImage(out vec4 c, in vec2 u){ c=vec4(1.0); }\n";
        seeds[seed_count] = malloc(sizeof s1); memcpy(seeds[seed_count], s1, sizeof s1); seed_lens[seed_count++] = sizeof s1 - 1;
        seeds[seed_count] = malloc(sizeof s2); memcpy(seeds[seed_count], s2, sizeof s2); seed_lens[seed_count++] = sizeof s2 - 1;
    }
    fprintf(stderr, "[driver] %d seeds, %ld iterations\n", seed_count, iters);

    size_t cap = MAX_SEED_BYTES * 2;
    uint8_t *buf = malloc(cap);
    if (!buf) return 1;

    /* Pass 1: every seed verbatim. */
    for (int i = 0; i < seed_count; i++)
        LLVMFuzzerTestOneInput(seeds[i], seed_lens[i]);

    /* Pass 2: mutations. */
    for (long i = 0; i < iters; i++) {
        int s = (int)(rnd() % (uint64_t)seed_count);
        size_t n = mutate(seeds[s], seed_lens[s], buf, cap);
        LLVMFuzzerTestOneInput(buf, n);
        if ((i % 5000) == 0) fprintf(stderr, "[driver] iter %ld\n", i);
    }
    fprintf(stderr, "[driver] done, no crashes\n");
    return 0;
}
