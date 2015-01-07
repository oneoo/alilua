#include "config.h"
#include "main.h"
#include "network.h"
#include "lua-ext.h"
#include <regex.h>
#include "cached-access.h"

int lua_routed = 0;
static char temp_buf[8192];

static char *v_p[100] = {0};
static int v_p_len[100] = {0};
static char *v_c[100] = {0};
static int v_p_count = 0;

static char *v_p2[100] = {0};
static int v_p_len2[100] = {0};
static int v_p_count2 = 0;

static int match_max = 0;
static int match_max_len = 0;
static int the_match_pat = 0;

#define REGEX_CACHE_SIZE 102400
static regex_t *regex_cache[REGEX_CACHE_SIZE] = {0};
static uint32_t regex_cache_key[REGEX_CACHE_SIZE] = {0};

static int is_match(const char *rule, const char *uri)
{
    static regex_t *re = NULL;
    static regmatch_t pm[100];
    int m = strlen(rule);
    char *nr = ((m * 2 + 10 < 8192) ? (char *)temp_buf : malloc(m * 2 + 10));
    int i = 0, gk = 0, nr_len = 0, fck = 0;

    v_p_count2 = 1;

    if(rule[0] != '^') {
        nr[0] = '^';
        nr_len++;
    }

    for(i = 0; i < m; i++) {

        if(rule[i] == ':') {
            gk = 1;
            v_p2[v_p_count2] = (char *)rule + (i + 1);

            if(fck == 0) {
                fck = nr_len - 1;
            }

        } else {
            if((rule[i] == '(' || rule[i] == '[' || rule[i] == '$' || rule[i] == '/')) {
                if(fck == 0 && rule[i] != '/') {
                    fck = nr_len - 1;
                }

                if(gk == 1) {
                    gk = 0;
                    v_p_len2[v_p_count2] = (rule + i) - v_p2[v_p_count2];

                    if(rule[i] == '/') {
                        nr[nr_len++] = '(';
                        nr[nr_len++] = '.';
                        nr[nr_len++] = '+';
                        nr[nr_len++] = ')';
                        v_p_count2 ++;
                    }

                } else {
                    v_p2[v_p_count2] = NULL;
                }
            }
        }

        if(gk == 0) {
            nr[nr_len++] = rule[i];
        }

        if(rule[i] == '(') {
            v_p_count2 ++;
        }
    }

    if(gk == 1) {
        v_p_len2[v_p_count2] = (rule + i) - v_p2[v_p_count2];
        nr[nr_len++] = '(';
        nr[nr_len++] = '.';
        nr[nr_len++] = '+';
        nr[nr_len++] = ')';
        v_p_count2 ++;
    }

    nr[nr_len] = '\0';

    if((fck > 1 && strncmp(uri, nr + 1, fck) != 0)) {
        return 0;
    }

    uint32_t key = fnv1a_32(nr, nr_len);
    int _key = key % REGEX_CACHE_SIZE;
    re = regex_cache[_key];

    if(re && regex_cache_key[_key] != key) {
        re = NULL;
        regfree(regex_cache[_key]);
        free(regex_cache[_key]);
        regex_cache[_key] = NULL;
    }

    if(!re) {
        re = malloc(sizeof(regex_t));

        if(!re || regcomp(re, nr, REG_EXTENDED | REG_ICASE) != 0) {
            LOGF(ERR, "Router Failed to compile regex '%s'", rule);

            if(re) {
                regfree(re);
            }

            if(nr != (char *)temp_buf) {
                free(nr);
            }

            return 0;
        }

        regex_cache[_key] = re;
        regex_cache_key[_key] = key;
    }

    unsigned int g = 0;
    int reti = regexec(re, uri, 100, pm, 0);

    if(reti == 0) {
        for(g = 0; g < 100; g++) {
            if(pm[g].rm_so == (size_t) - 1) {
                break; // No more groups
            }
        }

        if(g > match_max || (g >= match_max && nr_len > match_max_len)) {
            for(v_p_count = 0; v_p_count < v_p_count2; v_p_count++) {
                free(v_c[v_p_count]);
                v_c[v_p_count] = NULL;
                v_p[v_p_count] = v_p2[v_p_count];
                v_p_len[v_p_count] = v_p_len2[v_p_count];
            }

            match_max = g;
            match_max_len = nr_len;

            for(g = 1; g < match_max; g++) {
                if(v_p[g]) {
                    free(v_c[g]);

                    v_c[g] = malloc(pm[g].rm_eo - pm[g].rm_so + 1);
                    memcpy(v_c[g], uri + pm[g].rm_so, pm[g].rm_eo - pm[g].rm_so);

                    if(v_c[g][pm[g].rm_eo - pm[g].rm_so - 1] == '/') {
                        pm[g].rm_eo --;
                    }

                    v_c[g][pm[g].rm_eo - pm[g].rm_so] = '\0';

                } else {
                    v_c[g] = NULL;
                }
            }

        } else {
            g = 0;
        }

    }/*else {

        char msgbuf[100];
        regerror(reti, &re, msgbuf, sizeof(msgbuf));
        fprintf(stderr, "Regex match failed: %s\n", msgbuf);
    }*/

    if(nr != (char *)temp_buf) {
        free(nr);
    }

    return g;
}

int lua_f_router(lua_State *L)
{
    if(lua_routed) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    lua_routed = 1;

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "excepted uri");
        return 2;
    }

    if(!lua_istable(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "excepted router table");
        return 2;
    }

    const char *uri = lua_tostring(L, 1);
    int uri_len = strlen(uri);

    if(uri_len < 1 || uri[0] != '/') {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushstring(L, "not a uri");
        return 3;
    }

    if(lua_isstring(L, 3)) {
        // try local lua script file
        epdata_t *epd = NULL;

        lua_getglobal(L, "__epd__");

        if(lua_isuserdata(L, -1)) {
            epd = lua_touserdata(L, -1);
        }

        lua_pop(L, 1);

        if(epd) {

            size_t len = 0;
            const char *fname = lua_tolstring(L, 3, &len);
            char *full_fname = (char *)&temp_buf;
            memcpy(full_fname, epd->vhost_root, epd->vhost_root_len);
            memcpy(full_fname + epd->vhost_root_len, fname, len);
            memcpy(full_fname + epd->vhost_root_len + len , uri, uri_len);

            len = epd->vhost_root_len + len + uri_len;

            full_fname[len] = '\0';

            if(full_fname[len - 4] == '.' && full_fname[len - 3] == 'l' && full_fname[len - 1] == 'a') {
                if(cached_access(fnv1a_32(full_fname, len), full_fname) != -1) {
                    lua_pushnil(L);
                    lua_pushstring(L, full_fname + (len - uri_len));
                    return 2;
                }
            }

            if(full_fname[len - 1] != '/') {
                memcpy(full_fname + len, ".lua", 4);
                full_fname[len + 4] = '\0';

                //if(access(full_fname, F_OK) != -1) {
                if(cached_access(fnv1a_32(full_fname, len + 4), full_fname) != -1) {
                    lua_pushnil(L);
                    lua_pushstring(L, full_fname + (len - uri_len));
                    return 2;
                }

            } else {

                memcpy(full_fname + len, "index.lua", 9);
                full_fname[len + 9] = '\0';

                //if(access(full_fname, F_OK) != -1) {
                if(cached_access(fnv1a_32(full_fname, len + 9), full_fname) != -1) {
                    lua_pushnil(L);
                    lua_pushstring(L, full_fname + (len - uri_len));
                    return 2;
                }

                memcpy(full_fname + len - 1, ".lua", 4);
                full_fname[len - 1 + 4] = '\0';

                //if(access(full_fname, F_OK) != -1) {
                if(cached_access(fnv1a_32(full_fname, len + 3), full_fname) != -1) {
                    lua_pushnil(L);
                    lua_pushstring(L, full_fname + (len - uri_len));
                    return 2;
                }
            }
        }
    }

    int pat = 0;
    lua_pushvalue(L, 2);
    lua_pushnil(L);
    match_max = 0;

    while(lua_next(L, -2)) {
        if(lua_isstring(L, -2)) {
            if(is_match(lua_tostring(L, -2), uri)) {
                the_match_pat = pat;
            }
        }

        lua_pop(L, 1);
        pat++;

        if(pat >= 100) {
            break;
        }
    }

    lua_pop(L, 1);

    pat = 0;
    lua_pushvalue(L, 2);
    lua_pushnil(L);

    while(lua_next(L, -2)) {
        if(lua_isstring(L, -2)) {
            if(match_max > 0 && pat == the_match_pat) {
                lua_pushvalue(L, -1);

                lua_remove(L, -2);
                lua_remove(L, -2);
                lua_remove(L, -2);

                lua_createtable(L, 0, match_max);
                int i = 0;

                for(i = 1; i < match_max; i++) {
                    if(!v_p[i]) {
                        continue;
                    }

                    lua_pushlstring(L, v_p[i], v_p_len[i]);
                    lua_pushstring(L, v_c[i]);
                    free(v_c[i]);
                    v_c[i] = NULL;
                    lua_settable(L, -3);
                }

                return 2;
            }
        }

        lua_pop(L, 1);
        pat++;

        if(pat >= 100) {
            break;
        }
    }

    lua_pop(L, 1);

    return 0;
}
