#include "profile.h"

#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "files.h"

static const struct known {
    const char *dos;
    long long size;
    const char *sha256;
    int required;
} KNOWN[] = {
    {"DID.DAT", 12681671, "4ec6b35627b12dfe41b9da59d09816fdf105aebb61d662c80553b8241c85b085", 1},
    {"TFX.DAT", 833674, "1df51f7dacb6d6598830820566aed840ef56445a7b4e76a80ecb0da318123aa5", 1},
    {"START.EXE", 36288, "c5a60c7791c971a872aaed1e19ef2638aab0b21eb1e039907b8d64eb8f8980d6", 1},
    {"CONFIG.EXE", 341897, "b97c4f32a7cf7183a8978fbb0717f95cbb71fe803cf34362150e8fa020006504", 0},
    {"GUS.COM", 23376, "dc8abddfd7deee6e25f3e625708684209a408420c74e3ae1c403e0d4d47a5925", 0},
    {"MISSIONS\\MISS830.MIS", 696, "98d26d4b42af77a6cb58c952a96b7f132c585c3ac484e98fd9d1142f20374173", 0},
    {"Q2.COM", 14750, "24d626ce3a436296cd4cbfeb4123c8eb0b26765cb73f6678346512fb064edbbf", 0},
    {"README.TXT", 2539, "2a77149174235c2bd3962d4224c7d674354b2e0d11441e0a4a035b34abd96c96", 0},
    {"TFX.ADV", 258, "6482c2a00ceec44eec11ad908421c22744278fdd23a2ba5aed825d5804849a16", 0},
    {"TFX.BAT", 15, "5354baa2500f72e3f75383ce671a096756db5432b9d198381d0b9d84bed5f545", 0},
    {"TFX.MK1", 924, "b20e0124d3810827f77bf0648cd0926e80b2d87473c893db435b3a91fc1bb139", 0},
    {"TFXMK1.TXT", 330, "c153c249c0361ca37d089e4e0c528c1ba5bc0e6882cdcc6277c3b96d670dad89", 0},
    {"TUNES\\INTRO.ADL", 8639, "17c3c16fe2e84d2d5c660e7c598a48cb0f6c1b4655f9034f7bd25b09d8d8d92f", 0},
    {"TUNES\\INTRO.RLD", 11217, "f8ce712588cb2e44b4f3c1241e32c274067a804774edd5e979543c03a1bedf17", 0},
    {"TUNES\\INTRO.SCC", 8515, "31da35289ebf7b04ece0bc94cbcd650c148d8edbb7fb5e1f2bcf72b1ae79564d", 0},
    {"UPDATE.DOC", 1828, "433927b002809ce28dd9ebe4c6af24762fa8911a04b04fefb10248a270d3f571", 0},
    {"WAYBIN\\COL", 2, "7eb70257593da06f682a3ddda54a9d260d4fc514f645237f5ca74b08f8da61a6", 0},
    {"WAYTXT\\COL", 2, "7eb70257593da06f682a3ddda54a9d260d4fc514f645237f5ca74b08f8da61a6", 0},
};

int tfx_mac_sha256_file(const char *path, char hex[65]) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    static unsigned char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        CC_SHA256_Update(&c, buf, (CC_LONG)n);
    int ok = !ferror(f);
    fclose(f);
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(d, &c);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
        snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return ok;
}

static void fail(tfx_mac_profile *p, tfx_mac_profile_status s, const char *text) {
    if (p->status == TFX_MAC_BASELINE || s < p->status) {
        p->status = s;
        snprintf(p->message, sizeof p->message, "%s", text);
    }
}

int tfx_mac_profile_check(const char *root, tfx_mac_profile *p) {
    memset(p, 0, sizeof *p);
    struct stat st;
    if (stat(root, &st) || !S_ISDIR(st.st_mode)) {
        fail(p, TFX_MAC_NOT_FOLDER, "The selected item is not a folder that can be read.");
        return 0;
    }
    char path[TFX_MAC_PATH], text[1024];
    for (size_t i = 0; i < sizeof KNOWN / sizeof KNOWN[0]; i++) {
        const struct known *k = &KNOWN[i];
        if (!tfx_mac_resolve(root, k->dos, path, sizeof path, 0) || stat(path, &st) || !S_ISREG(st.st_mode)) {
            p->absent++;
            if (k->required && !strcmp(k->dos, "DID.DAT"))
                fail(p, TFX_MAC_NOT_GAME,
                     "This folder is not a TFX installation: DID.DAT is missing. Choose the folder TFX was installed "
                     "to, the one holding DID.DAT, TFX.DAT and START.EXE.");
            else if (k->required) {
                snprintf(text, sizeof text,
                         "The TFX installation is incomplete: %s is missing. Install the game again from the CD and "
                         "choose its folder.", k->dos);
                fail(p, TFX_MAC_INCOMPLETE, text);
            }
            continue;
        }
        char hex[65];
        if ((long long)st.st_size == k->size && !tfx_mac_sha256_file(path, hex)) {
            p->differ++;
            if (k->required) {
                snprintf(text, sizeof text, "%s cannot be read (check the folder's permissions).", k->dos);
                fail(p, TFX_MAC_UNREADABLE, text);
            }
            continue;
        }
        if ((long long)st.st_size != k->size || strcmp(hex, k->sha256)) {
            p->differ++;
            if (k->required) {
                snprintf(text, sizeof text,
                         "%s differs from the supported release (TFX, Ocean CD edition 1995: %lld bytes, SHA-256 "
                         "%.16s...; this file has %lld bytes). Other editions and modified files are not supported.",
                         k->dos, k->size, k->sha256, (long long)st.st_size);
                fail(p, TFX_MAC_UNSUPPORTED, text);
            }
            continue;
        }
        p->matched++;
    }
    if (p->status == TFX_MAC_BASELINE)
        snprintf(p->edition, sizeof p->edition, "TFX, Ocean CD edition (1995)");
    return p->status == TFX_MAC_BASELINE;
}
