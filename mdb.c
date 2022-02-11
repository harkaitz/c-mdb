#define MDB_C
#include <gdbm.h>
#include "mdb.h"
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <sys/authorization.h>
#include <sys/stat.h>
#include <types/str.h>
#include <stdarg.h>


#ifndef MDB_DEFAULT_DIRECTORY
#  define MDB_DEFAULT_DIRECTORY "/var/lib/mdb"
#endif
#ifndef MDB_MODE_DIR
#  define MDB_MODE_DIR 0777
#endif
#ifndef MDB_MODE_FILE
#  define MDB_MODE_FILE 0666
#endif

typedef struct mdb_leg {
    GDBM_FILE   gdbm_data;
    GDBM_FILE   gdbm_owner;
    const char *datatype;
    const char *mode;
} mdb_leg;

struct mdb {
    const char *db_dir;
    mdb_leg     legs[32];
    size_t      legsz;
    bool        verbose;
};

typedef struct mdb_iter {
    mdb_leg *leg;
    bool     not_first;
    mdb_k    key;
    bool     success;
    bool     filter_auth;
} mdb_iter;


    



/* --------------------------------------------------------------------------
 * ---- INITIALIZATION AND CLEANUP ------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_create(mdb **_o, char *_opts[]) {
    mdb        *o   = NULL;
    char      **opt = NULL;
    struct stat s   = {0};
    int         res = 0;
    /* Allocate memory. */
    o = calloc(1, sizeof(struct mdb));
    if (!o/*err*/) goto cleanup_errno;
    /* Get database directory. */
    o->db_dir = MDB_DEFAULT_DIRECTORY;
    for (opt = _opts; _opts && !*opt; opt+=2) {
        if (!strcasecmp(*opt, "mdb_directory")) {
            o->db_dir = *(opt+1);
        }
    }
    /* Check whether it exists. If not try to create it. */
    res = stat(o->db_dir, &s);
    if (res==-1) {
        if (errno != ENOENT/*err*/) goto cleanup_errno;
        res = mkdir(o->db_dir, MDB_MODE_DIR);
        if (res==-1/*err*/) goto cleanup_errno_mkdir;
    }
    /* It exists, but not a directory. */
    if (!S_ISDIR(s.st_mode)/*err*/) goto cleanup_errno_notdir;
    res = access(o->db_dir, R_OK|X_OK);
    /* No read access. */
    if (res==-1/*err*/) goto cleanup_errno_no_read_access;
    *_o = o;
    return true;
 cleanup_errno:
    syslog(LOG_ERR, "%s", strerror(errno));
    goto cleanup;
 cleanup_errno_mkdir:
    syslog(LOG_ERR, "Can't create %s: %s", o->db_dir, strerror(errno));
    goto cleanup;
 cleanup_errno_notdir:
    syslog(LOG_ERR, "%s not a directory.", o->db_dir);
    goto cleanup;
 cleanup_errno_no_read_access:
    syslog(LOG_ERR, "Can't read %s.", o->db_dir);
    goto cleanup;
 cleanup:
    if (o) free(o);
    return false;
}

void mdb_destroy(mdb *_o) {
    if (_o) {
        for (int i=0; i<_o->legsz; i++) {
            if (_o->legs[i].gdbm_data) {
                gdbm_close(_o->legs[i].gdbm_data);
            }
            if (_o->legs[i].gdbm_owner) {
                gdbm_close(_o->legs[i].gdbm_owner);
            }
        }
        free(_o);
    }
}

bool mdb_open(mdb  *_o, const char _t[], const char _mode[]) {
    return mdb_open_f(_o, _t, _mode, NULL);
}

bool mdb_open_f(mdb  *_o, const char _t[], const char _mode[], const char *_fmt, ...) {
    strpath filename1 = {0};
    strpath filename2 = {0};
    int l; va_list va;
    mdb_leg *leg = NULL;

    /* Calculate filename prefix. */
    l = snprintf(filename1, sizeof(filename1), "%s/%s%s", _o->db_dir, _t, (_fmt)?".":"");
    if (l >= sizeof(filename1) /*err*/) goto cleanup_name_too_long;
    if (_fmt) {
        va_start(va, _fmt);
        l += vsnprintf(filename1+l, sizeof(filename1)-l, _fmt, va);
        va_end(va);
        if (l >= sizeof(filename1)/*err*/) goto cleanup_name_too_long;
    }
    /* Copy and set suffix. */
    memcpy(filename2, filename1, l);
    l += snprintf(filename1+l, sizeof(filename1)-l, ".data");
    l += snprintf(filename2+l, sizeof(filename2)-l, ".owner");

    /* Calculate flags. */
    int flags = 0;
    if (strchr(_mode, 'w')) {
        flags |= GDBM_WRCREAT;
    } else {
        flags |= GDBM_READER;
    }
    if (strchr(_mode, 's')) {
        flags |= GDBM_SYNC;
    }
    
    /* Default mode. */
    int mode = MDB_MODE_FILE;
    
    /* Open files. */
    leg = &_o->legs[_o->legsz];
    leg->gdbm_data  = gdbm_open(filename1, 0, flags, mode, NULL);
    if (!leg->gdbm_data/*err*/) goto cleanup_gdbm_error;
    if (strchr(_mode, 'o')) {
        leg->gdbm_owner = gdbm_open(filename2, 0, flags, mode, NULL);
        if (!leg->gdbm_owner/*err*/) goto cleanup_gdbm_error;
    }
    leg->datatype = _t;
    leg->mode     = _mode;
    _o->legsz++;
    
    /* Return success. */
    if (_o->verbose) {
        if (leg->gdbm_data) {
            syslog(LOG_INFO, "mdb: Opened %s", filename1);
        }
        if (leg->gdbm_owner) {
            syslog(LOG_INFO, "mdb: Opened %s", filename2);
        }
    }
    return true;
 cleanup_name_too_long:
    syslog(LOG_ERR, "mdb: mdb_open_f(%s): Path too long.", _t);
    goto cleanup;
 cleanup_gdbm_error:
    syslog(LOG_ERR, "mdb: %s", gdbm_strerror(gdbm_errno));
    goto cleanup;
 cleanup:
    if (leg) {
        if (leg->gdbm_data)  gdbm_close(leg->gdbm_data);
        if (leg->gdbm_owner) gdbm_close(leg->gdbm_owner);
        leg->gdbm_data  = NULL;
        leg->gdbm_owner = NULL;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * ---- OPERATIONS ----------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_get_leg(mdb *_o, const char _t[], bool _write, mdb_leg **_out) {
    for (int i=0; i<_o->legsz; i++) {
        if (strcmp(_t, _o->legs[i].datatype)) continue;
        if (_write && (!strchr(_o->legs[i].mode,'w'))) {
            syslog(LOG_ERR, "mdb: Datatype %s not openned for writting.", _t);
            return false;
        }
        *_out  = &_o->legs[i];
        return true;
    }
    syslog(LOG_ERR, "mdb: Datatype %s not openned.", _t);
    return false;
}

bool mdb_insert(mdb *_o, const char _t[], mdb_k _id, const void *_d, size_t _dsz) {
    mdb_leg *leg;
    if (!mdb_get_leg(_o, _t, true, &leg)) return false;
    datum d = {.dptr = (void*)_d, .dsize = _dsz };
    int r = gdbm_store(leg->gdbm_data, _id, d, GDBM_INSERT);
    if (r==-1/*err*/) goto fail_gdbm;
    if (r==+1/*err*/) goto fail_exists;
    return true;
 fail_gdbm:
    syslog(LOG_ERR, "mdb: mdb_insert: %s", gdbm_strerror(gdbm_errno));
    return false;
 fail_exists:
    syslog(LOG_ERR, "mdb: mdb_insert: Already exists.");
    return false;
}

bool mdb_replace(mdb *_o, const char _t[], mdb_k _id, const void *_d, size_t _dsz) {
    mdb_leg *leg;
    if (!mdb_get_leg(_o, _t, true, &leg)) return false;
    datum d = {.dptr = (void*)_d, .dsize = _dsz };
    int r = gdbm_store(leg->gdbm_data, _id, d, GDBM_REPLACE);
    if (r==-1/*err*/) goto fail_gdbm;
    return true;
 fail_gdbm:
    syslog(LOG_ERR, "mdb: mdb_replace: %s", gdbm_strerror(gdbm_errno));
    return false;
}

bool mdb_search(mdb *_o, const char _t[], mdb_k _id, void **_d, size_t *_dsz, bool *_opt_exists) {
    mdb_leg *leg;
    if (!mdb_get_leg(_o, _t, false, &leg)) return false;
    datum d = gdbm_fetch(leg->gdbm_data, _id);
    if (!d.dptr/*err*/) goto fail_fetch;
    if (_d) {
        *_d = d.dptr;
    } else {
        free(d.dptr);
    }
    if (_dsz) {
        *_dsz = d.dsize;
    }
    if (_opt_exists) {
        *_opt_exists = true;
    }
    return true;
 fail_fetch:
    if (_d)   *_d   = NULL;
    if (_dsz) *_dsz = 0;
    if (_opt_exists) {
        *_opt_exists = false;
        return (gdbm_errno == GDBM_ITEM_NOT_FOUND)?true:false;
    }
    syslog(LOG_ERR, "mdb: mdb_search: %i, %s", _id.dsize, gdbm_strerror(gdbm_errno));
    return false;
}

bool mdb_search_cp(mdb *_o, const char _t[], mdb_k _id, void *_d, size_t _dsz, bool *_opt_exists) {
    void  *d   = NULL;
    size_t dsz = 0;
    bool   ret = false;
    if (!mdb_search(_o, _t, _id, &d, &dsz, _opt_exists)) return false;
    if (d) {
        if (_dsz < dsz/*err*/) goto cleanup_invalid_size;
        memcpy(_d, d, dsz);
    }
    ret = true;
    goto cleanup;
 cleanup_invalid_size:
    syslog(LOG_ERR, "Got invalid sized data.");
    goto cleanup;
 cleanup:
    if (d) free(d);
    return ret;
}

bool mdb_delete(mdb *_o, const char _t[], mdb_k _id) {
    mdb_leg *leg;
    if (!mdb_get_leg(_o, _t, true, &leg)) return false;
    gdbm_delete(leg->gdbm_data, _id);
    return true;
}

/* --------------------------------------------------------------------------
 * ---- LOOPS ---------------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_iter_create(mdb *_o, const char _t[], bool _filter_auth, mdb_iter **_iter) {
    mdb_leg *leg = NULL;
    mdb_iter_destroy(_iter);
    if (!mdb_get_leg(_o, _t, false, &leg)) return false;
    if (_filter_auth && leg->gdbm_owner && !authorization_get_username()) {
        syslog(LOG_ERR, "Not logged in");
        return false;
    }
    *_iter = calloc(1, sizeof(mdb_leg));
    if (!*_iter) {
        syslog(LOG_ERR, "%s", strerror(errno));
        return false;
    }
    (*_iter)->leg       = leg;
    (*_iter)->not_first = false;
    (*_iter)->key.dptr  = NULL;
    (*_iter)->key.dsize = 0;
    (*_iter)->success   = true;
    (*_iter)->filter_auth = _filter_auth;
    return true;
}

bool mdb_iter_destroy(mdb_iter **_iter) {
    bool res = true;
    if (*_iter) {
        res = (*_iter)->success;
        free((*_iter)->key.dptr);
        free((*_iter));
        *_iter = NULL;
    }
    return res;
}

bool mdb_iter_loop(mdb_iter *_iter, mdb_k *_key) {
    datum d;
    if (!_iter->not_first) {
        d = gdbm_firstkey(_iter->leg->gdbm_data);
        _iter->not_first = true;
    } else {
        d = gdbm_nextkey(_iter->leg->gdbm_data, _iter->key);
        free(_iter->key.dptr);
    }
    _iter->key = d;
    if (d.dptr) {
        *_key = _iter->key;
        _iter->success = true;
        if (_iter->filter_auth && _iter->leg->gdbm_owner) {
            const char *user = authorization_get_username();
            if (!user) return mdb_iter_loop(_iter, _key);
            datum owner = gdbm_fetch(_iter->leg->gdbm_owner, _iter->key);
            if (!owner.dptr) return mdb_iter_loop(_iter, _key);
            if (strcmp(user, owner.dptr)) {
                free(owner.dptr);
                return mdb_iter_loop(_iter, _key);
            }
        }
        return true;
    } else if (gdbm_errno == GDBM_ITEM_NOT_FOUND) {
        _iter->success = true;
        return false;
    } else {
        syslog(LOG_ERR, "%s", gdbm_strerror(gdbm_errno));
        _iter->success = false;
        return false;
    }
}

/* --------------------------------------------------------------------------
 * ---- AUTHORIZATION -------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_authorized(mdb *_o, const char _t[], mdb_k _id, const char *_opt_user) {
    mdb_leg    *leg    = NULL;
    bool        retval = false;
    datum       d      = {NULL,0};
    int         res    = 0;
    const char *user   = NULL;
    res = mdb_get_leg(_o, _t, false, &leg);
    if (!res/*err*/) goto cleanup;
    if (!leg->gdbm_owner) { /* Not openned with 'o' */
        retval = true;
        goto cleanup;
    }
    if (_opt_user) {
        user = _opt_user;
    } else {
        user = authorization_get_username();
        if (!user/*err*/) goto cleanup_not_logged_in;
    }
    d = gdbm_fetch(leg->gdbm_owner, _id);
    if (!d.dptr/*err*/) goto cleanup_cant_get_ownership;
    res = !strcmp(d.dptr, user);
    if (!res/*err*/) goto cleanup_not_authorized;
    retval = true;
    goto cleanup;
 cleanup_not_logged_in:
    syslog(LOG_ERR, "Not logged in");
    goto cleanup;
 cleanup_cant_get_ownership:
    syslog(LOG_ERR, "Can't get ownership. [1]");
    goto cleanup;
 cleanup_not_authorized:
    syslog(LOG_ERR, "Not authorized.");
    goto cleanup;
 cleanup:
    if (d.dptr) free(d.dptr);
    return retval;
}

bool mdb_set_owner (mdb *_o, const char _t[], mdb_k _id, const char *_opt_user) {
    mdb_leg    *leg    = NULL;
    const char *user   = NULL;
    int         res    = 0;
    if (_opt_user) {
        user = _opt_user;
    } else {
        user = authorization_get_username();
        if (!user/*err*/) goto cleanup_not_logged_in;
    }
    res = mdb_get_leg(_o, _t, false, &leg);
    if (!res/*err*/) goto cleanup;
    if (!leg->gdbm_owner/*err*/) goto cleanup_cant_set_ownership;
    res = gdbm_store(leg->gdbm_owner, _id, mdb_k_str(user), GDBM_REPLACE);
    if (res==-1/*err*/) goto cleanup_cant_set_ownership;
    return true;
 cleanup_not_logged_in:
    syslog(LOG_ERR, "Not logged in");
    return false;
 cleanup_cant_set_ownership:
    syslog(LOG_ERR, "Can't set ownership.");
    return false;
 cleanup:
    return false;
}


/* --------------------------------------------------------------------------
 * ---- KEY CREATION --------------------------------------------------------
 * -------------------------------------------------------------------------- */

mdb_k mdb_k_uuid (const uuid_t _uuid) {
    mdb_k k;
    if (_uuid && !uuid_is_null(_uuid)) {
        k.dptr  = (char*)_uuid;
        k.dsize = sizeof(uuid_t);
    } else {
        k.dptr = NULL;
        k.dsize = 0;
    }
    return k;
}

mdb_k mdb_k_uuid_new (uuid_t _uuid) {
    uuid_generate_random(_uuid);
    return mdb_k_uuid(_uuid);
}

mdb_k mdb_k_str (const char *_s) {
    mdb_k k = {(char*)_s, strlen(_s)+1};
    return k;
}

mdb_k mdb_k_uuid_str (const char *_s, uuid_t _uuid) {
    if (uuid_parse(_s, _uuid)!=-1) {
        return mdb_k_uuid(_uuid);
    } else {
        mdb_k k = {0};
        syslog(LOG_ERR, "Invalid UUID: %s", _s);
        return k;
    }
}

bool uuid_parse_nn(const char *_s, uuid_t _uuid) {
    if (uuid_parse(_s, _uuid)==-1) {
        syslog(LOG_ERR, "Invalid UUID: %s", _s);
        return false;
    } else if (uuid_is_null(_uuid)) {
        syslog(LOG_ERR, "NUll UUID: %s", _s);
        return false;
    } else {
        return true;
    }
}

void mdb_k_print (mdb_k _id, FILE *_fp1) {
    if (_id.dsize >= 1 && _id.dptr[_id.dsize-1]=='\0') {
        fwrite(_id.dptr, 1, _id.dsize-1, _fp1);
    } else {
        fwrite(_id.dptr, 1, _id.dsize, _fp1);
    }
}
