routes = {}
routes['^/(.*).(jpg|gif|png|css|js|ico|swf|flv|mp3|mp4|woff|eot|ttf|otf|svg)'] = function()
    header('Cache-Control: max-age=864000')
    sendfile(headers.uri)
end

routes['^/user/:user_id'] = function(r)
    print('User ID: ', r.user_id)
end

routes['^/user/:user_id/:post'] = function(r)
    print('User ID: ', r.user_id)
    print('\nPost: ', r.post)
end

routes['^/(.*)'] = function(r)
    dofile('/index.lua')
end

--[[
others you want :)
]]

if not router(headers.uri, routes, '/') then
    -- try local scripts
    --[[
        /       =>  /index.lua
        /hello  =>  /hello.lua  if not exists then try /hello/index.lua
        /hello/ =>  /hello.lua  if not exists then try /hello/index.lua
    ]]
    header('HTTP/1.1 404 Not Found')
    echo('File Not Found!')
end
