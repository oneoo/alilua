#include "lua.h"
#include "lauxlib.h"
#include "ok_mo.h"
#include "../../src/network.h"
#include "../../coevent/merry/common/rbtree.h"

static char buf[4096] = {0};
static const char *default_key = "*all";
extern int code_cache_ttl;
static const char *__i18n_locale = NULL;
static size_t __i18n_locale_len = 0;

typedef struct {
    ok_mo *mo;
    void *next;
    unsigned long lastmtime;
    char domain[68];
    char filename[4000];
} mos_t;
typedef struct {
    char *key;
    mos_t *mos;
} rb_key_t;
static rb_tree_t mo_tree;
static int rbtree_compare(const void *lhs, const void *rhs)
{
    const rb_key_t *l = (const rb_key_t *)lhs;
    const rb_key_t *r = (const rb_key_t *)rhs;

    return strcmp(l->key, r->key);
}
static rb_key_t *get_mo_tree_node(const char *c_key)
{
    rb_key_t *key = NULL, _key;
    rb_tree_node_t *tnode = NULL;
    _key.key = (char*)c_key;

    if(rb_tree_find(&mo_tree, &_key, &tnode) == RB_OK) {
        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

    } else {
        tnode = malloc(sizeof(rb_tree_node_t) + sizeof(rb_key_t));
        memset(tnode, 0, sizeof(rb_tree_node_t) + sizeof(rb_key_t));

        key = (rb_key_t *)((char *)tnode + sizeof(rb_tree_node_t));

        key->key = malloc(strlen(c_key));
        strcpy(key->key, c_key);

        key->mos = NULL;

        if(rb_tree_insert(&mo_tree, key, tnode) != RB_OK) {
            free(tnode);
            tnode = NULL;
            key = NULL;
        }
    }

    return key;
}

static epdata_t *get_epd(lua_State *L)
{
    epdata_t *epd = NULL;

    lua_getglobal(L, "__epd__");

    if(lua_isuserdata(L, -1)) {
        epd = lua_touserdata(L, -1);
    }

    lua_pop(L, 1);

    lua_getglobal(L, "__i18n_locale");

    if(lua_isstring(L, -1)) {
        __i18n_locale = lua_tolstring(L, -1, &__i18n_locale_len);
    }

    lua_pop(L, 1);

    return epd;
}

static int lua_f___(lua_State *L)
{
    const char *key = NULL;
    const char *value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(lua_isstring(L, 2)) {
        domain = lua_tolstring(L, 2, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_value(mos->mo, key);

            if(value && key != value) {
                lua_pushstring(L, value);
                return 1;
            }
        }

        mos = mos->next;
    }

    lua_pushstring(L, key);
    return 1;
}

static int lua_f__e(lua_State *L)
{
    const char *key = NULL;
    const char *value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(lua_isstring(L, 2)) {
        domain = lua_tolstring(L, 2, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_value(mos->mo, key);

            if(value && key != value) {
                //lua_pushstring(L, value);
                //return 1;
                network_send(epd, value, strlen(value));
                return 0;
            }
        }

        mos = mos->next;
    }

    //lua_pushstring(L, key);
    network_send(epd, key, strlen(key));
    return 0;
}

static int lua_f__x(lua_State *L)
{
    const char *key = NULL;
    const char *context = NULL;
    const char *value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a context!");
        return 2;
    }

    context = lua_tostring(L, 2);

    if(lua_isstring(L, 3)) {
        domain = lua_tolstring(L, 3, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_value_in_context(mos->mo, context, key);

            if(value && key != value) {
                lua_pushstring(L, value);
                return 1;
            }
        }

        mos = mos->next;
    }

    lua_pushstring(L, key);
    return 1;
}

static int lua_f__ex(lua_State *L)
{
    const char *key = NULL;
    const char *context = NULL;
    const char *value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a context!");
        return 2;
    }

    context = lua_tostring(L, 2);

    if(lua_isstring(L, 3)) {
        domain = lua_tolstring(L, 3, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_value_in_context(mos->mo, context, key);

            if(value && key != value) {
                //lua_pushstring(L, value);
                //return 1;
                network_send(epd, value, strlen(value));
                return 0;
            }
        }

        mos = mos->next;
    }

    //lua_pushstring(L, key);
    network_send(epd, key, strlen(key));
    return 0;
}

static int lua_f__n(lua_State *L)
{
    const char *key = NULL;
    const char *plural = NULL;
    int n = 0;
    const char *value = NULL, *last_value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a plural!");
        return 2;
    }

    if(!lua_isnumber(L, 3)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a number!");
        return 2;
    }

    plural = lua_tostring(L, 2);
    n = lua_tonumber(L, 3);

    if(lua_isstring(L, 4)) {
        domain = lua_tolstring(L, 4, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_plural_value(mos->mo, key, plural, n);

            if(plural == value) {
                last_value = plural;
            }

            if(value && key != value &&  key != plural) {
                lua_pushstring(L, value);
                return 1;
            }
        }

        mos = mos->next;
    }

    if(last_value == plural) {
        lua_pushstring(L, plural);

    } else {
        lua_pushstring(L, key);
    }

    return 1;
}

static int lua_f__nx(lua_State *L)
{
    const char *key = NULL;
    const char *context = NULL;
    const char *plural = NULL;
    int n = 0;
    const char *value = NULL, *last_value = NULL;
    char *filename = NULL;
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    size_t len = 0;

    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    if(__i18n_locale_len > 0) {
        memcpy(filename + epd->vhost_root_len, __i18n_locale, __i18n_locale_len);
    }

    filename[epd->vhost_root_len + __i18n_locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    if(!mo_node) {
        lua_pushnil(L);
        return 1;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a plural!");
        return 2;
    }

    if(!lua_isnumber(L, 3)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a number!");
        return 2;
    }

    if(!lua_isstring(L, 4)) {
        lua_pushnil(L);
        lua_pushstring(L, "need a context!");
        return 2;
    }

    plural = lua_tostring(L, 2);
    n = lua_tonumber(L, 3);
    context = lua_tostring(L, 4);

    if(lua_isstring(L, 5)) {
        domain = lua_tolstring(L, 5, &len);

        if(len == 0) {
            domain = default_key;
        }

    } else {
        domain = default_key;
    }

    key = lua_tostring(L, 1);

    mos = mo_node->mos;

    while(mos) {
        if(domain[0] == '*' || strcmp(mos->domain, domain) == 0) {
            value = ok_mo_plural_value_in_context(mos->mo, context, key, plural, n);

            if(plural == value) {
                last_value = plural;
            }

            if(value && key != value &&  key != plural) {
                lua_pushstring(L, value);
                return 1;
            }
        }

        mos = mos->next;
    }

    if(last_value == plural) {
        lua_pushstring(L, plural);

    } else {
        lua_pushstring(L, key);
    }

    return 1;
}

static int lua_f_i18n_load_mofile(lua_State *L)
{
    rb_key_t *mo_node = NULL;
    const char *domain = NULL;
    mos_t *mos = NULL;
    int has = 0;
    size_t len = 0;
    size_t locale_len = 0;
    struct stat fst;
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a Locale!");
        return 2;
    }

    const char *locale = lua_tolstring(L, 1, &locale_len);

    if(locale_len > 10) {
        lua_pushnil(L);
        lua_pushstring(L, "Locale length <= 10!");
        return 2;
    }

    if(!lua_isstring(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "Need a file path!");
        return 2;
    }

    if(lua_isstring(L, 3)) {
        domain = lua_tolstring(L, 3, &len);

        if(len == 0) {
            domain = default_key;

        } else if(len > 60) {
            lua_pushnil(L);
            lua_pushstring(L, "domain length <= 60!");
            return 2;
        }

    } else {
        domain = default_key;
    }

    const char *fname = lua_tolstring(L, 2, &len);

    if(epd->vhost_root_len + len > 4000) {
        lua_pushnil(L);
        lua_pushstring(L, "file path length <= 4000!");
        return 2;
    }

    char *filename = (char *)&buf;
    memcpy(filename, epd->vhost_root, epd->vhost_root_len);

    memcpy(filename + epd->vhost_root_len, locale, locale_len);
    filename[epd->vhost_root_len + locale_len] = '\0';
    mo_node = get_mo_tree_node(filename);

    memcpy(filename + epd->vhost_root_len, fname, len);
    filename[epd->vhost_root_len + len] = '\0';

    if(mo_node) {
        if(mo_node->mos) {
            //if(code_cache_ttl > 0)
            {
                mos = mo_node->mos;

                while(mos) {
                    if(now - mos->lastmtime > code_cache_ttl) {

                        if(stat(mos->filename, &fst) >= 0) {
                            if(fst.st_mtime > mos->lastmtime) {
                                FILE *fp = fopen(mos->filename, "rb");

                                if(fp) {
                                    ok_mo_free(mos->mo);
                                    mos->mo = ok_mo_read(fp, file_input_func);
                                    fclose(fp);
                                }
                            }
                        }

                        mos->lastmtime = now;
                    }

                    mos = mos->next;
                }
            }

            mos = mo_node->mos;

            while(mos) {
                if(strcmp(mos->filename, filename) == 0) {
                    has = 1;
                    break;
                }

                mos = mos->next;
            }
        }

        if(has == 0) {
            FILE *fp = fopen(filename, "rb");

            if(fp) {
                mos = malloc(sizeof(mos_t));
                mos->mo = ok_mo_read(fp, file_input_func);
                fclose(fp);
                strcpy(mos->domain, domain);
                strcpy(mos->filename, filename);
                mos->lastmtime = now;
                mos->next = mo_node->mos;
                mo_node->mos = mos;

            } else {
                lua_pushnil(L);
                lua_pushstring(L, strerror(errno));
                return 2;
            }
        }
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lua_f_i18n_set_locale(lua_State *L)
{
    epdata_t *epd = get_epd(L);

    if(!epd) {
        lua_pushnil(L);
        lua_pushstring(L, "miss epd!");
        return 2;
    }

    if(!lua_isstring(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "miss Locale!");
        return 2;
    }

    lua_pushvalue(L, 1);
    lua_setglobal(L, "__i18n_locale");

    lua_pushboolean(L, 1);
    return 1;
}

LUALIB_API int luaopen_i18n(lua_State *L)
{
    rb_tree_new(&mo_tree, rbtree_compare);

    lua_register(L, "i18n_load_mofile", lua_f_i18n_load_mofile);
    lua_register(L, "i18n_set_locale", lua_f_i18n_set_locale);
    lua_register(L, "__", lua_f___);
    lua_register(L, "_e", lua_f__e);
    lua_register(L, "_x", lua_f__x);
    lua_register(L, "_ex", lua_f__ex);
    lua_register(L, "_n", lua_f__n);
    lua_register(L, "_nx", lua_f__nx);
    return 0;
}
