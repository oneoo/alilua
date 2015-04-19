local routes = {}
routes['^/(.*).(jpg|gif|png|css|js|ico|swf|flv|mp3|mp4|woff|eot|ttf|otf|svg)$'] = function()
    header('Cache-Control: max-age=864000')
    if headers.uri:find('.js',1,1) or headers.uri:find('.css',1,1) then -- cloud be gzip
        local mtime = filemtime(headers.uri)
        if headers['if-none-match'] and tonumber(headers['if-none-match']) == mtime then
            header('HTTP/1.1 304 Not Modified')
        else
            header('Etag: '..mtime)
            echo(readfile(headers.uri))
        end
    else
        sendfile(headers.uri)
    end
end

routes['^/user/:user_id'] = function(r)
    echo('User ID: ', r.user_id)
end

routes['^/user/:user_id/:post(.+)'] = function(r)
    echo('User ID: ', r.user_id)
    echo(' Post: ', r.post)
end

routes['^/(.*)'] = function(r)
    dofile('/index.lua')
end

--[[
others you want :)
]]

-- if the 3rd argument is a path, then router will try to get local lua script file first
if not router(headers.uri, routes, '/') then
    header('HTTP/1.1 404 Not Found')
    echo('File Not Found!')
end
