#include "vhost.h"

static vhost_conf_t *vhost_conf_head = NULL;
static lua_State *L = NULL;

int max_request_header = 0; // default no limit
int max_request_body = 0;
int code_cache_ttl = 60; // set 0 to disable code cache

static int get_int_in_table(lua_State *L, const char *name)
{
    int r = 0;
    lua_getfield(L, -1, name);

    if(lua_isnumber(L, -1)) {
        r = (int)lua_tonumber(L, -1);
    }

    lua_pop(L, 1);
    return r;
}

int update_vhost_routes(char *f)
{
    if(!f) {
        return 0;
    }

    if(!L) {
        L = luaL_newstate();
    }

    if(!L) {
        LOGF(ERR, "can't open lua state!");
        return 0;
    }

    luaL_openlibs(L);

    luaL_dostring(L, "config={} host_route={}");

    if(luaL_dofile(L, f)) {
        LOGF(ERR, "Couldn't load file: %s", lua_tostring(L, -1));
        return 0;
    }

    vhost_conf_t *last_vcf = vhost_conf_head, *in_link = NULL;

    while(last_vcf) {
        if(!last_vcf->next) {
            break;
        }

        last_vcf = last_vcf->next;
    }

    lua_getglobal(L, "config");

    max_request_header = get_int_in_table(L, "max-request-header");

    if(max_request_header < 100) {
        LOGF(ERR, "config error: max-request-header=%d, use default 0", max_request_header);
        max_request_header = 0;
    }

    max_request_body = get_int_in_table(L, "max-request-body");

    if(max_request_body < 1024) {
        LOGF(ERR, "config error: max-request-body=%d, use default 0", max_request_body);
        max_request_body = 0;
    }

    code_cache_ttl = get_int_in_table(L, "code-cache-ttl");

    if(code_cache_ttl < 0) {
        LOGF(ERR, "config error: max-request-body=%d, use default 60", code_cache_ttl);
        code_cache_ttl = 60;
    }

    lua_pop(L, 1);

    lua_getglobal(L, "host_route");
    lua_pushnil(L);

    while(lua_next(L, -2)) {
        if(lua_isstring(L, -2)) {
            size_t host_len = 0;
            char *host = (char *)lua_tolstring(L, -2, &host_len);
            in_link = get_vhost_conf(host, 0);

            if(host_len < 256) {
                size_t root_len = 0;
                char *root = (char *)lua_tolstring(L, -1, &root_len);
                vhost_conf_t *vcf = NULL;

                if(root_len < 1024) {
                    if(!in_link) {
                        LOGF(ALERT, "init vhost: %s => %s", host, root);

                        vcf = malloc(sizeof(vhost_conf_t));
                        memcpy(vcf->host, host, host_len);
                        vcf->host[host_len] = '\0';
                        vcf->host_len = host_len;
                        memcpy(vcf->root, root, root_len);
                        vcf->root[root_len] = '\0';

                        vcf->mtype = (host[0] == '*');
                        vcf->prev = NULL;
                        vcf->next = NULL;

                        if(!vhost_conf_head) {
                            vhost_conf_head = vcf;
                            last_vcf = vcf;

                        } else {
                            if(vcf->mtype == 1) {

                                if(strlen(last_vcf->host) > 1) {
                                    last_vcf->next = vcf;
                                    vcf->prev = last_vcf;
                                    last_vcf = vcf;

                                } else {
                                    (last_vcf->prev)->next = vcf;
                                    vcf->prev = last_vcf->prev;
                                    vcf->next = last_vcf;
                                    last_vcf->prev = vcf;
                                }

                            } else {
                                vhost_conf_head->prev = vcf;
                                vcf->next = vhost_conf_head;
                                vhost_conf_head = vcf;
                            }
                        }

                    } else {
                        memcpy(in_link->root, root, root_len);
                        in_link->root[root_len] = '\0';
                        vcf = in_link;
                    }

                    int j = root_len;

                    while(j > 0) {
                        if(vcf->root[--j] == '/') {
                            vcf->vhost_root_len = j;
                            break;
                        }
                    }

                    vcf->root_len = root_len;
                }
            }
        }

        lua_pop(L, 1);
    }

    lua_pop(L, 1);

    //lua_close(L);

    return 1;
}

vhost_conf_t *get_vhost_conf(char *host, int prefix)
{
    if(!host) {
        return NULL;
    }

    vhost_conf_t *vcf = vhost_conf_head;

    while(vcf) {
        if(stricmp(host, vcf->host) == 0) {
            return vcf;
        }

        int a = strlen(host);
        int b = vcf->host_len;

        if(prefix && vcf->mtype == 1 && (b == 1 || (a >= (b - 1) && stricmp(vcf->host + 1, host + (a - b + 1)) == 0))) {
            return vcf;
        }

        vcf = vcf->next;
    }

    return NULL;
}

static char *_default_index_file = NULL;
static int _vhost_root_len = 0;
char *get_vhost_root(char *host, int *vhost_root_len)
{
    vhost_conf_t *vcf = get_vhost_conf(host, 1);

    if(!vcf) {
        if(!_default_index_file) {
            int n = 50;

            if(getarg("app")) {
                char npath[1024] = {0};
                realpath(getarg("app"), npath);
                _vhost_root_len = strlen(npath);
                _default_index_file = malloc(_vhost_root_len);

                sprintf(_default_index_file, "%s", npath);

            } else {

                _vhost_root_len = strlen(process_chdir);
                _default_index_file = malloc(_vhost_root_len + 50);
                sprintf(_default_index_file, "%sroute.lua", process_chdir);
            }

            while(_vhost_root_len > 0) {
                if(_default_index_file[--_vhost_root_len] == '/') {
                    break;
                }
            }
        }



        *vhost_root_len = _vhost_root_len;

        return _default_index_file;
    }

    *vhost_root_len = vcf->vhost_root_len;
    return vcf->root;
}
