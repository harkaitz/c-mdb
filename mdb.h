#ifndef MDB_H
#define MDB_H

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <uuid/uuid.h>


typedef struct mdb      mdb;
typedef struct mdb_iter mdb_iter;
typedef struct mdb_leg  mdb_leg;
typedef const char *    cstr;

#ifdef MDB_C
typedef datum mdb_k;
#else
typedef struct mdb_k {
    char  *dptr;
    int    dsize;
} mdb_k;
#endif

bool mdb_create  (mdb **_db, cstr opts[]);
void mdb_destroy (mdb  *_db);

bool mdb_open    (mdb  *_db, cstr _t, cstr _mode/*rws*/, unsigned _chmod);
bool mdb_open_f  (mdb  *_db, cstr _t, cstr _mode, unsigned _chmod, cstr _fmt, ...);
void mdb_close   (mdb  *_db, cstr _t);

bool mdb_insert    (mdb *_db, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_replace   (mdb *_db, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
bool mdb_search    (mdb *_db, cstr _t, mdb_k _id, void      **_d, size_t *_dsz, bool *_opt_exists);
bool mdb_search_cp (mdb *_db, cstr _t, mdb_k _id, void       *_d, size_t  _dsz, bool *_opt_exists);
bool mdb_delete    (mdb *_db, cstr _t, mdb_k _id);

bool mdb_iter_create  (mdb *_db, cstr _t, mdb_iter **_iter);
bool mdb_iter_destroy (mdb_iter **_iter);
bool mdb_iter_loop    (mdb_iter  *_iter, mdb_k *_key);

bool mdb_auth_insert_owner   (mdb *_db, cstr _t, mdb_k _key);

bool mdb_auth_check_owner  (mdb *_db, cstr _t, mdb_k _key);
bool mdb_auth_delete       (mdb *_db, cstr _t, mdb_k _key);
bool mdb_auth_iter_create  (mdb *_db, cstr _t, mdb_iter **_iter);

mdb_k mdb_k_uuid     (const uuid_t _uuid);
mdb_k mdb_k_uuid_new (uuid_t _uuid);
mdb_k mdb_k_uuid_str (cstr _s, uuid_t _uuid);
mdb_k mdb_k_str      (cstr _s);
void  mdb_k_print    (mdb_k _id, FILE *_fp1);

void     mdb_leg_close  (mdb_leg *_leg);
bool     mdb_leg_open   (mdb_leg *_leg, cstr _filename, cstr _t, cstr _mode, unsigned _chmod);
bool     mdb_leg_match  (mdb_leg *_leg, cstr _opt_t, cstr _opt_mode);
mdb_leg *mdb_leg_search (mdb     *_db, cstr _opt_t, cstr _opt_mode);

#endif

