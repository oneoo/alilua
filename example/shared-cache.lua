if _GET['set'] then
    print(cache_set(_GET['key'], _GET['value'], _GET['ttl']))
elseif _GET['get'] then
    print(cache_get(_GET['key']))
elseif _GET['del'] then
    print(cache_del(_GET['key']))
end