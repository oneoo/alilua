local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/')
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        return true
    end
end

return false