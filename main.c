#include "mdb.h"
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <io/fcopy.h>
#include <sys/authorization.h>

#define COPYRIGHT_LINE ""

const char help[] =
    "Usage: %s [-v][-a USER][-t TYPE][-m MODE] -i|r|s|d ID ..."  "\n"
    ""                                                       "\n"
    "    -i ID TEXT   : Insert data. You can use `uuidgen`." "\n"
    "    -r ID TEXT   : Replace data."                       "\n"
    "    -s ID        : Get data."                           "\n"
    "    -d ID        : Delete data."                        "\n"
    "    -l           : List IDs."                           "\n"
    "    -c ID        : Check whether it is owned by user."  "\n"
    "    -o ID        : Set ownershop of ID."                "\n"
    ""                                                       "\n"
    COPYRIGHT_LINE;
    

int main (int _argc, char *_argv[]) {

    char     cmd      = '\0';
    int      opt      = 0;
    char    *id       = NULL;
    char    *datatype = "mdb_text1";
    char    *mode     = "rwos";
    int      retval   = 1;
    int      res      = 0;
    mdb     *db       = NULL;
    char    *m        = NULL;
    size_t   m_size   = 0;
    FILE    *m_fp     = NULL;
    void     *d        = NULL;
    size_t    d_size   = 0;
    bool      d_exists = false;
    mdb_iter *iter     = {0};
    mdb_k     key      = {0};
    
    /* Help and logging. */
    _argv[0] = basename(_argv[0]);
    if (_argc == 1 || !strcmp(_argv[1], "-h") || !strcmp(_argv[1], "--help")) {
        printf(help, _argv[0]);
        return 0;
    }
    openlog(_argv[0], LOG_PERROR, LOG_USER);
    
    /* Parse options. */
    while((opt = getopt (_argc, _argv, "a:t:m:i:r:s:d:lc:o:")) != -1) {
        switch (opt) {
        case 'a':
            res = authorization_open(optarg);
            if (!res/*err*/) goto cleanup;
            break;
        case 't':
            datatype = optarg;
            break;
        case 'm':
            mode = optarg;
            break;
        case 'i': case 'r': case 's': case 'd': case 'l': case 'c': case 'o':
            cmd = opt;
            id  = optarg;
            break;
        case '?':
        default:
            return 1;
        }
    }

    /* Check for required parameters. */
    if (!cmd/*err*/) goto cleanup_missing_params;

    /* Open database. */
    res = mdb_create(&db, NULL);
    if (!res/*err*/) goto cleanup;
    res = mdb_open(db, datatype, mode);
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
        res = mdb_insert(db, datatype, mdb_k_str(id), m, m_size);
        if (!res/*err*/) goto cleanup_errno;
        break;
    case 'r':
        res = mdb_replace(db, datatype, mdb_k_str(id), m, m_size);
        if (!res/*err*/) goto cleanup_errno;
        break;
    case 's':
        res = mdb_search(db, datatype, mdb_k_str(id), &d, &d_size, &d_exists);
        if (!res/*err*/) goto cleanup_errno;
        if (d_exists) {
            fwrite(d, 1, d_size, stdout);
            fputc('\n', stdout);
        }
        break;
    case 'd':
        res = mdb_delete(db, datatype, mdb_k_str(id));
        if (!res/*err*/) goto cleanup_errno;
        break;
    case 'l':
        res = mdb_iter_create(db, datatype,(authorization_get_username())?true:false, &iter);
        if (!res/*err*/) goto cleanup;
        while (mdb_iter_loop(iter, &key)) {
            mdb_k_print(key, stdout);
            fputc('\n', stdout);
        }
        res = mdb_iter_destroy(&iter);
        if (!res/*err*/) goto cleanup;
        break;
    case 'c':
        res = mdb_authorized(db, datatype, mdb_k_str(id), NULL);
        if (!res/*err*/) goto cleanup;
        break;
    case 'o':
        res = mdb_set_owner(db, datatype, mdb_k_str(id), NULL);
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
