/* The user's data (M07): the files the game writes, in the store
 * (files.h), checked with the recovered formats, imported from and
 * exported to folders the user chooses, with the previous versions kept.
 * Portable C.
 *
 * - The files: TFX.CFG (EX10), TFX.HS (the scores, EX09), TFXSAV.<n> (a
 *   pilot: the flow script's state, EX12) and TFXSAV.<n>L (its temporary
 *   log), VIDEO.REC (the flight recording, EX12). Nothing else is the
 *   game's to write (R07: the levels' .sav are never written).
 * - A check is the game's own reading: TFX.CFG under the 1024 bytes the
 *   game reads (else it uses its built-in text); TFX.HS 674 bytes with its
 *   checksum (else the game restores its defaults and overwrites it); a
 *   pilot decoded with cd_demo.par's variables, every stack and resume
 *   node in the script (else the game stops with a fatal error loading
 *   it); VIDEO.REC as the player accepts it (frames, its size).
 * - Every store write (files.c tfx_mac_write_kept: the game's, the
 *   settings', an import's) keeps the version it replaces in the store's
 *   .history folder (TFX_MAC_KEPT versions, newest first; hidden from the
 *   game's searches); a failed write keeps the file as it was (written
 *   beside it, then renamed).
 * - Import reads the game's files from a folder (a copy of a DOS
 *   installation, say) and writes the ones that pass into the store, each
 *   replaced version kept; export copies the store's files to a folder,
 *   DOS-named, as the DOS game reads them. Neither writes the game folder. */
#ifndef TFX_MAC_USERDATA_H
#define TFX_MAC_USERDATA_H

#include <stddef.h>
#include <stdint.h>

#include "files.h"
#include "formats/pilot.h"

typedef enum tfx_mac_user_kind {
    TFX_MAC_USER_NONE = 0,
    TFX_MAC_USER_CFG,
    TFX_MAC_USER_HS,
    TFX_MAC_USER_PILOT,
    TFX_MAC_USER_LOG,
    TFX_MAC_USER_REC
} tfx_mac_user_kind;

/* which of the game's files the name is (case-insensitive), else NONE */
tfx_mac_user_kind tfx_mac_user_kind_of(const char *name);

/* the flow script the pilots belong to (cd_demo.par, from the game
 * folder's archive): its variables' layout and its flows' node counts */
typedef struct tfx_mac_user_flow {
    int loaded;
    tfx_pilot_layout layout;
    uint16_t flows;
    uint16_t nodes[64];
} tfx_mac_user_flow;

/* 1 loaded; 0 (the release's layout, no node check) when it cannot be */
int tfx_mac_user_flow_load(tfx_mac_user_flow *fl, const tfx_mac_files *f);

/* the game's reading of the file: 1 accepted, else 0 and why */
int tfx_mac_user_check(const tfx_mac_user_flow *fl, const char *name, const uint8_t *data, size_t len, char *why,
                       size_t cap);

/* the store's version k (1 the newest replaced) of name: 1 when there is one */
int tfx_mac_user_version(const tfx_mac_files *f, const char *name, int k, int64_t *when, uint32_t *size);
/* version k back as the file (the file it replaces kept, so this can be undone) */
int tfx_mac_user_restore(const tfx_mac_files *f, const char *name, int k);

/* one file met by an import or export: written, or why not */
typedef void (*tfx_mac_user_report)(void *ctx, const char *name, int written, const char *why);

/* the game's files in dir checked and written into the store; the number written */
int tfx_mac_user_import(const tfx_mac_files *f, const tfx_mac_user_flow *fl, const char *dir, tfx_mac_user_report report,
                        void *ctx);
/* the store's files copied into dir (DOS names); -1 when dir is the game
 * folder or the store, else the number written */
int tfx_mac_user_export(const tfx_mac_files *f, const char *dir, tfx_mac_user_report report, void *ctx);

#endif
