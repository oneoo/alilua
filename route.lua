local f = headers.uri
if headers.uri == '/' then
    f = f .. 'index.lua'
end

dofile(f)