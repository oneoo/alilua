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
local iconv_strlen = _G['string-utils'].iconv_strlen
local iconv_substr = _G['string-utils'].iconv_substr
local json_encode = cjson.encode
local json_decode = cjson.decode
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
		local t = {}
		local mt = {__newindex = function (t1,k,v) return false end}
		setmetatable(t, mt)
		return t
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
if not CODE_CACHE_TTL then CODE_CACHE_TTL = 60 end
local CodeCache = cacheTable(CODE_CACHE_TTL)
local FileExistsCache = cacheTable(CODE_CACHE_TTL/2)

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
	local err = debug.traceback()
	errorlog(__epd__, 'has error at '..
					(e and e or 'unknow!')..'\\n\\n'..err:gsub('\n', '\\n'))
	clear_header(__epd__)
	header(__epd__, {'HTTP/1.1 503 Server Error',
					'Content-Type: text/html'})
	echo(__epd__, {'<pre><b>has error at ',
					(e and e or 'unknow!'),
					'</b><br/>',
					err,
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

function jsonrpc_handle(data, apis)
	header('Content-Type:text/javascript')
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

function readfile(f)
	local f,e = io.open(f)
	local r
	if f then
		r = f:read('*all')
		f:close()
	end
	return r,e
end

local env = {null=null,errorlog=errorlog,error=error,io=io,_print=print, math=math, string=string,tostring=tostring,tonumber=tonumber, sleep=sleep,pairs=pairs,ipairs=ipairs,type=type,debug=debug,date=date,pcall=pcall,call=call,table=table,unpack=unpack,
			httpclient=httpclient,_jsonrpc_handle=jsonrpc_handle,
			cache_set=cache_set,cache_get=cache_get,cache_del=cache_del,random_string=random_string,
			cosocket=cosocket,allthreads=allthreads,newthread=newthread,coroutine_wait=coroutine_wait,swop=swop,time=time,longtime=longtime,mysql=mysql,json_encode=json_encode,json_decode=json_decode,memcached=memcached,redis=redis,coroutine=coroutine,
			is_dir=libfs.is_dir,is_file=libfs.is_file,mkdir=libfs.mkdir,rmdir=libfs.rmdir,readdir=libfs.readdir,stat=libfs.stat,unlink=libfs.unlink,_file_exists=file_exists,crypto=crypto,iconv=iconv,iconv_strlen=iconv_strlen,iconv_substr=iconv_substr,
			trim=trim,strip=strip,explode=explode,implode=implode,escape=escape,escape_uri=escape_uri,unescape_uri=unescape_uri,
			nl2br=_G['string-utils'].nl2br,
			base64_encode=base64_encode,base64_decode=base64_decode,printf=printf,sprintf=sprintf,
			hook=hook,get_hooks=get_hooks,host_route=host_route,_echo=echo,_die=die,_sendfile=sendfile,_header=header,_clear_header=clear_header,_loadfile=loadfile,_dofile=dofile,_loadtemplate=loadtemplate,_setfenv=setfenv,_setmetatable=setmetatable,_rawset=rawset,pcall=pcall,check_timeout=check_timeout,_setcookie=setcookie,_get_post_body=get_post_body,_dump=dump,print_error=print_error,CodeCache=CodeCache,FileExistsCache=FileExistsCache,_readfile=readfile,_loadstring=loadstring}


function initbox()
	function echo(...)
		check_timeout(__epd__)
		return _echo(__epd__, ...)
	end
	print = echo
	
	function printf(...)
		check_timeout(__epd__)
		return _echo(__epd__, sprintf(...))
	end
	
	function file_exists(f)
		if not f:startsWith(__root) then f = __root .. f end
		local exists = FileExistsCache[f]
		if not exists then
			exists = _file_exists(f)
			FileExistsCache[f] = exists
		end
		return exists
	end
	
	function sendfile(f)
		if not f:startsWith(__root) then f = __root .. f end
		return _sendfile(__epd__, f)
	end
	
	function header(s)
		return _header(__epd__, s)
	end
	
	function clear_header()
		return _clear_header(__epd__)
	end
	
	function die(...)
		if ... then
			echo(...)
		end
		if __session then
			cache_set(_COOKIE['_SESSION'], __session, 1800)
		end
		_die(__epd__)
	end
	
	function get_post_body() return _get_post_body(__epd__) end
	
	function setcookie(name, value, expire, path, domain)
		_header(__epd__, _setcookie(name, value, expire, path, domain))
	end
	
	function session()
		local t = nil
		if _COOKIE['_SESSION'] then
			t = cache_get(_COOKIE['_SESSION'])
		end
		if not t then
			if not _COOKIE['_SESSION'] then
				_COOKIE['_SESSION'] = random_string(48)
			end
			setcookie('_SESSION', _COOKIE['_SESSION'])
			t = {}
		end
		_setmetatable(t, {
			__newindex = function(t,k,v)
				_rawset(t, k, v)
				__session = t
			end
		})
		return t
	end
	
	function dump(o, p)
		local r = _dump(o)
		if p then
			echo(r)
			r = ''
		end
		return r
	end
	
	function loadstring(s, c)
		local f,e = _loadstring(s,c)
		if f then
			_setfenv(f, __box__)
		end
		return f, e
	end
	
	function loadfile(f)
		if not f:startsWith(__root) then f = __root .. f end
		local f1,e = CodeCache[f]
		if not f1 then
			f1,e = _readfile(f)
			if f1 then
				f1,e = loadstring('return function() ' .. f1 .. ' end', f)
				CodeCache[f] = f1
			end
		end
		
		if f1 then
			_setfenv(f1, __box__)
			return f1()
		end
		
		return nil, e
	end
	
	function dofile(f)
		local f1,e = loadfile(f)
		if f1 then
			return f1()
		else
			error(e)
		end
		return nil,e
	end
	
	function loadtemplate(f)
		if not f:startsWith(__root) then f = __root .. f end
		return _loadtemplate(f)
	end
	
	function dotemplate(f, ir)
		local f1, e, c = loadtemplate(f, ir)
		if f1 then
			_setfenv(f1, __box__)
			f1()
			f1 = true
		else
			print(e)
		end
		return f1, e, c
	end
	
	function jsonrpc_handle(...)
		_setfenv(_jsonrpc_handle, __box__)
		_jsonrpc_handle(...)
	end
	
	__fun_hooks = {}
	__hooked = {}
	function hook(f, h, i)
		if type(f) ~= 'function' or type(h) ~= 'function' then return false end
		if not __hooked[f] then return false end
		f = __hooked[f]
		if not __fun_hooks[f] then __fun_hooks[f] = {} end
		if i then
			table.insert(__fun_hooks[f], i, h)
		else
			table.insert(__fun_hooks[f], h)
		end
	end
	function get_hooks(f)
		return __fun_hooks[__hooked[f]]
	end
end

if not HOST_ROUTE then HOST_ROUTE = 'host-route.lua' end
function main(__epd, headers, _GET, _COOKIE, _POST)
	local box = {}--get_env()
	box.__epd__ = __epd
	box.__box__ = box
	box.headers = headers
	box._GET = _GET
	box._POST = _POST
	box._COOKIE = _COOKIE
	if not box._COOKIE then box._COOKIE = {} end

	setfenv(initbox, box)
	initbox()

	setmetatable(box, {
		__index = function(t, k)
			v = rawget(t, k)
			if not v then v = env[k] end
			return v
		end,
		-- for hooker
		__newindex = function(t,k,v)
			if k == '__epd__' or k == '__box__' then return false end
			if type(v) == 'function' then
				local _v = function(...)
					local rts = {v(...)}
					if t.__fun_hooks[v] then
						for k,v in ipairs(t.__fun_hooks[v]) do
							local _rts = {pcall(v, unpack(rts))}
							if #_rts > 1 and _rts[1] == true then
								table.remove(_rts, 1)
								rts = _rts
							end
						end
					end
					return unpack(rts)
				end
				t.__hooked[_v] = v
				return rawset(t, k, _v)
			end
			rawset(t, k, v)
		end
	})
	
	local f, e = CodeCache[HOST_ROUTE]
	if not f then
		f, e = loadfile(HOST_ROUTE)
		CodeCache[HOST_ROUTE] = f
		if e then
			box.header('HTTP/1.1 503 Server Error')
			box.echo('host route error!', e)
			box.die()
			return
		end
	end
	f()
	setfenv(process, box)
	process(headers, _GET, _COOKIE, _POST)
end
