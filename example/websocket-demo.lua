function on(data,frame_opcode,is_multi_frame)
    LOG(INFO, data)
    if data == 'close' then die() end
    websocket_send('['..data..']')
end

c = 0
function loop()
    c = c+1
    if c > 10 then die() end
    websocket_send('Now: ' .. time() .. ' N:' .. c)
    sleep(1000)
end

websocket_accept(loop, on)