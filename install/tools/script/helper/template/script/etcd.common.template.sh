#!/bin/bash
<%!
    import os
    import common.project_utils as project
%>
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

SERVER_NAME="etcd";

if [ -z "$WINDIR" ]; then
    SERVERD_NAME="./etcd";
else
    SERVERD_NAME="etcd.exe";
fi

SERVER_FULL_NAME="${project.get_server_name()}-${hex(project.get_server_id())}";
export PROJECT_INSTALL_DIR=$(cd ${project_install_prefix} && pwd);

source "$PROJECT_INSTALL_DIR/tools/script/common/common.sh";

if [ ! -e "$SERVERD_NAME" ]; then
    SERVERD_NAME="${project.get_server_name()}d";
fi

if [ ! -e "$SERVERD_NAME" ]; then
    ErrorMsg "Executable $SERVERD_NAME not found, run $@ failed";
    exit 1;
fi
SERVER_PID_FILE_NAME="$SERVER_FULL_NAME.pid";
SERVER_DATA_DIR="${project.get_server_option('data_dir', '../data', 'SYSTEM_MACRO_CUSTOM_ETCD_DATA_DIR')}";
<%
etcd_wal_dir = project.get_server_option('wal_dir', '', 'SYSTEM_MACRO_CUSTOM_ETCD_WAL_DIR')
if not etcd_wal_dir:
    etcd_wal_dir = os.path.join(os.path.dirname(project.get_server_option('data_dir', '../data', 'SYSTEM_MACRO_CUSTOM_ETCD_DATA_DIR')), 'wal')
%>SERVER_WAL_DIR="${etcd_wal_dir}";
SERVER_PEER_PORT=${project.get_calc_listen_port(None, None, 'peer_port')};
SERVER_CLIENT_PORT=${project.get_calc_listen_port(None, None, 'client_port')};
SERVER_OUTER_IP="${project.get_outer_ipv4()}";
SERVER_INIT_CLUSTER_TOKEN=${project.get_server_option('init_cluster_token', '')};
SERVER_CLUSTER_NAME="${project.get_server_name()}-${hex(project.get_server_id())}";
