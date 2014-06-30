local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/example/flush')

local s = 'abcdefghijklmnopqrstuvwxyz'
s = s:rep(2)
local ss = s
s = s:rep(2)
ss = ss .. s .. '123'

if not err then
    if res.header['transfer-encoding'] == 'chunked' and res.body == ss then
        return true
    end
end

return false