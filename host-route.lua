--host_route['^.*$'] = '/var/www/a.com/index.lua'
--host_route['b.com'] = '/var/www/b.com/index.lua'

function process(headers, _GET, _COOKIE, _POST)
	local r,e = newthread(function()
		local router = host_route[headers.host]
		__root = router
		local m = #router
		local k
		local p = ('/'):byte(1)
		for k = 1,m do
			if router:byte(m-k) == p then
				__root = router:sub(1,m-k)
				router = router:sub(m-k)
				break
			end
		end
		dofile(router)
		die()
	end)
	
	if e then print_error(__epd__, e) end
	
	r,e = wait(r) --important!
	if not r and e then print_error(__epd__, e) end
end
