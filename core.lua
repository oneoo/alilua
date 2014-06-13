mysql = require('mysql')
cjson = require('cjson.safe')
memcached = require('memcached')
redis = require('redis')
date = require('date')
loadtemplate = require('loadtemplate')
httpclient = (require "httpclient").httprequest
llmdb = require('llmdb-client')
cmsgpack = require('cmsgpack')

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
    local t = type(v)
    if t == 'table' then
        t = 4
        v = fastlz_compress(json_encode(v))
    elseif t == 'boolean' then
        t = 3
        v = tostring(v)
    elseif t == 'number' then
        t = 2
        v = tostring(v)
    else
        t = 1
        v = fastlz_compress(v)
    end

    return _cache_set(k, t..v, ttl)
end
function cache_get(k)
    local t,v = _cache_get(k)
    if t == 4 then
        t = json_decode(fastlz_decompress(v))
    elseif t == 3 then
        t = (v == 'true' and true or false)
    elseif t == 2 then
        t = tonumber(v)
    else
        t = fastlz_decompress(v)
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

        upgrade_to_websocket(function(data, typ) newthread(function() on(data, typ) end) end)

        while 1 do pcall(loop) if check_websocket_close() then break end end

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

__yield = coroutine.yield
_print = print
print = echo

function setcookie(name, value, expire, path, domain)
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

_file_exists = file_exists
function file_exists(f)
    if not f then return nil end
    local _f = __root..f
    local exists = FileExistsCache[_f]
    if exists == nil then
        exists = _file_exists(f)
        FileExistsCache[_f] = exists
    end
    return exists
end

_loadstring = loadstring
function loadstring(s,c)
    local f,e = _loadstring(s,c)
    if f then
        setfenv(f, _G)
    end
    return f, e
end

function loadfile(f)
    if not f then return nil end
    local _f = __root..f
    local f1,e = CodeCache[_f]
    if not f1 then
        f1,e = readfile(f)
        if f1 then
            f1,e = loadstring(f1, f)
            CodeCache[_f] = f1
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
    local _f = __root..f
    local f1,e = CodeCache[_f]
    if not f1 then
        f1,e = _loadtemplate(f)
        CodeCache[_f] = f1
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
function router(u,t,p)
    local f,p = _router(u,t,p)
    if f then
        f(p)
        return true
    elseif p then
        local r,e = dofile(p)
        if e then
            print_error(e)
            return nil
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

