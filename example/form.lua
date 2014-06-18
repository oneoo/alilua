print([[
<form action="/example/form" method="post" enctype="multipart/form-data">
<input type="text" name="name" value ="text name">
<input type="text" name="name2" value ="text name2">
<input type="file" name="file1">
<input type="text" name="name3" value ="text name3">
<input type="submit">
</form>
]])

local key,val,need_chunk_read,file_name,file_type = next_post_field()
while key do
    print(key,val,need_chunk_read,file_name,file_type, "\n")
    if need_chunk_read then
        local fh,en,e
        if file_name then
            fh,en,e = eio.open('/tmp/a', 'w')
            if not fh then
                _print(fh,en,e)
            end
        end

        local chunk = read_post_field_chunk()
        while chunk do
            print('[', file_name and #chunk or chunk, ']', "\n")
            if file_name then fh:write(chunk) end
            chunk = read_post_field_chunk()
        end
        if file_name then fh:close() end
    end

    key,val,need_chunk_read,file_name,file_type = next_post_field()
end
