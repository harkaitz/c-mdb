# NAME

MDB - Minimalist database manager.

# SYNOPSIS

    #include <mdb.h>
    
    typedef struct mdb      mdb;
    typedef struct mdb_iter mdb_iter;
    typedef const char *    cstr;
    
    /* Initialization and cleanup. */
    
    bool mdb_create   (mdb **_o, char *opts[]);
    void mdb_destroy  (mdb  *_o);
    bool mdb_open[_f] (mdb  *_o, cstr _t, cstr _mode [, cstr _fmt, ...]);
    
    /* Database access. */
    
    bool mdb_insert    (mdb *_o, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
    bool mdb_replace   (mdb *_o, cstr _t, mdb_k _id, const void *_d, size_t  _dsz);
    bool mdb_search    (mdb *_o, cstr _t, mdb_k _id, void      **_d, size_t *_dsz, bool *_opt_exists);
    bool mdb_search_cp (mdb *_o, cstr _t, mdb_k _id, void       *_d, size_t  _dsz, bool *_opt_exists);
    bool mdb_delete    (mdb *_o, cstr _t, mdb_k _id);
    
    /* Iteration. */
    
    bool mdb_iter_create  (mdb *_o, cstr _t, bool _filter_auth, mdb_iter **_iter);
    bool mdb_iter_destroy (mdb_iter **_iter);
    bool mdb_iter_loop    (mdb_iter  *_iter, mdb_k *_key);
    
    /* Authorization. */
    
    bool mdb_authorized (mdb  *_o, cstr _t, mdb_k _id, cstr _opt_user);
    bool mdb_set_owner  (mdb  *_o, cstr _t, mdb_k _id, cstr _opt_user);
    
    /* Key generation. */
    
    mdb_k mdb_k_uuid     (const uuid_t _uuid);
    mdb_k mdb_k_uuid_new (uuid_t _uuid);
    mdb_k mdb_k_uuid_str (const char *_s, uuid_t _uuid);
    mdb_k mdb_k_str      (const char *_s);
    
    /* Auxiliary. */
    bool  uuid_parse_nn(const char *_s, uuid_t _uuid);
    void  mdb_k_print (mdb_k _id, FILE *_fp1);
    
    

# DESCRIPTION

This is the smallest and simplest database library with *sys_authorization(3)* support. It
uses *gdbm(3)* for it's backend.

# RETURN VALUE

All this functions return true on success false on error.

# COLLABORATING

For making bug reports, feature requests and donations visit
one of the following links:

1. [gemini://harkadev.com/oss/](gemini://harkadev.com/oss/)
2. [https://harkadev.com/oss/](https://harkadev.com/oss/)

# SEE ALSO

**GDBM(3)**, **UUID(3)**
