#ifndef VERSION_H
#define VERSION_H

// FIRMWARE_VERSION is normally injected at build time by version.py (derived
// from the git tag). This fallback only applies to builds where that flag is
// missing (e.g. an IDE indexer or a checkout with no git history).
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

#endif
