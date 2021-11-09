#!/bin/bash
<%!
    import common.project_utils as project
%>
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )";
SCRIPT_DIR="$( readlink -f $SCRIPT_DIR )";
cd "$SCRIPT_DIR";

export PROJECT_INSTALL_DIR=$(cd ${project_install_prefix} && pwd);

if [ -z "$WINDIR" ]; then
    SIMULATOR_BIN_NAME="./simulator-cli";
else
    SIMULATOR_BIN_NAME="simulator-cli.exe";
fi

export LD_LIBRARY_PATH=$PROJECT_INSTALL_DIR/lib:$PROJECT_INSTALL_DIR/tools/shared:$LD_LIBRARY_PATH ;
<% is_first_addr = True %>
% for svr_index in project.get_service_index_range(int(project.get_global_option('server.loginsvr', 'number', 0))):
  <%
    connect_port = project.get_server_gateway_port('loginsvr', svr_index, 'atgateway')
    hostname, is_uuid = project.get_hostname()
    if hostname and not is_uuid:
      connect_ip = hostname
    elif project.is_ip_v6_enabled():
      connect_ip = '::1'
    else:
      connect_ip = '127.0.0.1'
    %>
  % if is_first_addr:
$SIMULATOR_BIN_NAME --host ${connect_ip} --port ${connect_port} "$@";
    <% is_first_addr = False %>
  % else:
# $SIMULATOR_BIN_NAME --host ${connect_ip} --port ${connect_port} "$@";
  % endif
% endfor

% if is_first_addr:
$SIMULATOR_BIN_NAME "$@";
% endif
