header ( 'Content-Type: text/plain' )
print ( get_post_body() )

print ( 'headers\n' ) for k, v in pairs ( headers ) do print ( '\''..k..'\'=\''..v,
                '\'\n' ) end
        print ( '\n_GET\n' ) for k, v in pairs ( _GET ) do print ( k, '=', v, '\n' ) end
                print ( '\n_POST\n' ) for k,
                      v in pairs ( _POST ) do if type ( v ) == 'string' then print ( k, '=', v, '\n' )
                                  else print ( k ) for kk, vv in pairs ( v ) do print ( '	', kk, '=', vv,
                                                      '\n' ) end print ( '\n' ) end end

                                              --print ( aaa..'aaa' )
                                              local st = longtime()

                 local db = mysql:
                                                                    new()
                            --db:
                                                                    setkeepalive ( 256 )
                            local db_ok, err, errno, sqlstate = db:
                                        connect ( {
                                        host = "localhost",
                                        port = 3306,
                                        --path = "/var/lib/mysql/mysql.sock",
                                        pool_size = 256,
                                        database = "d1",
                                        user = "u1",
                                        password = "u11111"
                                    } )

print ( 'start test_mysql\r\n' )


if not db_ok then
print ( "failed to connect: ", err, ": ", errno, " ", sqlstate )
    return
        end
        print ( '---------------------------------------mysql connected\n' )

        --print ( 'used:'.. ( ( longtime() - st ) / 1000 )..'\n' )
        for n = 1, 1 do
                    local bytes, err
            bytes, err = db:
                                 send_query ( "select * from t1 limit 1;" )
                                     if not bytes then
                                     print ( "failed to send query: ", err )
                                         end

                                         --print ( 'used:'.. ( ( longtime() - st ) / 1000 )..'\n' )
                         local res, err, errno, sqlstate = db:
                                                 read_result()
                                                 if not res then
                                                 print ( "bad result: ", err, ": ", errno, ": ", sqlstate, "." )
                                                     end
                                                     --print ( 'used:'.. ( ( longtime() - st ) / 1000 )..'\n' )
                                                     for m = 1, 1 do
                                                                 print ( n, "result: ", json_encode ( res )..'\n' )
                                                                     end
                                                                     end

                                                                     --print ( 'test_mysql ended  used:'.. ( ( longtime() - st ) / 1000 )..'\n' );

db:
close()

--echo ( string.rep ( 'hello world! abcdefghijklmopqrstuvwxyz 0123456789 !@#$%^&*()_+-=[]:"<>?,./\n',
                      1 ) )


--print ( 'test_mysql ended  used:'.. ( ( longtime() - st ) / 1000 )..'\n' );
--echo ( string.rep ( 'abcdefghijklmopqrstuvwxyz 0123456789 !@#$%^&*()_+-=[]:"<>?,./',
                      2 ) )
die ( 'hello world!' )