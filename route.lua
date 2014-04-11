routes = {}
routes['^/(.*).(jpg|gif|png|css|js|ico|swf|flv|mp3|mp4|woff|eot|ttf|otf|svg)'] = function()
    header('Cache-Control: max-age=864000')
    sendfile(headers.uri)
end

routes['^/user/:user_id(.*)'] = function(r)
    print('User ID: ', r.user_id)
end

routes['^/(.*)'] = function(r)
    dofile('/index.lua')
end

--[[
others you want :)
]]

if not router(headers.uri, routes) then
    -- try local scripts
    --[[
        /       =>  /index.lua
        /hello  =>  /hello.lua  if not exists then try /hello/index.lua
        /hello/ =>  /hello.lua  if not exists then try /hello/index.lua
    ]]
    local file = headers.uri
    if file:byte(#file) == 47 then -- end with /
        file = file:sub(1, #file - 1)
    end

    if file_exists(file .. '.lua') then
        file = file .. '.lua'
    elseif file_exists(file .. '/index.lua') then
        file = file .. '/index.lua'
    else
        file = nil
    end

    if file and file ~= '/route.lua' then
        dofile(file)
    else
        header('HTTP/1.1 404 Not Found')
        echo('File Not Found!')
    end
end
