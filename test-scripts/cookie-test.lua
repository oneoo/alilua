local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/example/cookie')

return unescape_uri(res.header['set-cookie']) == 'key=中文 +abc; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/abc;'