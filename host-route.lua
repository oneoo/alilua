--[[host route example]]
config['max-request-header']        = 4*1024        --4KB
config['max-request-body']          = 5*1024*1024   --5MB
config['request-body-buffer-size']  = 1*1024*1024   --1MB, if body size large then buffer, write to tmp file
config['temp-path']                 = '/tmp/'
config['code-cache-ttl']            = 60            --60 second
config['auto-reload-vhost-conf']    = 0             --0 second

--host_route['*'] = '/var/www/default/index.lua'
--host_route['*z.com'] = '/var/www/z.com/index.lua'
