function on(data,frame_opcode,is_multi_frame)
    LOG(INFO, data)
	websocket_send('['..data..']')
    aa = aa+1
end

function loop()
    websocket_send('text ' .. time())
end

websocket_accept(loop, on)