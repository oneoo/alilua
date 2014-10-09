local db = mysql:new()

local db_ok, err, errno, sqlstate = db:connect({
                    host = "localhost",
                    port = 3306,
                    pool_size = 256,
                    database = "hello_world",
                    user = "root",
                    password = ""})

if not db_ok then
    header('HTTP/1.1 503 ServerError')
    die('MySQL Connection Error.')
end

local res, err, errno, sqlstate = db:query("select * from Fortune where id=5")

if not res then
    echo("bad result: ", err, ": ", errno, ": ", sqlstate, ".")
else
    echo(res[1].message)
end

local res, err, errno, sqlstate = db:query("select * from Fortune where id=4")

if not res then
    echo("bad result: ", err, ": ", errno, ": ", sqlstate, ".")
else
    echo(res[1].message)
end

local res,err = db:get_results("select * from Fortune where id IN(8,9,10)")

if res then
    for k,v in pairs(res) do print(' ', k, v.message) end
end

db:close()
