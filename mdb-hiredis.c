#define _GNU_SOURCE /* asprintf */
#include "mdb.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <syslog.h>
#include <hiredis/hiredis.h>

struct mdb_map {
    char  *type;
    char  *table;
};

struct mdb_iter {
    redisReply *reply;
    size_t      pos;
};

struct mdb {
    redisContext  *redis;
    struct mdb_map map[20];
    size_t         mapsz;
};

bool mdb_create (mdb **_mdb, char const *_opts[]) {

    mdb           *mdb             = NULL;
    char const    *redis_host      = "127.0.0.1";
    long           redis_port      = 6379;
    int            i,e;
    char          *s;

    for (i=0; _opts && _opts[i]; i+=2) {
        if (!strcmp(_opts[i], "redis_host")) {
            redis_host = _opts[i+1];
        } else if (!strcmp(_opts[i], "redis_port")) {
            redis_port = strtol(_opts[i+1], &s, 10);
            e = (redis_port == 0 && errno == EINVAL);
            if (e/*err*/) goto error_invalid_redis_port;
            e = (redis_port <= 0 || redis_port >= 65535);
            if (e/*err*/) goto error_invalid_redis_port;
        }
    }
    
    mdb = calloc(1, sizeof(struct mdb));
    if (!mdb/*err*/) goto error_calloc;

    mdb->redis = redisConnect(redis_host, redis_port);
    if (!mdb->redis/*err*/) goto error_calloc;
    e = mdb->redis->errstr && mdb->redis->err;
    if (e/*err*/) goto error_redis;

    *_mdb = mdb;
    return true;
 cleanup:
    mdb_destroy(mdb);
    return false;
 error_invalid_redis_port:
    syslog(LOG_ERR, "mdb: Invalid 'redis_port' option.");
    goto cleanup;
 error_calloc:
    syslog(LOG_ERR, "mdb: %s", strerror(errno));
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: %s", mdb->redis->errstr);
    goto cleanup;
}

void mdb_destroy (mdb *_mdb) {
    if (_mdb) {
        if (_mdb->redis) redisFree(_mdb->redis);
        free(_mdb);
    }
}

static struct mdb_map *
mdb_map_search(mdb *_mdb, char const *_t, bool _alloc) {
    for (size_t i=0; i<_mdb->mapsz; i++) {
        if (!strcmp(_mdb->map[i].type, _t)) {
            return _mdb->map+i;
        }
    }
    if (_alloc) {
        if (_mdb->mapsz==20) {
            syslog(LOG_ERR, "mdb: Maximun number of tables reached.");
            return NULL;
        }
        return &_mdb->map[_mdb->mapsz++];
    } else {
        return NULL;
    }
}

static char const *
mdb_get_table(mdb *_mdb, char const *_t) {
    struct mdb_map *map = mdb_map_search(_mdb, _t, false);
    if (map) {
        if (map->table) {
            return map->table;
        } else {
            syslog(LOG_ERR, "mdb: Type %s unmapped.", _t);
            return NULL;
        }
    } else {
        return _t;
    }
}

bool mdb_map(mdb *_mdb, char const _t[], char const _fmt[], ...) {
    char           *type  = strdup(_t);
    char           *table = NULL;
    struct mdb_map *map;
    int             e;
    if (!type/*err*/) goto error_strdup;
    if (_fmt) {
        va_list va;
        va_start(va, _fmt);
        e = vasprintf(&table, _fmt, va);
        va_end(va);
        if (!e/*err*/) goto error_vasprintf;
    }
    map = mdb_map_search(_mdb, _t, true);
    if (!map/*err*/) goto error;
    free(map->type);
    free(map->table);
    map->type = type;
    map->table = table;
    return true;
 error:
    free(type);
    free(table);
    return false;
 error_strdup: error_vasprintf:
    syslog(LOG_ERR, "%s", strerror(errno));
    goto error;
}

bool mdb_insert (mdb *_mdb, char const *_t, mdb_k _id, void const *_d, size_t _dsz) {

    bool           retval = false;
    redisReply    *reply  = NULL;
    char const    *table  = mdb_get_table(_mdb, _t); if (!table/*err*/) return false;
    char const    *argv[] = {"HSETNX"       , table        , _id.dptr , _d};
    size_t         argl[] = {strlen(argv[0]), strlen(table), _id.dsize, _dsz};
    size_t         argc   = 4;

    
    reply = redisCommandArgv(_mdb->redis, argc, argv, argl);
    if (!reply/*err*/)                             goto error_redis_ctx;
    if (reply->type == REDIS_REPLY_ERROR/*err*/)   goto error_redis;
    if (reply->type != REDIS_REPLY_INTEGER/*err*/) goto error_redis_reply;
    if (reply->integer!=1/*err*/)                  goto error_exists;

    retval = true;
 cleanup:
    if (reply) freeReplyObject(reply);
    return retval;
 error_redis_ctx:
    syslog(LOG_ERR, "mdb: redis: %s", _mdb->redis->errstr);
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: hsetnx: %s", reply->str);
    goto cleanup;
 error_redis_reply:
    syslog(LOG_ERR, "mdb: redis: Unexpected reply to HEXIST.");
    goto cleanup;
 error_exists:
    syslog(LOG_ERR, "mdb: redis: The field already exists.");
    goto cleanup;
}

bool mdb_replace (mdb *_mdb, char const *_t, mdb_k _id, void const *_d, size_t _dsz) {

    bool        retval = false;
    redisReply *reply  = NULL;
    char const *table  = mdb_get_table(_mdb, _t); if (!table/*err*/) return false;
    char const *argv[] = {"HSET"         , table        , _id.dptr , _d};
    size_t      argl[] = {strlen(argv[0]), strlen(table), _id.dsize, _dsz};
    size_t      argc   = 4;
    
    reply = redisCommandArgv(_mdb->redis, argc, argv, argl);
    if (!reply/*err*/)                             goto error_redis_ctx;
    if (reply->type == REDIS_REPLY_ERROR/*err*/)   goto error_redis;
    if (reply->type != REDIS_REPLY_INTEGER/*err*/) goto error_redis_reply;
    
    retval = true;
 cleanup:
    if (reply) freeReplyObject(reply);
    return retval;
 error_redis_ctx:
    syslog(LOG_ERR, "mdb: redis: %s", _mdb->redis->errstr);
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: hexists: %s", reply->str);
    goto cleanup;
 error_redis_reply:
    syslog(LOG_ERR, "mdb: redis: Unexpected reply to HEXIST.");
    goto cleanup;
}

bool mdb_search(mdb *_mdb, char const *_t, mdb_k _id, void **_d, size_t *_dsz, bool *_opt_exists) {
    
    bool        retval = false;
    redisReply *reply  = NULL;
    char const *table  = mdb_get_table(_mdb, _t); if (!table/*err*/) return false;
    char const *argv[] = {"HGET"         , table        , _id.dptr};
    size_t      argl[] = {strlen(argv[0]), strlen(table), _id.dsize};
    size_t      argc   = 3;

    reply = redisCommandArgv(_mdb->redis, argc, argv, argl);
    if (!reply/*err*/)                             goto error_redis_ctx;
    if (reply->type == REDIS_REPLY_ERROR/*err*/)   goto error_redis;
    if (reply->type == REDIS_REPLY_NIL) {
        *_d = NULL;
        *_dsz = 0;
        if (!_opt_exists/*err*/) goto error_does_not_exist;
        *_opt_exists = false;
    } else {
        if (_opt_exists) {
            *_opt_exists = true;
        }
        if (_d) {
            *_d = reply->str;
            *_dsz = reply->len;
            reply->str = NULL;
        }
    }
    retval = true;
 cleanup:
    if (reply) freeReplyObject(reply);
    return retval;
 error_redis_ctx:
    syslog(LOG_ERR, "mdb: redis: %s", _mdb->redis->errstr);
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: hget: %s", reply->str);
    goto cleanup;
 error_does_not_exist:
    syslog(LOG_ERR, "mdb: redis: The field does not exists.");
    goto cleanup;
}

bool mdb_search_cp(mdb *_mdb, char const *_t, mdb_k _id, void *_d, size_t _dsz, bool *_opt_exists) {
    void  *d   = NULL;
    size_t dsz = 0;
    bool   ret = false;
    if (!mdb_search(_mdb, _t, _id, &d, &dsz, _opt_exists)) {
        return false;
    }
    if (d) {
        if (_dsz < dsz/*err*/) goto cleanup_invalid_size;
        memcpy(_d, d, dsz);
        if (_dsz>dsz) {
            memset(((char*)_d)+dsz, 0, _dsz-dsz);
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

bool mdb_delete(mdb *_mdb, char const *_t, mdb_k _id) {

    bool        retval = false;
    redisReply *reply  = NULL;
    char const *table  = mdb_get_table(_mdb, _t); if (!table/*err*/) return false;
    char const *argv[] = {"HDEL"         , table        , _id.dptr};
    size_t      argl[] = {strlen(argv[0]), strlen(table), _id.dsize};
    size_t      argc   = 3;

    reply = redisCommandArgv(_mdb->redis, argc, argv, argl);
    if (!reply/*err*/)                             goto error_redis_ctx;
    if (reply->type == REDIS_REPLY_ERROR/*err*/)   goto error_redis;
    
    retval = true;
 cleanup:
    if (reply) freeReplyObject(reply);
    return retval;
 error_redis_ctx:
    syslog(LOG_ERR, "mdb: redis: %s", _mdb->redis->errstr);
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: hget: %s", reply->str);
    goto cleanup;
}

bool mdb_iter_create(mdb *_mdb, char const *_t, mdb_iter **_iter) {

    bool        retval = false;
    redisReply *reply  = NULL;
    mdb_iter   *iter   = calloc(1, sizeof(mdb_iter));
    char const *table  = mdb_get_table(_mdb, _t); if (!table/*err*/) return false;
    char const *argv[] = {"HKEYS"        , table};
    size_t      argl[] = {strlen(argv[0]), strlen(table)};
    size_t      argc   = 2;

    if (!iter/*err*/) goto error_alloc;
    reply = redisCommandArgv(_mdb->redis, argc, argv, argl);
    if (!reply/*err*/)                             goto error_redis_ctx;
    if (reply->type == REDIS_REPLY_ERROR/*err*/)   goto error_redis;
    if (reply->type != REDIS_REPLY_ARRAY/*err*/)   goto error_redis_reply;

    iter->reply = reply;
    reply = NULL;
    *_iter = iter;

    retval = true;
 cleanup:
    if (reply) freeReplyObject(reply);
    return retval;
 error_redis_ctx:
    syslog(LOG_ERR, "mdb: redis: %s", _mdb->redis->errstr);
    goto cleanup;
 error_redis:
    syslog(LOG_ERR, "mdb: redis: hkeys: %s", reply->str);
    goto cleanup;
 error_redis_reply:
    syslog(LOG_ERR, "mdb: redis: Unexpected reply to HEXIST.");
    goto cleanup;
 error_alloc:
    syslog(LOG_ERR, "mdb: redis: %s", strerror(errno));
    goto cleanup;
}

void mdb_iter_destroy(mdb_iter **_iter) {
    if (*_iter) {
        freeReplyObject((*_iter)->reply);
        free((*_iter));
        *_iter = NULL;
    }
}

bool mdb_iter_loop(mdb_iter *_iter, mdb_k *_key) {
    if((_iter->pos)<(_iter->reply->elements)) {
        _key->dptr  = _iter->reply->element[_iter->pos]->str;
        _key->dsize = _iter->reply->element[_iter->pos]->len;
        _iter->pos++;
        return true;
    } else {
        return false;
    }
}

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

mdb_k mdb_k_str (char const _s[]) {
    if (_s) {
        mdb_k k = {(char*)_s, strlen(_s)};
        return k;
    } else {
        mdb_k k = {NULL, 0};
        return k;
    }
}

mdb_k mdb_k_uuid_str (char const _s[], uuid_t _uuid) {
    if (uuid_parse(_s, _uuid)!=-1) {
        return mdb_k_uuid(_uuid);
    } else {
        mdb_k k = {0};
        syslog(LOG_ERR, "Invalid UUID: %s", _s);
        return k;
    }
}

void mdb_k_print (mdb_k _id, FILE *_fp1) {
    fwrite(_id.dptr, 1, _id.dsize, _fp1);
}
