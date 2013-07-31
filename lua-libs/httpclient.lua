local io = io
local os = os
local table = table
local pairs = pairs
local ipairs = ipairs
local rawset = rawset
local math = math
local type = type
local tonumber = tonumber
local tostring = tostring
local setmetatable = setmetatable
local cosocket = cosocket
local ngx = ngx
local tcp
local base64_encode = base64_encode

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

local function httprequest(url, params)
	local chunk, protocol = url:match('^(([a-z0-9+]+)://)')
	url = url:sub((chunk and #chunk or 0) + 1)

	local sock, err = tcp(protocol=='https')
	if not sock then
		return nil, err
	end
	if params.pool_size then
		if ngx then
			sock:setkeepalive(60, params.pool_size)
		else
			sock:setkeepalive(params.pool_size)
		end
	end
	if params.timeout then
		sock:settimeout(params.timeout/(ngx and 1 or 1000))
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
		return nil, err
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
					rawset(contents, i, k..'='..tostring(v))
					i = i+1
				end
				contents = table.concat(contents, '&')
			else
				boundary = '--'..base64_encode(os.time()..math.random()):sub(1,16)
				for k,v in pairs(params.data) do
					if type(v) == 'string' then
						rawset(contents, i, 'Content-Disposition: form-data; name="'..k..'"\r\n\r\n'..v)
					else
						if not v.name then v.name = '' end
						rawset(contents, i, 'Content-Disposition: form-data; name="'..k..'"; filename="'..v.name..
						'"\r\nContent-Type: '..(v.type and v.type or 'application/octet-stream')..';\r\n\r\n'..
						(type(v.file)=='string' and v.file or ''))
					end
					i = i+1
				end
				contents = '--'..boundary..'\r\n'..table.concat(contents, '\r\n--'..boundary..'\r\n')..'\r\n--'..boundary..'--'
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
		table.insert(request_headers, 'Accept-Encoding: gzip,deflate')
	end
	if user and pw then
		table.insert(request_headers, 'Authorization: Basic ' .. base64_encode(user..':'..pw))
	end
	if params.header then
		if type(params.header) == 'table' then
			local k,v
			for k,v in ipairs(params.header) do
				table.insert(request_headers, v)
			end
		else
			table.insert(request_headers, tostring(params.header))
		end
	end
	if contents then
		if is_post then
			table.insert(request_headers, 'Content-Type: application/x-www-form-urlencoded')
		elseif is_multipart then
			table.insert(request_headers, 'Content-Type: multipart/form-data; boundary='..boundary)
		end
		table.insert(request_headers, 'Content-Length: '..#contents+send_file_length_sum)
	end
	
	--send request
	local bytes, err = sock:send(table.concat(request_headers, '\r\n')..'\r\n\r\n')
	--print(table.concat(request_headers, '\r\n')..'\r\n\r\n')
	if err then
		sock:close()
		return nil, err
	end
	
	--send body (if exists)
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

		if err then
			sock:close()
			return nil, err
		end
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

		if err then
			sock:close()
			return nil, err
		end
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
		rawset(headers, i, line)
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
			buf,err = sock:receive(read_length)
			if buf then
				rawset(bodys, i, buf)
				i = i+1
			end
			line,err = sock:receive('*l')
		end
	else
		local buf,err = sock:receive('*a')
		i = 1
		while not err do
			rawset(bodys, i, buf)
			--print(buf)
			i = i+1
			
			body_length = body_length+#buf
			if body_length >= get_body_length then
				break
			end
			
			buf,err = sock:receive('*a')
			
		end

		if body_length < get_body_length then
			rterr = 'body length < '..get_body_length
		end
	end
	
	sock:close()
	
	if zlib then
		if gziped then
			i = 1
			local maxi = #bodys
			local stream = zlib.inflate(function()
					i=i+1
					if i > maxi+1 then return nil end
					return bodys[i-1]
			end)
			bodys = stream:read('*a')
			stream:close()
		elseif deflated then
			bodys = zlib.decompress(table.concat(bodys),-8)
		end
	end
	
	if type(bodys) == 'table' then bodys = table.concat(bodys) end
	
	return bodys, headers, rterr
end

return httprequest