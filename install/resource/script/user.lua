local version = redis.call('HGET', KEYS[1], ARGV[1]);
local unpack_fn = table.unpack or unpack -- Lua 5.1 - 5.3
if version == false or tonumber(ARGV[2]) == tonumber(version)  then
    ARGV[2] = ARGV[2] + 1;
    redis.call('HMSET', KEYS[1], unpack_fn(ARGV));
    return  { ok = tostring(ARGV[2]) };
else
    return  { err = 'CAS_FAILED|' .. tostring(version) };
end

