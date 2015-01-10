function on(data,frame_opcode,is_multi_frame)
    LOG(INFO, data)
    websocket_send('['..data..']')
end

function loop()
    websocket_send('text ' .. time())
    sleep(1000)
end

websocket_accept(loop, on)