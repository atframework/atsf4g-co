# Setup redis(5.x+) cluster service using systemd

## Requirements

+ prebuilt redis-server in **[REDIS_DIST]/bin/redis-server** (use ```make install PREFIX=[REDIS_DIST]```)
+ prebuilt redis-cli in **[REDIS_DIST]/bin/redis-cli** (use ```make install PREFIX=[REDIS_DIST]```)
+ copy all files in **etc** into **[REDIS_DIST]/etc**


## Setup

1. cd ```etc```;
2. Run ```sudo ./cluster-gen-conf.sh``` to generate cluster configures
3. Copy systemd service files into systemd configure directory ```sudo cp -f systemd/redis-cluster-*.service /usr/lib/systemd/system```
4. Start all redis server ```sudo ./systemd/cluster_start_all.sh```
5. Create and initialize cluster ```./cluster-init.sh```

All jobs done. see the generated script for more detail.