# Security policy

## Supported versions

Security fixes go into the latest release.

## Reporting a vulnerability

Please don't open a public issue. Report it privately through GitHub's
[security advisory form](https://github.com/1ay1/neowall/security/advisories/new),
with the affected version, steps to reproduce, and the impact you expect.
You'll get a reply within a few days.

## What counts

neowall runs as your user and reads files you point it at: its config,
shaders, images, and Shadertoy exports you import. Problems worth reporting
include:

- memory-safety bugs in anything that parses those files (the config, shader
  manifests, PNG, JPEG and EXIF readers, the terminal emulator, the font
  reader), all of which are fuzzed in CI;
- a shader or manifest that can run commands or read files without
  `allow_exec` being enabled by the user;
- the control socket accepting commands from another user.
