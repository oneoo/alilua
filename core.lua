mysql = require('mysql')
cjson = require('cjson.safe')
memcached = require('memcached')
redis = require('redis')
date = require('date')
loadtemplate = require('loadtemplate')
httpclient = (require "httpclient").httprequest
llmdb = require('llmdb-client')
cmsgpack = require('cmsgpack')
ok, cmsgpack_safe = pcall(require, 'cmsgpack.safe')

md5 = function(s) return crypto.evp.digest('md5', s) end
function string:trim() return self:gsub('^%s*(.-)%s*$', '%1') end
function trim(s) return s:trim() end
function string:startsWith(s, i) return _G['string-utils'].startsWith(self, s, i) end
function string:endsWith(s, i) return _G['string-utils'].endsWith(self, s, i) end
explode = _G['string-utils'].explode
implode = table.concat
escape = _G['string-utils'].escape
escape_uri = _G['string-utils'].escape_uri
unescape_uri = _G['string-utils'].unescape_uri
urlencode = _G['string-utils'].escape_uri
urldecode = _G['string-utils'].unescape_uri
base64_encode = _G['string-utils'].base64_encode
base64_decode = _G['string-utils'].base64_decode
strip = _G['string-utils'].strip
iconv = _G['string-utils'].iconv
iconv_strlen = _G['string-utils'].iconv_strlen
iconv_substr = _G['string-utils'].iconv_substr
json_encode = cjson.encode
json_decode = cjson.decode
htmlspecialchars = function(value)
    if not value then
        return ''
    end
    if type(value) == "number" then
        return value
    end
    value = tostring(value)
    local subst =
    {
    ["&"] = "&amp;";
    ['"'] = "&quot;";
    ["'"] = "&apos;";
    ["<"] = "&lt;";
    [">"] = "&gt;";
    }
    return (value:gsub("[&\"'<>]", subst))
end
htmlspecialchars_decode = function(value)
    if not value then
        return ''
    end
    if type(value) == "number" then
        return value
    end
    value = tostring(value)
    local subst =
    {
    ["&amp;"] = "&";
    ['&quot;'] = '"';
    ["&apos;"] = "'";
    ["&lt;"] = "<";
    ["&gt;"] = ">";
    }
    return (value:gsub("[&\"'<>]", subst))
end

function printf(s, ...) print(s:format(...)) end
function sprintf(s, ...) return (s:format(...)) end

function dump(o, doprint, inden)
    local P=io.write
    if not inden then inden = 1 end
    if not doprint then
        if type(o) == 'table' then
            local s = '{\n'
            for k,v in pairs(o) do
                if type(k) == 'number' then k = '['..k..']' end
                s = s .. string.rep('    ',inden)..k.. ' = ' .. dump(v, doprint, inden+1) .. ',\n'
            end
            return s .. string.rep('    ',inden-1).. '}'
        else
            local ot = type(o)
            if ot == 'boolean' or ot == 'number' then
                return tostring(o)
            else
                return '"'..tostring(o)..'"'
            end
        end
    else
        if type(o) == 'table' then
            P('{\n')
            for k,v in pairs(o) do
                if type(k) == 'number' then k = '['..k..']' end
                P(string.rep('    ',inden), k, ' = ')
                dump(v, doprint, inden+1)
                P(',\n')
            end
            P(string.rep('    ',inden-1), '}')
        else
            local ot = type(o)
            if ot == 'boolean' or ot == 'number' then
                P(tostring(o))
            else
                P('"'..tostring(o)..'"')
            end
        end
    end
end

_dump = dump
function dump(o, p)
    local r = _dump(o)
    if p then
        echo(r)
        r = ''
    end
    return r
end

local _cache_set = cache_set
local _cache_get = cache_get
local _cache_del = cache_del
function cache_set(k, v, ttl)
    if not k or not v then return end
    local t = type(v)
    if t == 'table' then
        t = 4
        v = fastlz_compress(cmsgpack_safe.pack(v))
    elseif t == 'boolean' then
        t = 3
        v = tostring(v)
    elseif t == 'number' then
        t = 2
        v = tostring(v)
    else
        t = 1
        if #v > 0 then
            v = fastlz_compress(v)
        end
    end

    return _cache_set(k, t..v, ttl)
end
function cache_get(k)
    local t,v = _cache_get(k)
    if t == 4 then
        t = cmsgpack_safe.unpack(fastlz_decompress(v))
    elseif t == 3 then
        t = (v == 'true' and true or false)
    elseif t == 2 then
        t = tonumber(v)
    else
        t = (v and #v > 0) and fastlz_decompress(v) or v
    end
    return t
end

function websocket_accept(loop, on)
    if not loop or not on then return nil, 'usage: websocket_accept(loop function, on function)' end
    if is_websocket() then return end
    if not headers['sec-websocket-key'] then
        header("HTTP/1.1 400 Bad Request")
        die()
    else
        header({"HTTP/1.1 101 Switching Protocols",
                "Upgrade: websocket",
                "Connection: Upgrade",
                "Sec-WebSocket-Accept: "..base64_encode(sha1bin(headers['sec-websocket-key']..'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'))
                })

        upgrade_to_websocket(on)

        newthread(function()
            while 1 do pcall(loop) if check_websocket_close() then break end end
        end)

    end
end

--[[
if not debug.traceback then debug.traceback=function() return '' end end
function print_error(e,h)
    local err = debug.traceback()
    LOG(ERR, 'has error at '.. (e and e or 'unknow! \\')..err:gsub('\n', ' \\'))
    if not h then h = 'HTTP/1.1 503 Server Error' end
    if h:sub(1,4) ~= 'HTTP' then
        h = 'HTTP/1.1 '..h
    end
    header(h, 'Content-Type: text/html')
    echo({'\n<pre><h3>has error at ', (e and e or 'unknow!'), '</h3>', err, '</pre>'})

    __end()
end
]]

function read_post_field_chunk()
    local c,e
    local b = get_boundary()
    if b then
        if not __body_buf then
            c,e = read_request_body()
            if c then
                __body_buf = (__body_buf and __body_buf or '') .. c
            elseif e then
                return
            end
        end

        local p = __body_buf:find(b, 1,1)

        if p then
            p = __body_buf:find("\r\n", p-4, 1)
            if p then
                local r = __body_buf:sub(1, p-1)
                __body_buf = __body_buf:sub(p+2)
                return r
            end
        else
            local r = __body_buf
            __body_buf = nil
            return r
        end

        if e then __body_buf = nil return end
    end
end

function read_post_field_data()
    local r = nil
    local d = read_post_field_chunk()
    local nr = 1

    while d do
        nr = nr + 1
        if nr > 10000 then return end
        r = (r and r or '') .. d
        d = read_post_field_chunk()
    end

    return r
end

function next_post_field()
    local c,e
    local nr = 1
    while 1 do
        nr = nr + 1
        if nr > 100 then return end
        if __body_buf then
            if headers['content-type'] and headers['content-type']:find('x-www-form-urlencoded',1,1) then
                local p
                if e then p = #__body_buf else
                    p = __body_buf:find('&',1,1)
                    if p then p = p -1 end
                end
                if p then
                    local r = __body_buf:sub(1, p)
                    __body_buf = __body_buf:sub(p+2)
                    if #__body_buf == 0 then __body_buf = nil end
                    p = r:find('=',1,1)
                    if p then
                        local k = r:sub(1,p-1)
                        local v = r:sub(p+1)
                        return unescape_uri(k),unescape_uri(v)
                    end
                    return r
                end
            else
                local b = get_boundary()
                if b then
                    local p = __body_buf:find(b, 1,1)
                    if p then
                        p = __body_buf:find("\r\n\r\n", p, 1)
                        if p then
                            local info = __body_buf:sub(1,p-1)
                            local a = info:find('name="',1,1)
                            local b = a and info:find('"',a+6,1) or nil
                            if a and b then
                                __body_buf = __body_buf:sub(p+4)

                                local c = info:find('filename="', a+6, 1)
                                local f,t
                                if c then
                                    f = info:find('"', c+10,1)
                                    if f then
                                        f = info:sub(c+10, f-1)
                                    else
                                        f = nil
                                    end

                                    t = info:find('Content-Type: ',a+6,1)
                                    if t then
                                        t = info:sub(t+13)
                                    else
                                        t = nil
                                    end
                                end
                                return unescape_uri(info:sub(a+6,b-1), true),nil,true,f,t
                            end
                        else
                            return
                        end
                    end
                else
                    return
                end
            end
        end

        c,e = read_request_body()

        if c then
            __body_buf = (__body_buf and __body_buf or '') .. c
        elseif not __body_buf or #__body_buf < 1 then
            return
        end
    end
end

function parse_www_form()
    _POST = {}
    if headers['content-type'] and headers['content-type']:find('x-www-form-urlencoded',1,1) then
        local key,val = next_post_field()
        while key do
            if #key > 2 and key:sub(#key-1) == '[]' then
                key = key:sub(1, #key-2)
                if not _POST[key] then _POST[key] = {} end
                table.insert(_POST[key], val)
            else
                _POST[key] = val
            end

            key,val = next_post_field()
        end
    end
    return _POST
end

__yield = coroutine.yield
_print = print
print = echo

function setcookie(name, value, expire, path, domain, secure, httponly)
    if not name then return false end
    if not value then value = '' expire = time()-86400 end
    local cookie = 'Set-Cookie: '..escape_uri(name)..'='..escape_uri(value)..';'
    if expire then
        cookie = cookie .. ' Expires='..date(expire):fmt('%a, %d %b %Y %T GMT')..';'
    end
    if path then
        cookie = cookie .. ' Path='..path..';'
    end
    if domain then
        cookie = cookie .. ' Domain='..domain..';'
    end
    if secure then
        cookie = cookie .. ' Secure;'
    end
    if httponly then
        cookie = cookie .. ' HttpOnly'
    end

    header(cookie)
end

function jsonrpc_handle(apis)
    header('Content-Type:text/javascript')
    local bs = {}
    local i = 1
    local b,c = read_request_body()
    while b do
        bs[i] = b
        i = i + 1
        b,c = read_request_body()
    end

    local data = json_decode(table.concat(bs))

    if data and data.method then
        local v = '1.0'
        if data.method:find('.', 1, true) then
            v = '2.0'
        end
        if not data.jsonrpc then data.jsonrpc = v end
        local method = explode(data.method, '.')
        if apis[method[1]] and (v == '1.0' or apis[method[1]][method[2]]) then
            print(json_encode(
                    {
                    jsonrpc = data.jsonrpc,
                    result = (v == '1.0' and apis[method[1]] or apis[method[1]][method[2]])(unpack(data.params)),
                    error = null
                    }
                ))
        else
            print(json_encode({jsonrpc = data.jsonrpc,result = null,error = "method not exists!"}))
        end
    else
        print(json_encode({jsonrpc = v,result = null,error = "Agreement Error!"}))
    end
end

_loadstring = loadstring
function loadstring(s,c)
    local f,e = _loadstring(s,c)
    if f then
        setfenv(f, _G)
    end
    return f, e
end

math_floor = math.floor

function loadfile(f)
    if not f then return nil end
    local _f = __root..f
    local p1 = (math_floor(time()/CODE_CACHE_TTL)%2)+1
    local p2 = ((math_floor(time()/CODE_CACHE_TTL)+1)%2)+1
    local f1,e = CODE_CACHE_TTL > 0 and __CodeCache[p1][_f] or nil
    if CODE_CACHE_TTL > 0 and __CodeCacheC[p2] then
        --__CodeCache[p2] = {}
        --__CodeCacheC[p2] = false
    end
    if not f1 then
        f1,e = readfile(f)
        if f1 then
            f1,e = _loadstring(f1, f)
            if f1 then
                setfenv(f1, _G)
            end

            if CODE_CACHE_TTL > 0 then
                __CodeCache[p1][_f] = f1
                __CodeCacheC[p1] = true
            end
        end
    else
        setfenv(f1, _G)
    end

    return f1, e
end

function loadfile(f)
    if not f then return nil end
    local _f = __root..f
    local n = time()
    local f1,e = CODE_CACHE_TTL > 0 and __CodeCache[1][_f] or nil
    if CODE_CACHE_TTL > 0 and __CodeCache[2][_f] and n - __CodeCache[2][_f][1] > CODE_CACHE_TTL then
        if filemtime(f) > __CodeCache[2][_f][2] then
            f1 = nil
        else
            __CodeCache[2][_f][1] = n
        end
    end
    if not f1 then
        f1,e = readfile(f)
        if f1 then
            f1,e = _loadstring(f1, f)
            if f1 then
                setfenv(f1, _G)
            end

            if CODE_CACHE_TTL > 0 then
                __CodeCache[1][_f] = f1
                __CodeCache[2][_f] = {n, filemtime(f)}
            end
        end
    else
        setfenv(f1, _G)
    end

    return f1, e
end

function dofile(f)
    if not f then return nil end
    local f1,e = loadfile(f)
    if f1 then
        f1 = f1()
    else
        return nil, e..' ('..f..')'
    end
    return f1,e
end

_loadtemplate = loadtemplate
function loadtemplate(f)
    local n = time()
    local _f = __root..f
    local f1,e = CODE_CACHE_TTL > 0 and  __CodeCache[1][_f] or nil
    if CODE_CACHE_TTL > 0 and __CodeCache[2][_f] and n - __CodeCache[2][_f][1] > CODE_CACHE_TTL then
        if filemtime(f) > __CodeCache[2][_f][2] then
            f1 = nil
        else
            __CodeCache[2][_f][1] = n
        end
    end

    if not f1 then
        f1,e = _loadtemplate(f)

        if CODE_CACHE_TTL > 0 then
            __CodeCache[1][_f] = f1
            __CodeCache[2][_f] = {n, filemtime(f)}
        end
    end

    if f1 then
        setfenv(f1, _G)
    end
    return f1,e,c
end

function dotemplate(f, ir)
    local f1, e, c = loadtemplate(f, ir)
    if not f1 then
        print(e)
    else
        f1 = f1()
    end
    return f1, e, c
end

_router = router
function router(u,t,_p)
    local f,p = _router(u,t,_p)
    if f then
        f(p)
        return true
    elseif p then
        local e = nil
        if on_start then e = on_start() end
        if not e then
            local r,e = dofile(_p..p)
            if e then
                header('HTTP/1.1 503 Server Error')
                header('Content-Type: text/html; charset=UTF-8')
                print_error(e)
                die()
            end
        end

        return true
    else
        return nil
    end
end

function process(index)
    local r,e = loadfile(index)
    if r and not e then
        r,e = pcall(r)
    end

    if e then
        clear_header()
        header('HTTP/1.1 503 Server Error')
        header('Content-Type: text/html; charset=UTF-8')
        print_error(e)
    end

    pcall(on_shutdown)

    __end()
end
