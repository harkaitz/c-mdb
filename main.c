#include "mdb.h"
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <io/fcopy.h>

#define COPYRIGHT_LINE \
    "Bug reports, feature requests to gemini|https://harkadev.com/oss" "\n" \
    "Copyright (c) 2022 Harkaitz Agirre, harkaitz.aguirre@gmail.com" "\n" \
    ""

int main (int _argc, char *_argv[]) {

    char      cmd            = '\0';
    char     *datatype       = "MDB1";
    int       retval         = 1;
    mdb      *db             = NULL;
    char     *m              = NULL;
    size_t    m_size         = 0;
    FILE     *m_fp           = NULL;
    void     *d              = NULL;
    size_t    d_size         = 0;
    mdb_iter *iter           = {0};
    mdb_k     id_k           = {0};
    uuid_t    u              = {0};
    int       opt,e;
    
    /* Help and logging. */
    _argv[0] = basename(_argv[0]);
    if (_argc == 1 || !strcmp(_argv[1], "-h") || !strcmp(_argv[1], "--help")) {
        printf("Usage: %s [-t TYPE] -i|r|g|d ID ..."                    "\n"
               ""                                                       "\n"
               "    -i ID|@ TEXT : Insert data. You can use `uuidgen`." "\n"
               "    -r ID   TEXT : Replace data."                       "\n"
               "    -g ID        : Get data."                           "\n"
               "    -d ID        : Delete data."                        "\n"
               "    -l           : List data keys."                     "\n"
               ""                                                       "\n"
               COPYRIGHT_LINE, _argv[0]);
        return 0;
    }
    openlog(_argv[0], LOG_PERROR, LOG_USER);
    
    /* Parse options. */
    while((opt = getopt (_argc, _argv, "t:" "i:r:g:d:l")) != -1) {
        switch (opt) {
        case 't':
            datatype = optarg;
            break;
        case 'i':
            cmd = opt;
            if (!strcmp(optarg, "@")) {
                id_k = mdb_k_uuid_new(u);
            } else {
                id_k = mdb_k_str(optarg);
            }
            break;
        case 'r':
        case 'g':
        case 'd':
            cmd = opt;
            id_k = mdb_k_str(optarg);
            break;
        case 'l':
            cmd = opt;
            break;
        case '?':
        default:
            return 1;
        }
    }
    if (!cmd/*err*/) goto cleanup_missing_params;

    /* Open database. */
    e = mdb_create(&db, NULL);
    if (!e/*err*/) goto cleanup;
    
    /* Get the data. */
    opt = optind;
    switch (cmd) {
    case 'i':
    case 'r':
        if (opt < _argc) {
            m      = _argv[opt++];
            m_size = strlen(m); 
        } else {
            m_fp = open_memstream(&m, &m_size);
            if (!m_fp/*err*/) goto cleanup_errno;
            e = fcopy_fd(m_fp, 0, 0, 0)>=0 && fflush(m_fp)!=EOF;
            if (!e/*err*/) goto cleanup_errno;
            fflush(m_fp);
        }
        break;
    }
    /* Perform operations. */
    switch (cmd) {
    case 'i':
        e = mdb_insert(db, datatype, id_k, m, m_size);
        if (!e/*err*/) goto cleanup;
        break;
    case 'r':
        e = mdb_replace(db, datatype, id_k, m, m_size);
        if (!e/*err*/) goto cleanup;
        break;
    case 'g':
        e = mdb_search(db, datatype, id_k, &d, &d_size, NULL);
        if (!e/*err*/) goto cleanup;
        fwrite(d, 1, d_size, stdout);
        break;
    case 'd':
        e = mdb_delete(db, datatype, id_k);
        if (!e/*err*/) goto cleanup;
        break;
    case 'l':
        e = mdb_iter_create(db, datatype, &iter);
        if (!e/*err*/) goto cleanup;
        while (mdb_iter_loop(iter, &id_k)) {
            mdb_k_print(id_k, stdout);
            fputc('\n', stdout);
        }
        mdb_iter_destroy(&iter);
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
