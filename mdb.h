#ifndef MDB_H
#define MDB_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <uuid/uuid.h>


typedef struct mdb      mdb;
typedef struct mdb_iter mdb_iter;
typedef const char *    cstr;

#ifdef MDB_C
typedef datum mdb_k;
#else
typedef struct mdb_k {
    char  *dptr;
    int    dsize;
} mdb_k;
#endif


bool mdb_create  (mdb **_o, char *opts[]);
void mdb_destroy (mdb  *_o);
bool mdb_open    (mdb  *_o, cstr _t, cstr _mode);
bool mdb_open_f  (mdb  *_o, cstr _t, cstr _mode, cstr _fmt, ...);

bool mdb_insert    (mdb *_o, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_replace   (mdb *_o, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_search    (mdb *_o, cstr _t, mdb_k _id, void      **_d, size_t *_dsz, bool *_opt_exists);
bool mdb_search_cp (mdb *_o, cstr _t, mdb_k _id, void       *_d, size_t  _dsz, bool *_opt_exists);
bool mdb_delete    (mdb *_o, cstr _t, mdb_k _id);

bool mdb_iter_create  (mdb *_o, cstr _t, bool _filter_auth, mdb_iter **_iter);
bool mdb_iter_destroy (mdb_iter **_iter);
bool mdb_iter_loop    (mdb_iter  *_iter, mdb_k *_key);

bool mdb_authorized(mdb  *_o, cstr _t, mdb_k _id, cstr _opt_user);
bool mdb_set_owner (mdb  *_o, cstr _t, mdb_k _id, cstr _opt_user);

mdb_k mdb_k_uuid     (const uuid_t _uuid);
mdb_k mdb_k_uuid_new (uuid_t _uuid);
mdb_k mdb_k_uuid_str (const char *_s, uuid_t _uuid);
mdb_k mdb_k_str      (const char *_s);

bool  uuid_parse_nn(const char *_s, uuid_t _uuid);
void  mdb_k_print (mdb_k _id, FILE *_fp1);

#ifndef UUID_STORE
#  define UUID_STORE alloca(sizeof(uuid_t))
#endif
/*
 * Modes:
 * w : Writable.
 * o : Open owner file.
 * r : Readable.
 * s : Synchronized operations.
 *
 * For WR check `sys/authorization.h`.
 */

#endif

