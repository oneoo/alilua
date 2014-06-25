header('Content-Type: text/plain')
echo('This is the data in the first chunk\r\n')
local s = 'abcdefghijklmnopqrstuvwxyz'
s = s:rep(8192)
echo(s)
echo(s)
echo(s)
flush(s)
echo(s)
echo('ccc')

echo('eee')