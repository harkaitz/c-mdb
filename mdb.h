#ifndef MDB_H
#define MDB_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <uuid/uuid.h>

typedef struct mdb      mdb;
typedef struct mdb_iter mdb_iter;
typedef struct mdb_k {
    char  *dptr;
    int    dsize;
} mdb_k;

bool mdb_create  (mdb **_db, char const *opts[]);
void mdb_destroy (mdb  *_db);

bool mdb_map (mdb  *_db, char const *_t, char const _fmt[], ...);

bool mdb_insert    (mdb *_db, char const _t[], mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_replace   (mdb *_db, char const _t[], mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_search    (mdb *_db, char const _t[], mdb_k _id, void      **_d, size_t *_dsz, bool *_opt_exists);
bool mdb_search_cp (mdb *_db, char const _t[], mdb_k _id, void       *_d, size_t  _dsz, bool *_opt_exists);
bool mdb_delete    (mdb *_db, char const _t[], mdb_k _id);

bool mdb_iter_create  (mdb *_db, char const _t[], mdb_iter **_iter);
void mdb_iter_destroy (mdb_iter **_iter);
bool mdb_iter_loop    (mdb_iter  *_iter, mdb_k *_key);

mdb_k mdb_k_uuid     (const uuid_t _uuid);
mdb_k mdb_k_uuid_new (uuid_t _uuid);
mdb_k mdb_k_uuid_str (char const _s[], uuid_t _uuid);
mdb_k mdb_k_str      (char const _s[]);
void  mdb_k_print    (mdb_k _id, FILE *_fp1);

/* Authorization support. */
__attribute__((weak))
bool mdb_insert_auth(mdb *_db, char const _t[], mdb_k _id, const void *_d, size_t  _dsz);
__attribute__((weak))
bool mdb_replace_auth(mdb *_db, char const _t[], mdb_k _id, const void *_d, size_t  _dsz);
__attribute__((weak))
bool mdb_search_auth(mdb *_db, char const _t[], mdb_k _id, void **_d, size_t *_dsz, bool *_opt_exists);
__attribute__((weak))
bool mdb_delete_auth(mdb *_db, char const _t[], mdb_k _id);


#endif

