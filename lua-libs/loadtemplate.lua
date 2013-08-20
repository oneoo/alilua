local string=string
local os=os
local io=io
local loadstring=loadstring
local concat=table.concat
local _G=_G
local fopen=io.open
local ceil=math.ceil
local time=os.time
local CODE_CACHE_TTL=CODE_CACHE_TTL
local print=print

local bs = ('{'):byte(1)
local be = ('}'):byte(1)
local ab = ('='):byte(1)
local _r = ('\r'):byte(1)
local _n = ('\n'):byte(1)
local bba = ('['):byte(1)
local bbb = (']'):byte(1)
	
module(...)

_VERSION = '0.1'

function string.findlast(s, pattern, plain)
	local curr = 0
	repeat
		local next = s:find(pattern, curr + 1, plain)
		if (next) then curr = next end
	until (not next)
	if (curr > 0) then
		return curr
	end	
end

local template_cache = {{},{}}
if not CODE_CACHE_TTL then CODE_CACHE_TTL = 60 end

function loadtemplate(f, is_return, init)
	local _cache = nil
	if CODE_CACHE_TTL > 0 then
		if template_cache[ceil((time()/CODE_CACHE_TTL)+1)%2+1]['h'] then template_cache[ceil((time()/CODE_CACHE_TTL)+1)%2+1] = {} end
		_cache = template_cache[ceil((time()/CODE_CACHE_TTL))%2+1]
	end
	local _f = f..(is_return and '#' or '')
	if _cache and _cache[_f] then return _cache[_f]() end
	
	local fpath = '.'
	local i = f:findlast('/', true)
	if (i) then
		if i > 1 then i = i - 1 end
		fpath = f:sub(1, i)
	end
	
	--print('loadtemplate', f)
	
	local html = ''
	local __f = fopen(f)
	if __f then
		html = __f:read('*all')
		__f:close()
	else
		return false, 'Template file ['..f..'] not found!'
	end

	local _codes = {}
	if not is_return then
		_codes[1] = 'local __P=print '
	else
		if not init then
			_codes[1] = '__HTMLS,__HTMLC, __P={},1, function(s) __HTMLS[__HTMLC]=s __HTMLC=__HTMLC+1 end '
		else
			_codes[1] = ''
		end
	end
	local _code_i = 2
	local len = #html
	local i = 1
	local _bs = 0
	local inb = false
	local has_q = false
	for i = 1,len,2 do
		if inb == false and html:byte(i) == bs then -- {{
			local __bs = _bs
			if html:byte(i+1) == bs then
				_bs = i
				inb = true
			elseif html:byte(i-1) == bs then
				_bs = i-1
				inb = true
			end
		
			if _bs > __bs then
				if has_q then
					_codes[_code_i] = '__P[=['..html:sub(__bs, _bs-1)..(html:byte(_bs-1)==bbb and ' ' or '')..']=] '
				else
					_codes[_code_i] = '__P[['..html:sub(__bs, _bs-1)..(html:byte(_bs-1)==bbb and ' ' or '')..']] '
				end
				_code_i = _code_i+1
			end
			
			if inb then
				has_q = false
			end
		elseif inb == true and html:byte(i) == be then -- }}
			local _be = 0
			if html:byte(i+1) == be then
				_be = i+1
			elseif html:byte(i-1) == be then
				_be = i
			end
			
			if _be > 0 then -- processing block
				if html:byte(_bs+2) == ab then
					_codes[_code_i] = '__P('..html:sub(_bs+3, _be-2)..') '
				elseif _be - _bs > 8 and html:sub(_bs+2, _bs+8) == 'include' then
					local k,e = 0,0
					if html:byte(_bs+10) == (' '):byte(1) then k = 1 end
					if html:byte(_be+-2) == (' '):byte(1) then e = 1 end
					_codes[_code_i] = 'local _r,_e = dotemplate("'..fpath..'/'..html:sub(_bs+10+k, _be-2-e)..'", '..(is_return and 'true, true' or 'false')..') if _e then __P(_e) end '
				else
					_codes[_code_i] = html:sub(_bs+2, _be-2)..' '
				end

				_code_i = _code_i+1
				_bs = _be+1
				
				inb = false
			end
		elseif html:byte(i) == bbb then
			if html:byte(i+1) == bbb then
				has_q = true
			elseif html:byte(i-1) == bbb then
				has_q = true
			end
		end
	end
	
	if _bs < len then
		_codes[_code_i] = '__P[=['..html:sub(_bs, len)..(html:byte(len-1)==bbb and ' ' or '')..']=] '
		_code_i = _code_i+1
	end
	
	--print(concat(_codes))
	_codes[_code_i] = ' return __HTMLS'
	_codes = concat(_codes)
	local codes, err = loadstring('return function() '.._codes..' end', _f)
	if _cache then _cache[_f] = codes _cache['h'] = true end
	if not codes then codes = nil return nil, err end
	
	return codes()
end

_G.loadtemplate = loadtemplate
return loadtemplate
