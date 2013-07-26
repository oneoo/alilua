--host_route['^.*$'] = '/var/www/a.com/index.lua'
--host_route['b.com'] = '/var/www/b.com/index.lua'

function process(headers, _GET, _COOKIE, _POST)
	local r,e = newthread(function()
		dofile(host_route[headers.host]) die()
	end)
	
	if e then print_error(e) end
end
