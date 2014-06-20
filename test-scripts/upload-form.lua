local httprequest = require('httpclient').httprequest

local data = {}
data[escape_uri(' + key')] = 'value 中文'

data['file'] = {
                    file = io.open('upload-form.lua'),
                    name = 'upload-form.lua',
                    type = 'text/plain'
                }

local res,err = httprequest('http://localhost:19827/example/form', {data=data})

if not err then
    if res.status == 200 and res.body:find('filetrueupload-form.lua',1,1) then
        return true
    end
end

return false