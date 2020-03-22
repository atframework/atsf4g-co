#!/usr/bin/env bash

cd "$(dirname "$0")";

START_ID=1;
END_ID=6;
REPLICAS=1;

if [ $# -gt 1 ]; then
    START_ID=$1;
    END_ID=$2;
elif [ $# -gt 0 ]; then
    END_ID=$1;
fi

if [ $# -gt 2 ]; then
    REPLICAS=$3;
fi

echo "cluster init $START_ID-$END_ID";

REDIS_NODE="";

for (( i = $START_ID; i <= $END_ID ; ++ i )); do
    let port=$i+7000;
    REDIS_NODE="$REDIS_NODE 127.0.0.1:$port";
done

../bin/redis-cli --cluster create $REDIS_NODE --cluster-replicas $REPLICAS;
