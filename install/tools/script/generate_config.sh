#!/bin/bash
cd "$(dirname $0)"

chmod +x ../atdtool/atdtool
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --set global.world_id=1

# Render start_all/stop_all/kill_all scripts from the tools chart.
# The instance batches come from deploy.yaml (group ordering) and the
# script templates are selected per platform by the caller. kill_all only
# considers service bin directories listed in deploy.yaml.
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --set global.world_id=1 --mode deploy_script --scripts tools/start_all.sh.tpl,tools/stop_all.sh.tpl,tools/kill_all.sh.tpl

../atdtool/atdtool template ../../cloud-native/charts/robot -o ../../robot --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --mode nondeploy
../atdtool/atdtool template ../../cloud-native/charts/otelcol -o ../../otelcol --values ../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal --mode nondeploy
