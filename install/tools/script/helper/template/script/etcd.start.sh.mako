<%!
import os
import common.project_utils as project
%><%include file="etcd.common.template.sh" />

if [ ! -e "$SERVER_DATA_DIR" ]; then
    mkdir -p "$SERVER_DATA_DIR";
fi

if [ ! -e "$SERVER_WAL_DIR" ]; then
    mkdir -p "$SERVER_WAL_DIR";
fi

<%
start_scripts = []
max_line_column = 0

def append_start_script(s, lc, line):
    s.append(line)
    return max(lc, len(line))

# max_line_column = append_start_script(start_scripts, max_line_column,'nohup $SERVERD_NAME --config-file ../etc/$SERVER_FULL_NAME.yaml" > /dev/null 2>&1 &')
max_line_column = append_start_script(start_scripts, max_line_column,'nohup $SERVERD_NAME --name "$SERVER_CLUSTER_NAME"')
max_line_column = append_start_script(start_scripts, max_line_column,'  --data-dir "$SERVER_DATA_DIR" --wal-dir "$SERVER_WAL_DIR" --force-new-cluster')
max_line_column = append_start_script(start_scripts, max_line_column,'  --snapshot-count 20000 --heartbeat-interval 100 --election-timeout 1000 --max-snapshots 3 --max-wals 3')
max_line_column = append_start_script(start_scripts, max_line_column,'  --cors "{0}"'.format(project.get_server_option('cors', '')))
max_line_column = append_start_script(start_scripts, max_line_column,'  --initial-cluster-token "$SERVER_INIT_CLUSTER_TOKEN"')
max_line_column = append_start_script(start_scripts, max_line_column,'  --listen-peer-urls "http://0.0.0.0:$SERVER_PEER_PORT" --listen-client-urls "http://0.0.0.0:$SERVER_CLIENT_PORT"')
max_line_column = append_start_script(start_scripts, max_line_column,'  --initial-advertise-peer-urls "http://$SERVER_OUTER_IP:$SERVER_PEER_PORT"')
max_line_column = append_start_script(start_scripts, max_line_column,'  --initial-cluster "$SERVER_CLUSTER_NAME=http://$SERVER_OUTER_IP:$SERVER_PEER_PORT"')
max_line_column = append_start_script(start_scripts, max_line_column,'  --advertise-client-urls "{0}"'.format(project.get_etcd_client_urls()))
max_line_column = append_start_script(start_scripts, max_line_column,'  --enable-grpc-gateway=true')
max_line_column = append_start_script(start_scripts, max_line_column,'  > /dev/null 2>&1 &')
%>
${os.linesep.join([('{0:' + str(max_line_column) + '} \\').format(x) for x in start_scripts])}

if [ $? -ne 0 ]; then
    ErrorMsg "start $SERVER_CLUSTER_NAME failed.";
    exit $?;
fi

echo $! > "$SERVER_PID_FILE_NAME" ;

WaitProcessStarted "$SERVER_PID_FILE_NAME" ;

if [ $? -ne 0 ]; then
    ErrorMsg "start $SERVER_CLUSTER_NAME failed.";
    exit $?;
fi

NoticeMsg "start $SERVER_CLUSTER_NAME done.";

exit 0;
