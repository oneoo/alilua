-- Copyright (C) 2012 Yichun Zhang (agentzh)


local bit = require "bit"
local sub = string.sub
local tcp --= ngx.socket.tcp
local strbyte = string.byte
local strchar = string.char
local strfind = string.find
local strrep = string.rep
local null = null
local band = bit.band
local bxor = bit.bxor
local bor = bit.bor
local lshift = bit.lshift
local rshift = bit.rshift
local tohex = bit.tohex
local sha1 --= ngx.sha1_bin
local concat = table.concat
local unpack = unpack
local setmetatable = setmetatable
local error = error
local tonumber = tonumber
local print = print

-- add by oneoo
local ngx = ngx
local sha1bin = sha1bin
local type = type
local pairs = pairs
local ipairs = ipairs
local tonumber = tonumber
local tostring = tostring
local _mysql_quote = escape
if not _mysql_quote and ngx then
    _mysql_quote = function(_)
        _ = string.gsub(_, "\\", "\\\\")
        _ = string.gsub(_, "\"", "\\\"")
        _ = string.gsub(_, "\n", "\\n")
        _ = string.gsub(_, "\r", "\\r")
        _ = string.gsub(_, "\t", "\\t")
        return _
    end
end
if not _mysql_quote then _mysql_quote = function(s) return s end end
function quote_sql_var(s)
    if type(s) == 'string' then
        return _mysql_quote(s)
    end
    return s
end
if ngx then
    tcp = ngx.socket.tcp
    null = ngx.null
    sha1 = ngx.sha1_bin
else
    tcp = cosocket.tcp
    sha1 = sha1bin
    if not null then null = false end
end
-- end

local _M = { _VERSION = '0.13' }


-- constants

local STATE_CONNECTED = 1
local STATE_COMMAND_SENT = 2

local COM_QUERY = 0x03

local SERVER_MORE_RESULTS_EXISTS = 8

-- 16MB - 1, the default max allowed packet size used by libmysqlclient
local FULL_PACKET_SIZE = 16777215


local mt = { __index = _M }


-- mysql field value type converters
local converters = {}

for i = 0x01, 0x05 do
    -- tiny, short, long, float, double
    converters[i] = tonumber
end
-- converters[0x08] = tonumber  -- long long
converters[0x09] = tonumber  -- int24
converters[0x0d] = tonumber  -- year
converters[0xf6] = tonumber  -- newdecimal


local function _get_byte2(data, i)
    local a, b = strbyte(data, i, i + 1)
    return bor(a, lshift(b, 8)), i + 2
end


local function _get_byte3(data, i)
    local a, b, c = strbyte(data, i, i + 2)
    return bor(a, lshift(b, 8), lshift(c, 16)), i + 3
end


local function _get_byte4(data, i)
    local a, b, c, d = strbyte(data, i, i + 3)
    return bor(a, lshift(b, 8), lshift(c, 16), lshift(d, 24)), i + 4
end


local function _get_byte8(data, i)
    local a, b, c, d, e, f, g, h = strbyte(data, i, i + 7)

    -- XXX workaround for the lack of 64-bit support in bitop:
    local lo = bor(a, lshift(b, 8), lshift(c, 16), lshift(d, 24))
    local hi = bor(e, lshift(f, 8), lshift(g, 16), lshift(h, 24))
    return lo + hi * 4294967296, i + 8

    -- return bor(a, lshift(b, 8), lshift(c, 16), lshift(d, 24), lshift(e, 32),
               -- lshift(f, 40), lshift(g, 48), lshift(h, 56)), i + 8
end


local function _set_byte2(n)
    return strchar(band(n, 0xff), band(rshift(n, 8), 0xff))
end


local function _set_byte3(n)
    return strchar(band(n, 0xff),
                   band(rshift(n, 8), 0xff),
                   band(rshift(n, 16), 0xff))
end


local function _set_byte4(n)
    return strchar(band(n, 0xff),
                   band(rshift(n, 8), 0xff),
                   band(rshift(n, 16), 0xff),
                   band(rshift(n, 24), 0xff))
end


local function _from_cstring(data, i)
    local last = strfind(data, "\0", i, true)
    if not last then
        return nil, nil
    end

    return sub(data, i, last), last + 1
end


local function _to_cstring(data)
    return data .. "\0"
end


local function _to_binary_coded_string(data)
    return strchar(#data) .. data
end


local function _dump(data)
    local bytes = {}
    local len = #data
    for i = 1, len do
        bytes[i] = strbyte(data, i)
    end
    return concat(bytes, " ")
end


local function _dumphex(data)
    local bytes = {}
    local len = #data
    for i = 1, len do
        bytes[i] = tohex(strbyte(data, i), 2)
    end
    return concat(bytes, " ")
end


local function _compute_token(password, scramble)
    if password == "" then
        return ""
    end

    local stage1 = sha1(password)
    local stage2 = sha1(stage1)
    local stage3 = sha1(scramble .. stage2)
    local bytes = {}
    local n = #stage1
    for i = 1, n do
         bytes[i] = strchar(bxor(strbyte(stage3, i), strbyte(stage1, i)))
    end

    return concat(bytes)
end


local function _send_packet(self, req, size)
    local sock = self.sock

    self.packet_no = self.packet_no + 1

    --print("packet no: ", self.packet_no)

    local packet = _set_byte3(size) .. strchar(self.packet_no) .. req

    --print("sending packet...")

    return sock:send(packet)
end


local function _recv_packet(self)
    local sock = self.sock

    local data, err = sock:receive(4) -- packet header
    if not data then
        return nil, nil, "failed to receive packet header: " .. err
    end

    --print("packet header: ", _dump(data))

    local len, pos = _get_byte3(data, 1)

    --print("packet length: ", len)

    if len == 0 then
        return nil, nil, "empty packet"
    end

    if len > self._max_packet_size then
        return nil, nil, "packet size too big: " .. len
    end

    local num = strbyte(data, pos)

    --print("recv packet: packet no: ", num)

    self.packet_no = num

    data, err = sock:receive(len)

    --print("receive returned")

    if not data then
        return nil, nil, "failed to read packet content: " .. err
    end

    --print("packet content: ", _dump(data))
    --print("packet content (ascii): ", data)

    local field_count = strbyte(data, 1)

    local typ
    if field_count == 0x00 then
        typ = "OK"
    elseif field_count == 0xff then
        typ = "ERR"
    elseif field_count == 0xfe then
        typ = "EOF"
    elseif field_count <= 250 then
        typ = "DATA"
    end

    return data, typ
end


local function _from_length_coded_bin(data, pos)
    local first = strbyte(data, pos)

    --print("LCB: first: ", first)

    if not first then
        return nil, pos
    end

    if first >= 0 and first <= 250 then
        return first, pos + 1
    end

    if first == 251 then
        return null, pos + 1
    end

    if first == 252 then
        pos = pos + 1
        return _get_byte2(data, pos)
    end

    if first == 253 then
        pos = pos + 1
        return _get_byte3(data, pos)
    end

    if first == 254 then
        pos = pos + 1
        return _get_byte8(data, pos)
    end

    return false, pos + 1
end


local function _from_length_coded_str(data, pos)
    local len
    len, pos = _from_length_coded_bin(data, pos)
    if len == nil or len == null then
        return null, pos
    end

    return sub(data, pos, pos + len - 1), pos + len
end


local function _parse_ok_packet(packet)
    local res = {}
    local pos

    res.affected_rows, pos = _from_length_coded_bin(packet, 2)

    --print("affected rows: ", res.affected_rows, ", pos:", pos)

    res.insert_id, pos = _from_length_coded_bin(packet, pos)

    --print("insert id: ", res.insert_id, ", pos:", pos)

    res.server_status, pos = _get_byte2(packet, pos)

    --print("server status: ", res.server_status, ", pos:", pos)

    res.warning_count, pos = _get_byte2(packet, pos)

    --print("warning count: ", res.warning_count, ", pos: ", pos)

    local message = sub(packet, pos)
    if message and message ~= "" then
        res.message = message
    end

    --print("message: ", res.message, ", pos:", pos)

    return res
end


local function _parse_eof_packet(packet)
    local pos = 2

    local warning_count, pos = _get_byte2(packet, pos)
    local status_flags = _get_byte2(packet, pos)

    return warning_count, status_flags
end


local function _parse_err_packet(packet)
    local errno, pos = _get_byte2(packet, 2)
    local marker = sub(packet, pos, pos)
    local sqlstate
    if marker == '#' then
        -- with sqlstate
        pos = pos + 1
        sqlstate = sub(packet, pos, pos + 5 - 1)
        pos = pos + 5
    end

    local message = sub(packet, pos)
    return errno, message, sqlstate
end


local function _parse_result_set_header_packet(packet)
    local field_count, pos = _from_length_coded_bin(packet, 1)

    local extra
    extra = _from_length_coded_bin(packet, pos)

    return field_count, extra
end


local function _parse_field_packet(data)
    local col = {}
    local catalog, db, table, orig_table, orig_name, charsetnr, length
    local pos
    catalog, pos = _from_length_coded_str(data, 1)

    --print("catalog: ", col.catalog, ", pos:", pos)

    db, pos = _from_length_coded_str(data, pos)
    table, pos = _from_length_coded_str(data, pos)
    orig_table, pos = _from_length_coded_str(data, pos)
    col.name, pos = _from_length_coded_str(data, pos)

    orig_name, pos = _from_length_coded_str(data, pos)

    pos = pos + 1 -- ignore the filler

    charsetnr, pos = _get_byte2(data, pos)

    length, pos = _get_byte4(data, pos)

    col.type = strbyte(data, pos)

    --[[
    pos = pos + 1

    col.flags, pos = _get_byte2(data, pos)

    col.decimals = strbyte(data, pos)
    pos = pos + 1

    local default = sub(data, pos + 2)
    if default and default ~= "" then
        col.default = default
    end
    --]]

    return col
end


local function _parse_row_data_packet(data, cols, compact)
    local row = {}
    local pos = 1
    local ncols = #cols
    for i = 1, ncols do
        local value
        value, pos = _from_length_coded_str(data, pos)
        local col = cols[i]
        local typ = col.type
        local name = col.name

        --print("row field value: ", value, ", type: ", typ)

        if value ~= null then
            local conv = converters[typ]
            if conv then
                value = conv(value)
            end
        end

        if compact then
            row[i] = value

        else
            row[name] = value
        end
    end

    return row
end


local function _recv_field_packet(self)
    local packet, typ, err = _recv_packet(self)
    if not packet then
        return nil, err
    end

    if typ == "ERR" then
        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
    end

    if typ ~= 'DATA' then
        return nil, "bad field packet type: " .. typ
    end

    -- typ == 'DATA'

    return _parse_field_packet(packet)
end


function _M.new(self)
    local sock, err = tcp()
    if not sock then
        return nil, err
    end
    return setmetatable({ sock = sock }, mt)
end


function _M.set_timeout(self, timeout)
    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    return sock:settimeout(timeout)
end


function _M.connect(self, opts)
    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    local max_packet_size = opts.max_packet_size
    if not max_packet_size then
        max_packet_size = 1024 * 1024 -- default 1 MB
    end
    self._max_packet_size = max_packet_size

    local ok, err

    self.compact = opts.compact_arrays

    local database = opts.database or ""
    local user = opts.user or ""

    local pool = opts.pool

    local host = opts.host
    if host then
        local port = opts.port or 3306
        if not pool then
            pool = user .. ":" .. database .. ":" .. host .. ":" .. port
        end
        if ngx then
            ok, err = sock:connect(host, port, { pool = pool })
        else
            ok, err = sock:connect(host, port, opts.pool_size, user..'@'..host..':'..port..'/'..database)
        end

    else
        local path = opts.path
        if not path then
            return nil, 'neither "host" nor "path" options are specified'
        end

        if not pool then
            pool = user .. ":" .. database .. ":" .. path
        end

        if ngx then
            ok, err = sock:connect("unix:" .. path, { pool = pool })
        else
            ok, err = sock:connect(path, opts.pool_size, user..'@'..path..'/'..database)
        end
    end

    if not ok then
        return nil, 'failed to connect: ' .. err
    end

    local reused = sock:getreusedtimes()

    if reused and reused > 0 then
        self.state = STATE_CONNECTED
        return 1
    end

    local packet, typ, err = _recv_packet(self)
    if not packet then
        return nil, err
    end

    if typ == "ERR" then
        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
    end

    self.protocol_ver = strbyte(packet)

    --print("protocol version: ", self.protocol_ver)

    local server_ver, pos = _from_cstring(packet, 2)
    if not server_ver then
        return nil, "bad handshake initialization packet: bad server version"
    end

    --print("server version: ", server_ver)

    self._server_ver = server_ver

    local thread_id, pos = _get_byte4(packet, pos)

    --print("thread id: ", thread_id)

    local scramble = sub(packet, pos, pos + 8 - 1)
    if not scramble then
        return nil, "1st part of scramble not found"
    end

    pos = pos + 9 -- skip filler

    -- two lower bytes
    self._server_capabilities, pos = _get_byte2(packet, pos)

    --print("server capabilities: ", self._server_capabilities)

    self._server_lang = strbyte(packet, pos)
    pos = pos + 1

    --print("server lang: ", self._server_lang)

    self._server_status, pos = _get_byte2(packet, pos)

    --print("server status: ", self._server_status)

    local more_capabilities
    more_capabilities, pos = _get_byte2(packet, pos)

    self._server_capabilities = bor(self._server_capabilities,
                                    lshift(more_capabilities, 16))

    --print("server capabilities: ", self._server_capabilities)

    -- local len = strbyte(packet, pos)
    local len = 21 - 8 - 1

    --print("scramble len: ", len)

    pos = pos + 1 + 10

    local scramble_part2 = sub(packet, pos, pos + len - 1)
    if not scramble_part2 then
        return nil, "2nd part of scramble not found"
    end

    scramble = scramble .. scramble_part2
    --print("scramble: ", _dump(scramble))

    local password = opts.password or ""

    local token = _compute_token(password, scramble)

    -- local client_flags = self._server_capabilities
    local client_flags = 260047;

    --print("token: ", _dump(token))

    local req = _set_byte4(client_flags)
                .. _set_byte4(self._max_packet_size)
                .. "\0" -- TODO: add support for charset encoding
                .. strrep("\0", 23)
                .. _to_cstring(user)
                .. _to_binary_coded_string(token)
                .. _to_cstring(database)

    local packet_len = 4 + 4 + 1 + 23 + #user + 1
        + #token + 1 + #database + 1

    -- print("packet content length: ", packet_len)
    -- print("packet content: ", _dump(concat(req, "")))

    local bytes, err = _send_packet(self, req, packet_len)
    if not bytes then
        return nil, "failed to send client authentication packet: " .. err
    end

    --print("packet sent ", bytes, " bytes")

    local packet, typ, err = _recv_packet(self)
    if not packet then
        return nil, "failed to receive the result packet: " .. err
    end

    if typ == 'ERR' then
        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
    end

    if typ == 'EOF' then
        return nil, "old pre-4.1 authentication protocol not supported"
    end

    if typ ~= 'OK' then
        return nil, "bad packet type: " .. typ
    end

    self.state = STATE_CONNECTED

    return 1
end


function _M.set_keepalive(self, ...)
    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    if self.state ~= STATE_CONNECTED then
        return nil, "cannot be reused in the current connection state: "
                    .. (self.state or "nil")
    end

    self.state = nil
    if not ngx then return true end
    return sock:setkeepalive(...)
end


function _M.get_reused_times(self)
    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    return sock:getreusedtimes()
end


function _M.close(self)
    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    self.state = nil

    return sock:close()
end


function _M.server_ver(self)
    return self._server_ver
end


local function send_query(self, query)
    if self.state ~= STATE_CONNECTED then
        return nil, "cannot send query in the current context: "
                    .. (self.state or "nil")
    end
    
    --print(query)

    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    self.packet_no = -1

    local cmd_packet = strchar(COM_QUERY) .. query
    local packet_len = 1 + #query

    local bytes, err = _send_packet(self, cmd_packet, packet_len)
    if not bytes then
        return nil, err
    end

    self.state = STATE_COMMAND_SENT

    -- add by oneoo
    self.command = query:sub(1,query:find(' ')-1):upper()
    -- end
    
    --print("packet sent ", bytes, " bytes")

    return bytes
end
_M.send_query = send_query


local function read_result(self)
    if self.state ~= STATE_COMMAND_SENT then
        return nil, "cannot read result in the current context: " .. self.state
    end

    local sock = self.sock
    if not sock then
        return nil, "not initialized"
    end

    local packet, typ, err = _recv_packet(self)
    if not packet then
        return nil, err
    end

    if typ == "ERR" then
        self.state = STATE_CONNECTED

        local errno, msg, sqlstate = _parse_err_packet(packet)
        return nil, msg, errno, sqlstate
    end

    if typ == 'OK' then
        local res = _parse_ok_packet(packet)
        if res and band(res.server_status, SERVER_MORE_RESULTS_EXISTS) ~= 0 then
            return res, "again"
        end

        -- add by oneoo
        if self.command == 'INSERT' or self.command == 'DELETE' or self.command == 'UPDATE' then
            if res.affected_rows < 1 then
                self.state = STATE_CONNECTED
                return false, 'no affected'
            end
        end
        -- end

        self.state = STATE_CONNECTED
        return res
    end

    if typ ~= 'DATA' then
        self.state = STATE_CONNECTED

        return nil, "packet type " .. typ .. " not supported"
    end

    -- typ == 'DATA'

    --print("read the result set header packet")

    local field_count, extra = _parse_result_set_header_packet(packet)

    --print("field count: ", field_count)

    local cols = {}
    for i = 1, field_count do
        local col, err, errno, sqlstate = _recv_field_packet(self)
        if not col then
            return nil, err, errno, sqlstate
        end

        cols[i] = col
    end

    local packet, typ, err = _recv_packet(self)
    if not packet then
        return nil, err
    end

    if typ ~= 'EOF' then
        return nil, "unexpected packet type " .. typ .. " while eof packet is "
            .. "expected"
    end

    -- typ == 'EOF'

    local compact = self.compact

    local rows = {}
    local i = 0
    while true do
        --print("reading a row")

        packet, typ, err = _recv_packet(self)
        if not packet then
            return nil, err
        end

        if typ == 'EOF' then
            local warning_count, status_flags = _parse_eof_packet(packet)

            --print("status flags: ", status_flags)

            if band(status_flags, SERVER_MORE_RESULTS_EXISTS) ~= 0 then
                return rows, "again"
            end

            break
        end

        -- if typ ~= 'DATA' then
            -- return nil, 'bad row packet type: ' .. typ
        -- end

        -- typ == 'DATA'

        local row = _parse_row_data_packet(packet, cols, compact)
        i = i + 1
        rows[i] = row
    end

    self.state = STATE_CONNECTED

    return rows
end
_M.read_result = read_result

-- add by oneoo
local function parse_sql(...)
    local args = {...}
    local sql = args[1]
    if not sql or type(sql) ~= 'string' then
        return nil, 'no SQL'
    end

    if #args > 1 then
        local is_normal_mode = true
        --('SELECT * FROM table WHERE id=? AND name=? LIMIT 1', 1, 'one')
        local k,v
        for k,v in pairs(args) do
            if type(v) == 'table' then
                is_normal_mode = false
                break
            end
        end
        
        if not is_normal_mode and 
            type(args[2]) == 'table' and 
            #args[2] > 0 and 
            type(args[2][1]) ~= 'table' then
            --('SELECT * FROM table WHERE id=? AND name=? LIMIT 1', {1, 'one'})
            args = {args[1], unpack(args[2])}
            is_normal_mode = true
        end
        
        if is_normal_mode then
            local i = 2
            local _pos = 1
            local w = sql:upper():find(' WHERE ')
            local iw = 20000
            if w then
                iw = 1
                local _sql = sql:sub(1, w)
                local p = _sql:find('?')
                while p do
                    iw = iw + 1
                    p = _sql:find('?', p+1)
                end
            end
            local p = sql:find('?', _pos)
            while p do
                if i>#args or args[i] == nil then
                    return nil, 'miss data to parse marker'
                end
                v = args[i]
                if v == null then
                    v = ((w and i>iw) and 'IS ' or '')..'NULL'
                end
                sql = concat({
                                    sql:sub(1, p-1),
                                    (type(args[i])=='string' and '"' or ''),
                                    _mysql_quote(v),
                                    (type(args[i])=='string' and '"' or ''),
                                    sql:sub(p+1)
                                    })
                i = i+1
                _pos = p+#tostring(args[i])
                p = sql:find('?', _pos)
            end
            
            return sql
        end
        
        local i = sql:find(' ')
        if not i then
            return nil, 'error SQL format'
        end
        local starts = sql:sub(1, i-1):upper()
        i = sql:find('?')
        if not i then
            return nil, 'miss ? marker in the SQL'
        end
        
        local t2 = type(args[2])
        
        if t2 == 'string' then
            sql = sql:sub(1, i-1).. '"'.. args[2] .. '"' .. sql:sub(i+1)
        elseif t2 == 'number' then
            sql = sql:sub(1, i-1).. args[2] .. sql:sub(i+1)
        elseif t2 == 'table' then
            local sql_p = ''
            local sep = ' AND '
            if starts == 'UPDATE' or starts == 'INSERT' then
                sql_p = ''
                sep = ', '
            end
            if #args[2] < 1 then
                for k,v in pairs(args[2]) do
                    if type(k) == 'string' then
                        if v == null then
                            sql_p = concat({
                                                sql_p,
                                                '`', k, '`',
                                                ((starts=='INSERT' or starts=='UPDATE') and '=' or ' IS '),
                                                'NULL', sep
                                                })
                        else
                            local q = (type(v) == 'string' and '"' or '')
                            sql_p = concat({
                                                sql_p,
                                                '`', k, '`=',
                                                q, _mysql_quote(v), q,
                                                sep
                                                })
                        end
                    end
                end
                sql_p = sql_p:sub(1,#sql_p-#sep)
            else
                if starts == 'INSERT' and type(args[2][1]) == 'table' and #args[2][1] < 1 then
                sql_p = sql_p .. '('
                for k,v in pairs(args[2][1]) do
                    if type(k) == 'string' then
                        sql_p = sql_p ..'`' .. k .. '`' .. sep
                    end
                end
                sql_p = sql_p:sub(1,#sql_p-#sep)
                sql_p = sql_p .. ')'
                sql_p = sql_p .. ' VALUES '
                local k
                for k=1,#args[2] do
                    sql_p = sql_p .. '('
                    for k,v in pairs(args[2][k]) do
                        if type(k) == 'string' then
                            if v == null then
                                sql_p = sql_p .. 'NULL' .. sep
                            else
                                local q = (type(v) == 'string' and '"' or '')
                                sql_p = concat({
                                                    sql_p,
                                                    q, _mysql_quote(v), q,
                                                    sep
                                                    })
                            end
                        end
                    end
                    sql_p = sql_p:sub(1,#sql_p-#sep)
                    sql_p = sql_p .. ')'.. sep
                end
                sql_p = sql_p:sub(1,#sql_p-#sep)
                end
            end
            
            sql = sql:sub(1, i-1) .. sql_p .. sql:sub(i+1)
            local oi = i
            i = sql:find('?', i+#sql_p)
            if i then
                if #args < 3 or type(args[3]) ~= 'table' or #args[3] > 0 then
                    return nil, 'miss data to parse marker near:'
                                .. sql:sub(1, oi-1)..'?'..sql:sub(#sql_p+oi,i-1)..'<?>'
                end
                
                sql_p = ''
                local sep = ' AND '
                for k,v in pairs(args[3]) do
                    if type(k) == 'string' then
                        if v == null then
                            sql_p = sql_p .. '`' .. k .. '` IS NULL' .. sep
                        else
                            local q = (type(v) == 'string' and '"' or '')
                            sql_p = concat({
                                                sql_p,
                                                '`', k, '`=',
                                                q, _mysql_quote(v), q,
                                                sep
                                                })
                        end
                    end
                end
                sql_p = sql_p:sub(1,#sql_p-#sep)
                
                sql = sql:sub(1, i-1) .. sql_p .. sql:sub(i+1)
            end
        end
    end

    return sql
end

function _M.get_row(self, query)
    local sql = parse_sql(query)
    local _sql = sql:upper()
    
    if not _sql:find(' LIMIT ') then
        local i = sql:find(';')
        if not i then i = #sql else i = i-1 end
        sql = sql:sub(1,i)..' LIMIT 1'..sql:sub(i+1)
    end
    
    local bytes, err = send_query(self, sql)
    if not bytes then
        return nil, "failed to send query: " .. err
    end

    local r, err = read_result(self)
    if r and #r < 1 then r = nil end
    return r, err
end

function _M.get_results(self, query)
    local sql = parse_sql(query)
    local _sql = sql:upper()
    
    local bytes, err = send_query(self, sql)
    if not bytes then
        return nil, "failed to send query: " .. err
    end

    local r, err = read_result(self)
    if r and #r < 1 then r = nil end
    return r, err
end
-- end

function _M.query(self, query, ...)
    local args = {query, ...}
    local bytes, err = send_query(self, parse_sql(unpack(args)))
    if not bytes then
        return nil, "failed to send query: " .. err
    end

    return read_result(self)
end


function _M.set_compact_arrays(self, value)
    self.compact = value
end


_M.send_query = send_query
_M.read_result = read_result


return _M