local mysql = require('mysql')
local cjson = require('cjson')
local memcached = require('memcached')
local redis = require('redis')
local date = require('date')
local loadtemplate = require('loadtemplate')
local httpclient = require('httpclient')

local md5 = function(s) return crypto.evp.digest('md5', s) end
function string:trim() return self:gsub('^%s*(.-)%s*$', '%1') end
function trim(s) return s:trim() end
function string:startsWith(s, i) return _G['string-utils'].startsWith(self, s, i) end
function string:endsWith(s, i) return _G['string-utils'].endsWith(self, s, i) end
local explode = _G['string-utils'].explode
local implode = table.concat
local escape = _G['string-utils'].escape
local escape_uri = _G['string-utils'].escape_uri
local unescape_uri = _G['string-utils'].unescape_uri
local base64_encode = _G['string-utils'].base64_encode
local base64_decode = _G['string-utils'].base64_decode
local strip = _G['string-utils'].strip
local iconv = _G['string-utils'].iconv
local json_encode = cjson.encode
local json_decode = cjson.decode
function printf(s, ...) print(s:format(...)) end
function sprintf(s, ...) return (s:format(...)) end

function dump(P, o, doprint, inden)
	--local P=--io.write
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
				dump(P, v, doprint, inden+1)
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
--function cache_del(k)return _cache_del(k)end

function cacheTable(ttl)
	if not ttl or type(ttl) ~= 'number' or ttl < 2 then
		return nil
	end
	ttl = ttl/2
	local t = {{},{},{}}
	local proxy = {}
	local mt = {
		__index = function (t1,k)
			local p = math.floor(os.time()/ttl)
			if t[(p-2)%3+1].__has then t[(p-2)%3+1] = {} end
			return t[(p)%3+1][k]
		end,
		__newindex = function (t1,k,v)
			local p = math.floor(os.time()/ttl)
			t[p%3+1][k] = v
			t[(p+1)%3+1][k] = v
			t[p%3+1].__has = 1
		end
	}
	setmetatable(proxy, mt)
	return proxy
end
local CodeCache = cacheTable(60)
local FileExistsCache = cacheTable(10)

host_route = {}
setmetatable(host_route, {
	__index = function(t, _k)
		for k,v in pairs(t) do
			if _k == k then return v end
		end
		for k,v in pairs(t) do
			if _k:match(k) then return v end
		end
		return './alilua-index.lua'
	end
})

if _G.jit then debug.traceback=function() return '' end end
function print_error(__epd__, e)
	clear_header(__epd__)
	header(__epd__, {'HTTP/1.1 503 Server Error',
					'Content-Type: text/html'})
	echo(__epd__, {'<pre>',
					debug.traceback(),
					'has error at ',
					(e and e or 'unknow!'),
					'</pre>'})
	die(__epd__)
end

--setcookie( name [, value [, expire = 0 [, path [, domain [, bool secure [, bool httponly]]]]]]
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
	return cookie
end
function session()
end

local env = {io=io,_print=print, math=math, string=string,tostring=tostring,tonumber=tonumber, sleep=sleep,pairs=pairs,ipairs=ipairs,type=type,debug=debug,date=date,pcall=pcall,call=call,table=table,unpack=unpack,
			httpclient=httpclient,
			cache_set=cache_set,cache_get=cache_get,cache_del=cache_del,random_string=random_string,
			cosocket=cosocket,allthreads=allthreads,newthread=newthread,coroutine_wait=coroutine_wait,swop=swop,time=time,longtime=longtime,mysql=mysql,json_encode=json_encode,json_decode=json_decode,memcached=memcached,redis=redis,coroutine=coroutine,
			is_dir=libfs.is_dir,is_file=libfs.is_file,mkdir=libfs.mkdir,rmdir=libfs.rmdir,readdir=libfs.readdir,stat=libfs.stat,unlink=libfs.unlink,file_exists=file_exists,crypto=crypto,iconv=iconv,
			trim=trim,strip=strip,explode=explode,implode=implode,escape=escape,escape_uri=escape_uri,unescape_uri=unescape_uri,
			nl2br=_G['string-utils'].nl2br,
			base64_encode=base64_encode,base64_decode=base64_decode,printf=printf,sprintf=sprintf,
			hook=hook,get_hooks=get_hooks,host_route=host_route}
local env_pairs = {}
local k,v,i=nil,nil,1 for k,v in pairs(env) do env_pairs[i]={k=k,v=v} i=i+1 end env_pairs.c = i-1
function get_env() local t,i,k,v={},nil,nil,nil for i=1,env_pairs.c do t[env_pairs[i].k]=env_pairs[i].v end return t end

function main(__epd, headers, _GET, _COOKIE, _POST)
	local box = {}--get_env()
	local __epd__ = __epd
	box.headers = headers
	box._GET = _GET
	box._POST = _POST
	box.echo = function(...) check_timeout(__epd__) echo(__epd__, ...) end
	box.print = box.echo
	box.printf = function(...) check_timeout(__epd__) echo(__epd__, sprintf(...)) end
	box.dump = function(...) dump(box.echo, ...) end
	box.sendfile = function(f) sendfile(__epd__, f) end
	box.header = function(s) header(__epd__, s) end
	box.loadtemplate = function(f) if not f:startsWith(box.__root) then f = box.__root .. f end return loadtemplate(f) end
	box.dotemplate = function(f, ir)
		local f, e, c = box.loadtemplate(f, ir)
		if f then
			setfenv(f, box)
			return pcall(f)
		else
			box.print(e)
		end
		return f, e, c
	end
	box.setcookie = function(name, value, expire, path, domain) header(__epd__, setcookie(name, value, expire, path, domain)) end
	box.session = function()
		local t = nil
		if _COOKIE['_SESSION'] then
			t = cache_get(_COOKIE['_SESSION'])
		end
		if not t then
			if not _COOKIE['_SESSION'] then
				_COOKIE['_SESSION'] = random_string(48)
			end
			box.setcookie('_SESSION', _COOKIE['_SESSION'])
			t = {}
		end
		setmetatable(t, {
			__newindex = function(t,k,v)
				rawset(t, k, v)
				box.__session = t
			end
		})
		return t
	end
	box.check_timeout = function(s) check_timeout(__epd__) end
	box.clear_header = function() clear_header(__epd__) end
	box.die = function(...)
		if box.__session then
			cache_set(_COOKIE['_SESSION'], box.__session, 1800)
		end
		if ... then box.echo(...) end
		die(__epd__)
	end
	box.get_post_body = function() return get_post_body(__epd__) end
	box.print_error = function(e) print_error(__epd__, e) end
	box.file_exists = function(f) if not f:startsWith(box.__root) then f = box.__root .. f end return file_exists(f) end
	box.sendfile = function(f) if not f:startsWith(box.__root) then f = box.__root .. f end return sendfile(__epd__, f) end
	box.loadfile = function(f)
		if not f:startsWith(box.__root) then f = box.__root .. f end
		local r = CodeCache[f]
		local err
		if not r then
			r = cache_get('__cache_'..f)
			if not r then
				r, err = loadfile(f)
				if r then cache_set('__cache_'..f, string.dump(r)) end
			else
				r, err = loadstring(r)
			end
			CodeCache[f] = r
		end
		if r then setfenv(r, box) end
		return r, err
	end
	box.dofile = function(f) local r,e = box.loadfile(f) if r then setfenv(r, box) return r() else error(e) end end
	
	box.__fun_hooks = {}
	box.__hooked = {}
	box.hook = function(f, h, i)
		if type(f) ~= 'function' or type(h) ~= 'function' then return false end
		if not box.__hooked[f] then return false end
		f = box.__hooked[f]
		if not box.__fun_hooks[f] then box.__fun_hooks[f] = {} end
		if i then
			table.insert(box.__fun_hooks[f], i, h)
		else
			table.insert(box.__fun_hooks[f], h)
		end
	end
	box.get_hooks = function(f)
		return box.__fun_hooks[box.__hooked[f]]
	end

	setmetatable(box, {
		__index = function(t, k)
			v = rawget(t, k)
			if not v then v = env[k] end
			return v
		end,
		-- for hooker
		__newindex = function(t,k,v)
			if type(v) == 'function' then
				local _v = function(...)
					local rts = {v(...)}
					if box.__fun_hooks[v] then
						for k,v in ipairs(box.__fun_hooks[v]) do
							local _rts = {pcall(v, unpack(rts))}
							if #_rts > 1 and _rts[1] == true then
								table.remove(_rts, 1)
								rts = _rts
							end
						end
					end
					return unpack(rts)
				end
				box.__hooked[_v] = v
				return rawset(t, k, _v)
			end
			rawset(t, k, v)
		end
	})
	
	if not CodeCache['host-route.lua'] then
		CodeCache['host-route.lua'], e = loadfile('host-route.lua')
		if e then
			box.header('HTTP/1.1 503 Server Error')
			box.echo(e)
			box.die()
			return
		end
	end
	CodeCache['host-route.lua']()
	setfenv(process, box)
	process(headers, _GET, _COOKIE, _POST)
end
