#!/bin/bash

SCRIPT_DIR="$(cd $(dirname $0) && pwd)";

if [ -z "$REDIS_PREFIX_DIR" ]; then
    export REDIS_PREFIX_DIR="$(cd $SCRIPT_DIR/.. && pwd)";
else
    export REDIS_PREFIX_DIR="$(cd $REDIS_PREFIX_DIR && pwd)";
fi

cd "$SCRIPT_DIR";

mkdir -p ../data;
mkdir -p ../log;
mkdir -p systemd; 

echo "#!/bin/sh" > systemd/cluster_enable_all.sh;
echo "#!/bin/sh" > systemd/cluster_disable_all.sh;
echo "#!/bin/sh" > systemd/cluster_start_all.sh;
echo "#!/bin/sh" > systemd/cluster_stop_all.sh;

START_ID=1;
END_ID=6;

if [ $# -gt 1 ]; then
    START_ID=$1;
    END_ID=$2;
elif [ $# -gt 0 ]; then
    END_ID=$1;
fi

for (( i = $START_ID; i <= $END_ID ; ++ i )); do
    let port=$i+7000;
    echo "gen svr $port";
    mkdir -p cluster-$port;
    cp -f cluster-redis.conf cluster-$port/redis.conf;
    perl -p -i -e "s;\\bREDIS_INST_ID\\b;$port;g" cluster-$port/redis.conf;
    perl -p -i -e "s;\\bREDIS_PREFIX_DIR\\b;$REDIS_PREFIX_DIR;g" cluster-$port/redis.conf;
    
    cp -f cluster-systemd.template.service systemd/redis-cluster-$port.service;
    perl -p -i -e "s;\\bREDIS_INST_ID\\b;$port;g" systemd/redis-cluster-$port.service;
    perl -p -i -e "s;\\bREDIS_PREFIX_DIR\\b;$REDIS_PREFIX_DIR;g" systemd/redis-cluster-$port.service;
    
    echo "systemctl enable redis-cluster-$port" >> systemd/cluster_enable_all.sh;
    echo "systemctl disable redis-cluster-$port" >> systemd/cluster_disable_all.sh;
    echo "systemctl start redis-cluster-$port" >> systemd/cluster_start_all.sh;
    echo "systemctl stop redis-cluster-$port" >> systemd/cluster_stop_all.sh;
done


chmod +x systemd/*.sh;