local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/example/chunk-output')

local s = 'abcdefghijklmnopqrstuvwxyz'
s = s:rep(8192)
local ss = 'This is the data in the first chunk\r\n' .. s:rep(5) .. 'end'

if not err then
    if res.header['transfer-encoding'] == 'chunked' and res.body == ss then
        return true
    end
end

return false