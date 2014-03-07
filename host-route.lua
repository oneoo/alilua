--host_route['^.*$'] = '/var/www/a.com/index.lua'
--host_route['b.com'] = '/var/www/b.com/index.lua'

function process(headers, _GET, _COOKIE, _POST)
	local r,e = newthread(function()
		local router = host_route[headers.host]
		__root = router
		local c = host_route[headers.host]:byte(1)
		if c ~= 46 and c ~= 47 then
			__root = debug.getinfo(1).source:sub(2)
			router = debug.getinfo(1).source:sub(2)
		end

		local m = #router
		local k
		local p = ('/'):byte(1)
		for k = m,1,-1 do
			if router:byte(k) == p then
				__root = router:sub(1,k)
				router = router:sub(k)
				break
			end
		end

		if c ~= 46 and c ~= 47 then
			router = host_route[headers.host]
		end
		
		dofile(router)
		die()
	end)
	
	if e then print_error(__epd__, e) end
	
	r,e = wait(r) --important!
	if not r and e then print_error(__epd__, e) end
end
