#define MDB_C
#define SLOG_PREFIX "MDB: "
#include <gdbm.h>
#include "mdb.h"
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/authorization.h>
#include <sys/stat.h>
#include <types/bool_ss.h>
#include <str/sizes.h>
#include <str/strdupa.h>
#include <syslog.h>

#ifndef MDB_DEFAULT_DIRECTORY
#  define MDB_DEFAULT_DIRECTORY "/var/lib/mdb"
#endif
#ifndef MDB_MODE_DIR
#  define MDB_MODE_DIR 0777
#endif
#ifndef MDB_MAX_LEGS
#  define MDB_MAX_LEGS 32
#endif


typedef struct mdb_leg {
    GDBM_FILE   gdbm;
    str64       datatype;
    const char *mode;
    unsigned    perm;
} mdb_leg;

struct mdb {
    const char *db_dir;
    mdb_leg     legs[MDB_MAX_LEGS];
    size_t      legsz;
    long      (*get_limit) (mdb *_db, cstr _opt_user, cstr _t);
};

typedef struct mdb_iter {
    mdb_leg *leg;
    mdb_k    next;
    mdb_k    curr;
    int      errcode;
    bool     success;
} mdb_iter;

typedef struct mdb_auth {
    char reserved;
} mdb_auth;

/* --------------------------------------------------------------------------
 * ---- INITIALIZATION AND CLEANUP ------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_create(mdb **_mdb, cstr _opts[]) {
    mdb         *mdb = NULL;
    cstr        *opt = NULL;
    struct stat  s   = {0};
    int          e;
    
    mdb = calloc(1, sizeof(struct mdb));
    if (!mdb/*err*/) goto c_errno;

    
    mdb->db_dir = MDB_DEFAULT_DIRECTORY;
    if (_opts) {
        for (opt = _opts; *opt; opt+=2) {
            if (*(opt+1) && !strcasecmp(*opt, "mdb_directory")) {
                mdb->db_dir = *(opt+1);
            }
        }
    }
    
    if (stat(mdb->db_dir, &s)==-1) {
        if (errno != ENOENT/*err*/) goto c_errno;
        e = mkdir(mdb->db_dir, MDB_MODE_DIR);
        if (e==-1/*err*/) goto c_errno_mkdir;
    }

    if (!S_ISDIR(s.st_mode)/*err*/) goto c_errno_notdir;
    e = access(mdb->db_dir, R_OK|X_OK);
    if (e==-1/*err*/) goto c_errno_no_read_access;
    
    *_mdb = mdb;
    syslog(LOG_DEBUG, "Database started.");
    return true;
 c_errno:                syslog(LOG_ERR, "%s", strerror(errno)); goto cleanup;
 c_errno_mkdir:          syslog(LOG_ERR, "%s: Can't create: %s", mdb->db_dir, strerror(errno)); goto cleanup;
 c_errno_notdir:         syslog(LOG_ERR, "%s: Not a directory.", mdb->db_dir); goto cleanup;
 c_errno_no_read_access: syslog(LOG_ERR, "%s: Can't read."     , mdb->db_dir); goto cleanup;
 cleanup:
    if (mdb) free(mdb);
    return false;
}

void mdb_destroy(mdb *_mdb) {
    if (_mdb) {
        for (int i=0; i<_mdb->legsz; i++) {
            if (_mdb->legs[i].gdbm) {
                syslog(LOG_DEBUG, "%s: Closing ...", _mdb->legs[i].datatype);
            }
            mdb_leg_close(&_mdb->legs[i]);
        }
        syslog(LOG_DEBUG, "All databases closed.");
        free(_mdb);
    }
}


/* --------------------------------------------------------------------------
 * ---- LEGS ----------------------------------------------------------------
 * -------------------------------------------------------------------------- */

void mdb_leg_close(mdb_leg *_leg) {
    if (_leg->gdbm) {
        //info("GDBM %p: Closing...", _leg);
        gdbm_close(_leg->gdbm);
    }
    memset(_leg, 0, sizeof(mdb_leg));
}

bool mdb_leg_open (mdb_leg *_leg, cstr _filename, cstr _t, cstr _mode, unsigned _chmod) {
    int         flags_i = 0;
    struct stat stat_s  = {0};
    if (strchr(_mode, 'w')) {
        flags_i |= GDBM_WRCREAT;
    } else if (stat(_filename, &stat_s)!=-1) {
        flags_i |= GDBM_READER;
    } else if (errno == ENOENT) {
        flags_i |= GDBM_WRCREAT;
    } else {
        syslog(LOG_ERR, "%s: %s", _t, strerror(errno));
        return false;
    }
    //if (strchr(_mode, 's')) {
    //    flags_i |= GDBM_SYNC;
    //}
    //info("GDBM %p: Openning %s", _leg, _filename);
    _leg->gdbm  = gdbm_open(_filename, 0, flags_i, _chmod, NULL);
    if (!_leg->gdbm) {
        syslog(LOG_ERR, "%s: %s", _t, gdbm_strerror(gdbm_errno));
        if (gdbm_check_syserr(gdbm_errno)) {
            syslog(LOG_ERR, "%s: %s", _t, strerror(errno));
        }
        mdb_leg_close(_leg);
        return false;
    }
    //info("GDBM %p: Openned %s", _leg, _filename);
    chmod(_filename, _chmod);
    strncpy(_leg->datatype, _t, sizeof(_leg->datatype)-1);
    _leg->mode     = _mode;
    _leg->perm     = _chmod;
    return true;
}

bool mdb_leg_match(mdb_leg *_leg, cstr _opt_t, cstr _opt_mode) {
    cstr c;
    if (!_opt_t && !_opt_mode) {
        if (_leg->gdbm) {
            return false;
        }
    } else {
        if (!_leg->gdbm) {
            return false;
        }
        if (_opt_t && strcmp(_leg->datatype, _opt_t)) {
            return false;
        }
        if (_opt_mode) {
            for (c = _opt_mode; *c; c++) {
                if (!strchr(_leg->mode, *c)) {
                    return false;
                }
            }
        }
    }
    return true;
}

mdb_leg *mdb_leg_search(mdb *_mdb, cstr _opt_t, cstr _opt_mode) {
    for (size_t l=0; l<((!_opt_t && !_opt_mode)?MDB_MAX_LEGS:_mdb->legsz); l++) {
        if (mdb_leg_match(&_mdb->legs[l], _opt_t, _opt_mode)) {
            if (l >= _mdb->legsz) _mdb->legsz = l+1;
            return &_mdb->legs[l];
        }
    }
    return NULL;
}


/* --------------------------------------------------------------------------
 * ---- OPEN TYPES ----------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_open(mdb *_mdb, cstr _t, cstr _mode, unsigned _chmod) {
    return mdb_open_f(_mdb, _t, _mode, _chmod, NULL);
}

bool mdb_open_f(mdb *_mdb, cstr _t, cstr _mode, unsigned _chmod, cstr _fmt, ...) {
    strpath  f    = {0};
    int      fsz  = 0;
    mdb_leg *leg  = NULL;
    
    /* Get a leg. */
    leg = mdb_leg_search(_mdb, _t, NULL);
    if (leg) {
        if (mdb_leg_match(leg, NULL, _mode)) {
            return true;
        } else {
            mdb_leg_close(leg);
        }
    } else {
        leg = mdb_leg_search(_mdb, NULL, NULL);
        if (!leg/*err*/) goto cleanup_limit_reached;
    }
    
    /* Calculate filename. */
    fsz += snprintf(f, sizeof(f), "%s/%s%s", _mdb->db_dir, _t, (_fmt)?".":"");
    if (fsz >= sizeof(f) /*err*/) goto cleanup_name_too_long;
    if (_fmt) {
        va_list va;
        va_start(va, _fmt);
        fsz += vsnprintf(f+fsz, sizeof(f)-fsz, _fmt, va);
        va_end(va);
        if (fsz >= sizeof(f)/*err*/) goto cleanup_name_too_long;
    }
    fsz += snprintf(f+fsz, sizeof(f)-fsz, ".data");
    if (fsz >= sizeof(f)/*err*/) goto cleanup_name_too_long;

    /* Open leg. */
    syslog(LOG_DEBUG, "%s: Openning %s ...", _t, f);
    return mdb_leg_open(leg, f, _t, _mode, _chmod);
    
    /* Failures. */
 cleanup_limit_reached:
    syslog(LOG_ERR, "%s: Too much tables openned.", _t);
    return false;
 cleanup_name_too_long:
    syslog(LOG_ERR, "%s: Name too long.", _t);
    return false;
}

void mdb_close(mdb *_mdb, cstr _t) {
    mdb_leg *leg = mdb_leg_search(_mdb, _t, "");
    if (leg) {
        mdb_leg_close(leg);
    }
}

/* --------------------------------------------------------------------------
 * ---- OPERATIONS ----------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_insert(mdb *_mdb, cstr _t, mdb_k _id, const void *_d, size_t _dsz) {
    mdb_leg *leg = mdb_leg_search(_mdb, _t, "w");
    if (!leg/*err*/) goto fail_not_openned;
    datum d = {.dptr = (void*)_d, .dsize = _dsz };
    int r = gdbm_store(leg->gdbm, _id, d, GDBM_INSERT);
    if (r==-1/*err*/) goto fail_gdbm;
    if (r==+1/*err*/) goto fail_exists;
    syslog(LOG_DEBUG, "%s: Inserted %li bytes.", _t, _dsz);
    return true;
    fail_gdbm:        syslog(LOG_ERR, "%s: Invalid key or data.", _t);   return false;
    fail_exists:      syslog(LOG_ERR, "%s: Object already exists.", _t); return false;
    fail_not_openned: syslog(LOG_ERR, "%s: Not openned.", _t);           return false;
}

bool mdb_replace(mdb *_db, cstr _t, mdb_k _id, const void *_d, size_t _dsz) {
    mdb_leg *leg = mdb_leg_search(_db, _t, "w");
    if (!leg/*err*/) goto fail_not_openned;
    datum d = {.dptr = (void*)_d, .dsize = _dsz };
    int r = gdbm_store(leg->gdbm, _id, d, GDBM_REPLACE);
    if (r==-1/*err*/) goto fail_gdbm;
    syslog(LOG_DEBUG, "%s: Replaced %li bytes.", _t, _dsz);
    return true;
    fail_gdbm:        syslog(LOG_ERR, "%s: Invalid key or data.", _t); return false;
    fail_not_openned: syslog(LOG_ERR, "%s: Not openned for writting.", _t); return false;
}

bool mdb_search(mdb *_db, cstr _t, mdb_k _id, void **_d, size_t *_dsz, bool *_opt_exists) {
    mdb_leg *leg = mdb_leg_search(_db, _t, "r");
    if (!leg/*err*/) goto fail_not_openned;
    datum d = gdbm_fetch(leg->gdbm, _id);
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
    syslog(LOG_DEBUG, "%s: Retrieved data", _t);
    return true;
 fail_fetch:
    if (_d)   *_d   = NULL;
    if (_dsz) *_dsz = 0;
    if (_opt_exists) {
        *_opt_exists = false;
        if (gdbm_errno == GDBM_ITEM_NOT_FOUND) {
            syslog(LOG_DEBUG, "%s: Field does not exist.", _t);
            return true;
        }
    }
    syslog(LOG_ERR, "%s: Can't read: %s", _t, gdbm_strerror(gdbm_errno));
    return false;
 fail_not_openned:
    syslog(LOG_ERR, "%s: Not openned for reading.", _t);
    return false;
}

bool mdb_search_cp(mdb *_db, cstr _t, mdb_k _id, void *_d, size_t _dsz, bool *_opt_exists) {
    void  *d   = NULL;
    size_t dsz = 0;
    bool   ret = false;
    if (!mdb_search(_db, _t, _id, &d, &dsz, _opt_exists)) return false;
    if (d) {
        if (_dsz < dsz/*err*/) goto cleanup_invalid_size;
        memcpy(_d, d, dsz);
        if (_dsz>dsz) {
            memset(_d+dsz, 0, _dsz-dsz);
        }
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

bool mdb_delete(mdb *_db, cstr _t, mdb_k _id) {
    mdb_leg *leg = mdb_leg_search(_db, _t, "w");
    if (!leg/*err*/) goto fail_not_openned;
    gdbm_delete(leg->gdbm, _id);
    syslog(LOG_DEBUG, "%s: Field deleted.", _t);
    return true;
    fail_not_openned: syslog(LOG_ERR, "%s: Not openned.", _t); return false;
}

/* --------------------------------------------------------------------------
 * ---- LOOPS ---------------------------------------------------------------
 * -------------------------------------------------------------------------- */

bool mdb_iter_create(mdb *_db, cstr _t, mdb_iter **_iter) {
    mdb_iter_destroy(_iter);
    mdb_leg *leg = mdb_leg_search(_db, _t, "r");
    if (!leg/*err*/) goto cleanup_not_openned;
    *_iter = calloc(1, sizeof(mdb_iter));
    if (!*_iter/*err*/) goto cleanup_errno;    
    (*_iter)->leg       = leg;
    (*_iter)->next      = gdbm_firstkey(leg->gdbm);
    (*_iter)->success   = true;
    if (!(*_iter)->next.dptr) {
        (*_iter)->errcode = gdbm_errno;
    }
    syslog(LOG_DEBUG, "%s: Iteration start.", _t);
    return true;
    cleanup_not_openned: syslog(LOG_ERR, "%s: Not openned.", _t);        return false;
    cleanup_errno:       syslog(LOG_ERR, "%s: %s", _t, strerror(errno)); return false;
}

bool mdb_iter_destroy(mdb_iter **_iter) {
    bool e = true;
    if (*_iter) {
        e = (*_iter)->success;
        free((*_iter)->next.dptr);
        free((*_iter)->curr.dptr);
        free((*_iter));
        *_iter = NULL;
    }
    return e;
}

bool mdb_iter_loop(mdb_iter *_iter, mdb_k *_key) {
    if (_iter->curr.dptr) {
        free(_iter->curr.dptr);
        _iter->curr.dptr = NULL;
    }
    if (!_iter->next.dptr) {
        if (_iter->errcode != GDBM_ITEM_NOT_FOUND) {
            syslog(LOG_ERR, "%s", gdbm_strerror(_iter->errcode));
            _iter->success = false;
        }
        return false;
    }
    _iter->curr = _iter->next;
    _iter->next = gdbm_nextkey(_iter->leg->gdbm, _iter->curr);
    if (!_iter->next.dptr) {
        _iter->errcode = gdbm_errno;
    }
    *_key = _iter->curr;
    return true;
}

/* --------------------------------------------------------------------------
 * ---- AUTHORIZATION -------------------------------------------------------
 * -------------------------------------------------------------------------- */

static
bool mdb_auth_init (mdb *_db, cstr _t) {
    int         res  = 0;
    const char *user = NULL;
    const char *type = strcata(_t,".auth");
    mdb_leg    *leg  = mdb_leg_search(_db, _t, /*mode:*/"");
    if (!leg/*err*/) goto fail_not_openned;
    user = authorization_get_username();
    if (!user/*err*/) goto fail_unauthenticated;
    res = mdb_open_f(_db, type, "rw", leg->perm, "%s", user);
    if (!res/*err*/) return false;
    return true;
    fail_unauthenticated: syslog(LOG_ERR, "Auth %s: Unauthenticated", _t); return false;
    fail_not_openned:     syslog(LOG_ERR, "Auth %s: Not openned.", _t);    return false;
}

bool mdb_auth_insert_owner (mdb *_db, cstr _t, mdb_k _key) {
    int         e;
    mdb_auth    info = {0};
    const char *type = strcata(_t,".auth");
    e = mdb_auth_init(_db, _t);
    if (!e/*err*/) return false;
    return mdb_insert(_db, type, _key, &info, sizeof(info));
}

bool mdb_auth_check_owner (mdb *_db, cstr _t, mdb_k _key) {
    int         res;
    const char *type = strcata(_t,".auth");
    res = mdb_auth_init(_db, _t);
    if (!res/*err*/) return false;
    res = mdb_search(_db, type, _key, NULL, NULL, NULL);
    if (!res/*err*/) return false;
    return true;
}

bool mdb_auth_delete (mdb *_db, cstr _t, mdb_k _key) {
    int e = mdb_auth_init(_db, _t);
    if (!e/*err*/) return false;
    mdb_leg *leg1 = mdb_leg_search(_db, _t, "w");
    mdb_leg *leg2 = mdb_leg_search(_db, strcata(_t,".auth"), "w");
    if (!leg1/*err*/) return false;
    if (!leg2/*err*/) goto fail_not_openned;
    if (gdbm_delete(leg2->gdbm, _key)==0) {
        gdbm_delete(leg1->gdbm, _key);
    }
    syslog(LOG_DEBUG, "%s: Deleted authenticated.", _t);
    return true;
    fail_not_openned: syslog(LOG_ERR, "%s: Not openned.", _t); return false;
}

bool mdb_auth_iter_create(mdb *_db, cstr _t, mdb_iter **_iter) {
    int e = 0;
    e = mdb_auth_init(_db, _t);
    if (!e/*err*/) return false;
    return mdb_iter_create(_db, strcata(_t,".auth"), _iter);
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

mdb_k mdb_k_str (cstr _s) {
    if (_s) {
        mdb_k k = {(char*)_s, strlen(_s)+1};
        return k;
    } else {
        mdb_k k = {NULL, 0};
        return k;
    }
}

mdb_k mdb_k_uuid_str (cstr _s, uuid_t _uuid) {
    if (uuid_parse(_s, _uuid)!=-1) {
        return mdb_k_uuid(_uuid);
    } else {
        mdb_k k = {0};
        syslog(LOG_ERR, "Invalid UUID: %s", _s);
        return k;
    }
}

void mdb_k_print (mdb_k _id, FILE *_fp1) {
    if (_id.dsize >= 1 && _id.dptr[_id.dsize-1]=='\0') {
        fwrite(_id.dptr, 1, _id.dsize-1, _fp1);
    } else {
        fwrite(_id.dptr, 1, _id.dsize, _fp1);
    }
}
