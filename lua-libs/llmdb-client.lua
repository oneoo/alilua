local llmdb = require('llmdb')
local insert = table.insert
local remove = table.remove
local ceil = math.ceil
local print = print

local _M = { _VERSION = '0.1', 
            MDB_SET_KEY = llmdb.MDB_SET_KEY, 
            MDB_SET_RANGE = llmdb.MDB_SET_RANGE, 
            MDB_NEXT = llmdb.MDB_NEXT, 
            MDB_NOOVERWRITE = llmdb.MDB_NOOVERWRITE, 
            MDB_PREV_DUP = llmdb.MDB_PREV_DUP, 
            MDB_PREV_NODUP = llmdb.MDB_PREV_NODUP, 
            MDB_PREV = llmdb.MDB_PREV, 
        }

local mt = { __index = _M }

function _M.new(self, path)
    local e = nil
    local n = ceil(time()/60)%3 +1

    if not e then
        e = llmdb.env_create()
        --os.execute("mkdir -p " .. path)
        local r, err = e:open(path,0,420)
        if err then
            return nil, err
        end
    end

    self.db = e

    return setmetatable({ db = e }, mt)
end

function _M.close(self)
    local db = self.db
    db:close()
    self.db = nil
end

function _M.put(self, key, value)
    local db = self.db
    local t = db:txn_begin(nil,0)
    local d = t:dbi_open(nil,0)

    local rc = t:put(d, key, value, llmdb.MDB_NOOVERWRITE)

    local r,e = t:commit()
    db:dbi_close(d)
    return r,e
end

function _M.get(self, key)
    local db = self.db
    local t = db:txn_begin(nil,0)
    local d = t:dbi_open(nil,0)
    local c = t:cursor_open(d)

    k,v = c:get(key, llmdb.MDB_SET_KEY)

    c:close()
    t:abort()

    if k ~= key then
        v = nil
    end
    db:dbi_close(d)
    return v
end

function _M.del(self, key)
    local db = self.db
    local t = db:txn_begin(nil,0)
    local d = t:dbi_open(nil,0)

    if not t:del(d,key,nil) then
        t:abort()
    else
        t:commit()
        db:dbi_close(d)
        return true
    end

    db:dbi_close(d)

    return false
end


return _M
