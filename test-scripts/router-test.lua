local httprequest = require('httpclient').httprequest
local res,err = httprequest('http://localhost:19827/user')
local a = nil
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        a = res.body
    else
        return false
    end
end

local res,err = httprequest('http://localhost:19827/user/')
local b = nil
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        b = res.body
    end
end

local res,err = httprequest('http://localhost:19827/user/123')
local aa = nil
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        aa = res.body
    else
        return false
    end
end

local res,err = httprequest('http://localhost:19827/user/123/')
local bb = nil
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        bb = res.body
    end
end

local res,err = httprequest('http://localhost:19827/user/123/postid')
local cc = nil
if not err then
    if #res.body == tonumber(res.header['content-length']) then
        cc = res.body
    end
end

if a == b and aa == bb and cc == 'User ID: 123 Post: postid' then
    return true
end

return false