local io = io
local os = os
local table = table
local pairs = pairs
local ipairs = ipairs
local math = math
local type = type
local tonumber = tonumber
local tostring = tostring
local setmetatable = setmetatable
local cosocket = cosocket
local ngx = ngx
local tcp
local base64_encode = base64_encode
local insert=table.insert
local concat=table.concat
local match = string.match
local char = string.char
local function trim(s)
	return match(s,'^()%s*$') and '' or match(s,'^%s*(.*%S)')
end

if ngx and ngx.say then
	tcp = ngx.socket.tcp
	base64_encode = ngx.encode_base64
	base64_decode = ngx.decode_base64
else
	tcp = cosocket.tcp
end

local zlib = nil
pcall(function()
	-- download from: https://github.com/LuaDist/lzlib
	zlib = require('zlib')
	if not zlib.inflate then zlib = nil end
end)

module(...)

_VERSION = '0.1'

local mt = { __index = _M }

function httprequest(url, params)
	if not params then params = {} end
	local chunk, protocol = url:match('^(([a-z0-9+]+)://)')
	url = url:sub((chunk and #chunk or 0) + 1)

	local sock, err
	if not params['ssl_cert'] then
		sock, err = tcp(protocol=='https')
	else
		sock, err = tcp(params['ssl_cert'], params['ssl_key'], params['ssl_pw'])
	end
	if not sock then
		return nil, err
	end
	
	if not params.pool_size then params.pool_size = 0 end
	if params.pool_size then
		if ngx then
			sock:setkeepalive(60, params.pool_size)
		else
			sock:setkeepalive(params.pool_size)
		end
	end
	if params.timeout then
		sock:settimeout(params.timeout)
	end
	
	local host = url:match('^([^/]+)')
	local hostname, port
	local user,pw
	url = url:sub((host and #host or 0) + 1)
	if host then
		user,pw = host:match('^(%w+):(%w+)@')
		if user then
			host = host:sub(#user+#pw+3)
		end
		hostname = host:match('^([^:/]+)')
		port = host:match(':(%d+)$')
		port = port and port or (protocol=='https' and 443 or 80)
	end
	
	local uri = url
	if not params then params = {} end
	if not params.method then
		if not params.data then
			params.method = 'GET'
		else
			if type(params.data) == 'table' then
				params.method = 'POST'
			else
				params.method = 'PUT'
			end
		end
	end
	if not uri or uri =='' then uri = '/' end
	
	-- connect to server
	local ok, err = sock:connect(hostname, port)
	if not ok then
		sock:close()
		return nil, err
	end

	if params.host then
		hostname = params.host
	end
	
	local contents
	local is_multipart = false
	local is_post = false
	--multipart/form-data
	local boundary = ''
	local send_file_length_sum = 0
	if params.data then
		local k,v
		if type(params.data) == 'table' then
			for k,v in pairs(params.data) do
				if type(v) == 'table' then
					is_multipart = true
					if type(v.file) ~= 'string' then
						v.file:seek('set', 0)
						send_file_length_sum = send_file_length_sum+v.file:seek('end')
						v.file:seek('set', 0)
					end
				end
			end
		end
		if type(params.data) == 'table' then
			contents = {}
			local i = 1
			if not is_multipart then
				is_post = true
				for k,v in pairs(params.data) do
					contents[i] = k..'='..tostring(v)
					i = i+1
				end
				contents = concat(contents, '&')
			else
				boundary = '--'..base64_encode(os.time()..math.random()):sub(1,16)
				for k,v in pairs(params.data) do
					if type(v) == 'string' then
						contents[i] = 'Content-Disposition: form-data; name="'..k..'"\r\n\r\n'..v
					else
						if not v.name then v.name = '' end
						contents[i] = 'Content-Disposition: form-data; name="'..k..'"; filename="'..v.name..
						'"\r\nContent-Type: '..(v.type and v.type or 'application/octet-stream')..';\r\n\r\n'..
						(type(v.file)=='string' and v.file or '')
					end
					i = i+1
				end
				contents = '--'..boundary..'\r\n'..concat(contents, '\r\n--'..boundary..'\r\n')..'\r\n--'..boundary..'--'
			end
		else
			contents = params.data
		end
	end
	local request_headers = {params.method..' '..uri..' HTTP/1.1',
							'Host: '..hostname,
							'User-Agent: lua-http-client(v1)',
							'Connection: '..(params.pool_size > 0 and 'keep-alive' or 'close'),
							'Accept: */*',
							}
	if zlib then
		insert(request_headers, 'Accept-Encoding: gzip,deflate')
	end
	if user and pw then
		insert(request_headers, 'Authorization: Basic ' .. base64_encode(user..':'..pw))
	end
	if params.header then
		if type(params.header) == 'table' then
			local k,v
			for k,v in ipairs(params.header) do
				insert(request_headers, v)
			end
		else
			insert(request_headers, tostring(params.header))
		end
	end
	if contents then
		if is_post then
			insert(request_headers, 'Content-Type: application/x-www-form-urlencoded')
		elseif is_multipart then
			insert(request_headers, 'Content-Type: multipart/form-data; boundary='..boundary)
		end
		insert(request_headers, 'Content-Length: '..#contents+send_file_length_sum)
	end
	
	--send request
	local bytes, err = sock:send(concat(request_headers, '\r\n')..'\r\n\r\n')

	if err then
		sock:close()
		return nil, err
	end

	if send_file_length_sum == 0 then
		if contents then
			bytes, err = sock:send(contents)
			if err then
				sock:close()
				return nil, err
			end
		end
	else
		local i,k,v=1
		bytes, err = sock:send('--'..boundary..'\r\n')

		for k,v in pairs(params.data) do
			if i > 1 then
				bytes, err = sock:send('\r\n--'..boundary..'\r\n')

				if err then
					sock:close()
					return nil, err
				end
			end
			if type(v) == 'string' then
				bytes, err = sock:send('Content-Disposition: form-data; name="'..k..'"\r\n\r\n'..v)

				if err then
					sock:close()
					return nil, err
				end
			else
				if not v.name then v.name = '' end
				local t = type(v.file)
				bytes, err = sock:send('Content-Disposition: form-data; name="'..k..'"; filename="'..v.name..
				'"\r\nContent-Type: '..(v.type and v.type or 'application/octet-stream')..';\r\n\r\n'..
				(t=='string' and v.file or ''))

				if err then
					sock:close()
					return nil, err
				end
				if t~='string' then
					local buf = v.file:read(4096)
					while buf do
						bytes, err = sock:send(buf)

						if err then
							sock:close()
							return nil, err
						end
						buf = v.file:read(4096)
					end
					v.file:close()
				end
			end
			i = i+1
		end
		bytes, err = sock:send('\r\n--'..boundary..'--')
	end
	
	local is_chunked = false
	local gziped = false
	local deflated = false
	local headers = {}
	local i = 1
	local line,err = sock:receive('*l')
	local get_body_length = 0

	while not err do
		if line == '' then break end
		local te = 'transfer-encod' --ing
		local cl = 'content-length'
		local ce = 'content-encodi' --ng
		local _line = line:sub(1, #cl):lower()
		if not is_chunked then
			if #line > #te then
				if _line == te and line:find('chunked') then
					is_chunked = true
				elseif _line == cl then
					get_body_length = tonumber(line:sub(line:find(':')+1, #line))
				end
			end
		end
		if _line == ce and gziped == false and deflated == false then
			if line:find('gzip') then
				gziped = true
			elseif line:find('deflate') then
				deflated = true
			end
		end
		headers[i] = line
		i = i+1
		line,err = sock:receive('*l')
	end

	local bodys = {}
	local body_length = 0
	local buf
	local rterr
	if is_chunked then
		rterr = 'error chunk format!'
		i = 1
		err = nil
		while not err do
			line,err = sock:receive('*l')
			if err then
				break
			end
			
			local read_length = tonumber(line, 16)
			if read_length == 0 then rterr = nil break end
			if not read_length or read_length < 1 then break end
			
			while read_length > 0 do
				local rl = read_length
				if rl > 4096 then rl = 4096 end
				read_length = read_length - rl
				buf,err = sock:receive(rl)
				if buf then
					bodys[i] = buf
					i = i+1
				else
					break
				end
			end
			
			line,err = sock:receive('*l')
		end
	elseif get_body_length > 0 then
		local buf,err = sock:receive(get_body_length < 4096 and get_body_length or 4096)
		i = 1
		while not err do
			bodys[i] = buf
			--print(buf)
			i = i+1
			
			body_length = body_length+#buf
			if body_length >= get_body_length then
				break
			end

			buf,err = sock:receive(get_body_length-body_length < 4096 and get_body_length-body_length or 4096)
			
		end

		if err then
			sock:close()
			sock = nil
		end

		if body_length < get_body_length then
			rterr = 'body length < '..get_body_length
		end
	end
	
	if params.pool_size and sock then
		if ngx then
			sock:setkeepalive(60, params.pool_size)
		else
			sock:close()
		end
	end
	
	if zlib and (gziped or deflated) then
		if deflated and bodys[1]:byte(1) ~= 120 and bodys[1]:byte(1) ~= 156 then
			bodys[1] = char(120,156) .. bodys[1]
		end

		i = 1
		local maxi = #bodys
		local stream = zlib.inflate(function()
				i=i+1
				if i > maxi+1 then return nil end
				return bodys[i-1]
		end)
		bodys = stream:read('*a')
		stream:close()
	end
	
	if type(bodys) == 'table' then bodys = concat(bodys) end
	
	--return bodys, headers, rterr
	local res = {}
	res.body = bodys
	res.status = 0
	if headers and headers[1] then
		local i = headers[1]:find(' ', 1, true)
		if i then
			local e = headers[1]:find(' ', i+1, true)
			res.status = e and tonumber(headers[1]:sub(i+1, e)) or 0
		else
			res.status = 0
		end

		local header = {}
		for k,v in ipairs(headers) do
			local i = v:find(':', 1, true)
			if i then
				header[v:sub(1,i-1):lower()] = trim(v:sub(i+1))
			end
		end

		res.header = header
	end

	return res, rterr
end

local class_mt = {
	-- to prevent use of casual module global variables
	__newindex = function (table, key, val)
		error('attempt to write to undeclared variable "' .. key .. '"')
	end
}

setmetatable(_M, class_mt)
