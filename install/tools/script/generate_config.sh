#!/bin/bash
set -e
cd "$(dirname "$0")"

# Helm consumes packaged library dependencies, even when ../libapp has newer sources.
bash ./update_dependency.sh

VALUES_PATHS="../../cloud-native/values/default,../../cloud-native/values/dev,../../cloud-native/values/personal"
LOCAL_OPENOBSERVE_VALUES="../../var/local-test-env/openobserve/values"
if [ -f "$LOCAL_OPENOBSERVE_VALUES/modules/telemetry.yaml" ]; then
  VALUES_PATHS="${VALUES_PATHS},${LOCAL_OPENOBSERVE_VALUES}"
fi

chmod +x ../atdtool/atdtool
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values "$VALUES_PATHS" --set global.world_id=1

# Render start_all/stop_all/kill_all scripts from the tools chart.
# The instance batches come from deploy.yaml (group ordering) and the
# script templates are selected per platform by the caller. kill_all invokes
# each listed instance's kill script so its recorded manager is stopped first.
../atdtool/atdtool template ../../cloud-native/charts -o ../../ --values "$VALUES_PATHS" --set global.world_id=1 --mode deploy_script --scripts tools/start_all.sh.tpl,tools/stop_all.sh.tpl,tools/kill_all.sh.tpl

../atdtool/atdtool template ../../cloud-native/charts/robot -o ../../robot --values "$VALUES_PATHS" --mode nondeploy
../atdtool/atdtool template ../../cloud-native/charts/otelcol -o ../../otelcol --values "$VALUES_PATHS" --mode nondeploy
