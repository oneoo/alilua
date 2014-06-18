local httprequest = require('httpclient').httprequest

local data = {}
data[escape_uri(' + key')] = 'value 中文'
local a = 'http://localhost:19827/example/form'
local s = ''
for i=1,4096 do
    s = s .. a
end
data['big value'] = s

local res,err = httprequest('http://localhost:19827/example/form', {data=data})

if not err then
    if res.status == 200 and tonumber(res.header['content-length']) > 1024 then
        return true
    end
end

return false