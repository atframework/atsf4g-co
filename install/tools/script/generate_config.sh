#!/bin/bash
cd "$(dirname $0)"

chmod +x ../atdtool/atdtool
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values ../../cloud-native/values/default,../../cloud-native/values/dev --set global.world_id=1
