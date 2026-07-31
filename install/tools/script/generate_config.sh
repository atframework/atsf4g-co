#!/bin/bash
cd "$(dirname $0)"

chmod +x ../atdtool/atdtool
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --set global.world_id=1

../atdtool/atdtool template ../../cloud-native/charts/robot -o ../../robot --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --mode nondeploy
../atdtool/atdtool template ../../cloud-native/charts/otelcol -o ../../otelcol --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --mode nondeploy
