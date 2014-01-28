function on(data)
	websocket_send('['..data..']')
end

function loop()
	while true do
		websocket_send('text')
		sleep(1)
	end
end

websocket_accept(loop)