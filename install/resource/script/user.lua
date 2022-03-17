local real_version_str = redis.call('HGET', KEYS[1], ARGV[1])
local real_version = 0
if real_version_str != false and real_version_str != nil then
    real_version = tonumber(real_version_str)
end
local except_version = tonumber(ARGV[2])
local unpack_fn = table.unpack or unpack -- Lua 5.1 - 5.3
if real_version == 0 or except_version == real_version then
    ARGV[2] = real_version + 1;
    redis.call('HMSET', KEYS[1], unpack_fn(ARGV));
    return  { ok = tostring(ARGV[2]) };
else
    return  { err = 'CAS_FAILED|' .. tostring(real_version) };
end

