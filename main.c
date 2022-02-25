#include "mdb.h"
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <io/fcopy.h>
#include <io/mconfig.h>
#include <sys/authorization.h>

#define NL "\n"
#define COPYRIGHT_LINE \
    "Bug reports, feature requests to gemini|https://harkadev.com/oss" "\n" \
    "Copyright (c) 2022 Harkaitz Agirre, harkaitz.aguirre@gmail.com" "\n" \
    ""

const char help[] =
    "Usage: %s [-v][-a USER][-t TYPE][-m MODE] -i|r|s|d ID ..."  NL
    ""                                                           NL
    "    -i ID TEXT   : Insert data. You can use `uuidgen`."     NL
    "    -r ID TEXT   : Replace data."                           NL
    "    -s ID        : Get data."                               NL
    "    -d ID        : Delete data."                            NL
    "    -l           : List data keys."                         NL
    "    -c           : Enable ownership."                       NL
    "    -L           : List owned."                             NL
    ""                                                           NL
    COPYRIGHT_LINE;
    

int main (int _argc, char *_argv[]) {

    char      cmd            = '\0';
    int       opt,res;
    char     *datatype       = "mdb-cli-1";
    char     *mode           = "rws";
    int       retval         = 1;
    mdb      *db             = NULL;
    char     *m              = NULL;
    size_t    m_size         = 0;
    FILE     *m_fp           = NULL;
    void     *d              = NULL;
    size_t    d_size         = 0;
    mdb_iter *iter           = {0};
    mdb_k     id_k           = {0};
    bool      ownership_p    = false;
    mconfig_t mconfig        = MCONFIG_INITIALIZER();
    
    /* Help and logging. */
    _argv[0] = basename(_argv[0]);
    if (_argc == 1 || !strcmp(_argv[1], "-h") || !strcmp(_argv[1], "--help")) {
        printf(help, _argv[0]);
        return 0;
    }
    openlog(_argv[0], LOG_PERROR, LOG_USER);
    
    /* Parse options. */
    while((opt = getopt (_argc, _argv, "va:t:m:i:r:s:d:lc")) != -1) {
        switch (opt) {
        case 'v': mconfig_add(&mconfig, "mdb_verbose", "true", NULL); break;
        case 'a': if (!authorization_open(optarg)) { goto cleanup; } break;
        case 't': datatype = optarg; break;
        case 'm': mode = optarg; break;
        case 'i': case 'r': case 's': case 'd': case 'l': cmd = opt; id_k = mdb_k_str(optarg); break;
        case 'c': ownership_p  = true; break;
        case '?':
        default:
            return 1;
        }
    }
    if (!cmd/*err*/) goto cleanup_missing_params;

    /* Open database. */
    res = mdb_create(&db, mconfig.v);
    if (!res/*err*/) goto cleanup;
    res = mdb_open(db, datatype, mode, 0666);
    if (!res/*err*/) goto cleanup;

    /* Get the data. */
    opt = optind;
    if (strchr("ir", cmd)) {
        if (opt < _argc) {
            m      = _argv[opt++];
            m_size = strlen(m); 
        } else {
            m_fp = open_memstream(&m, &m_size);
            if (!m_fp/*err*/) goto cleanup_errno;
            res = fcopy_fd(m_fp, 0, 0, 0)>=0 && fflush(m_fp)!=EOF;
            if (!res/*err*/) goto cleanup_errno;
        }
    }
    /* Perform operations. */
    switch (cmd) {
    case 'i':
        if (ownership_p) {
            res = mdb_auth_insert_owner(db, datatype, id_k);
            if (!res/*err*/) goto cleanup;
        }
        res = mdb_insert(db, datatype, id_k, m, m_size);
        if (!res/*err*/) goto cleanup;
        break;
    case 'r':
        if (ownership_p) {
            res = mdb_auth_check_owner(db, datatype, id_k);
            if (!res/*err*/) goto cleanup;
        }
        res = mdb_replace(db, datatype, id_k, m, m_size);
        if (!res/*err*/) goto cleanup;
        
        break;
    case 's':
        if (ownership_p) {
            res = mdb_auth_check_owner(db, datatype, id_k);
            if (!res/*err*/) goto cleanup;
        }
        res = mdb_search(db, datatype, id_k, &d, &d_size, NULL);
        if (!res/*err*/) goto cleanup;
        fwrite(d, 1, d_size, stdout);
        fputc('\n', stdout);
        break;
    case 'd':
        if (ownership_p) {
            res = mdb_auth_delete(db, datatype, id_k);
        } else {
            res = mdb_delete(db, datatype, id_k);
        }
        if (!res/*err*/) goto cleanup;
        break;
    case 'l':
        if (ownership_p) {
            res = mdb_auth_iter_create(db, datatype, &iter);
        } else {
            res = mdb_iter_create(db, datatype, &iter);
        }
        if (!res/*err*/) goto cleanup;
        while (mdb_iter_loop(iter, &id_k)) {
            mdb_k_print(id_k, stdout);
            fputc('\n', stdout);
        }
        res = mdb_iter_destroy(&iter);
        if (!res/*err*/) goto cleanup;
        break;
    }

    /* Cleanup. */
    retval = 0;
    goto cleanup;
 cleanup_missing_params:
    if (!cmd)      syslog(LOG_ERR, "Please specify a command: -i|r|s|d|l|c|o.");
    goto cleanup;
 cleanup_errno:
    syslog(LOG_ERR, "%s", strerror(errno));
    goto cleanup;
 cleanup:
    if (m_fp)      fclose(m_fp);
    if (m_fp && m) free(m);
    if (d)         free(d);
    if (db)        mdb_destroy(db);
    return retval;
}
