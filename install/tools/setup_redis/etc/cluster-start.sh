#!/usr/bin/env bash

cd "$(dirname "$0")";

START_ID=1;
END_ID=6;

if [ $# -gt 1 ]; then
    START_ID=$1;
    END_ID=$2;
elif [ $# -gt 0 ]; then
    END_ID=$1;
fi

echo "$START_ID-$END_ID";

for (( i = $START_ID; i <= $END_ID ; ++ i )); do
    let port=$i+7000;
    echo "start svr $port";
    cd cluster-$port;
    ../../bin/redis-server ./redis.conf;
    cd ..;
done

echo "all done";
