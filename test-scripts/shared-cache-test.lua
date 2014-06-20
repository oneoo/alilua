local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/example/shared-cache?set=1&key=key&value=abc&ttl=1')

if res.body == 'true' then
    res,err = httprequest('http://localhost:19827/example/shared-cache?get=1&key=key')
    if res.body == 'abc' then
        sleep(2000)
        res,err = httprequest('http://localhost:19827/example/shared-cache?get=1&key=key')
        if res.body ~= 'abc' then
            res,err = httprequest('http://localhost:19827/example/shared-cache?set=1&key=key&value=abc')
            sleep(2000)
            res,err = httprequest('http://localhost:19827/example/shared-cache?get=1&key=key')
            if res.body == 'abc' then
                res,err = httprequest('http://localhost:19827/example/shared-cache?del=1&key=key')
                if res.body == 'true' then
                    return true
                end
            end
        end
    end
end

return false