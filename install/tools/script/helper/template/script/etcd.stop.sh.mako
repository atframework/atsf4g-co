<%!
    import common.project_utils as project
%><%include file="etcd.common.template.sh" />

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -eq $? ]; then
    NoticeMsg "$SERVER_CLUSTER_NAME already stopped";
    exit 0;
fi

kill -s QUIT $(cat "$SERVER_PID_FILE_NAME") ;

if [ $? -ne 0 ]; then
    ErrorMsg "send stop command to $SERVER_CLUSTER_NAME failed.";
    exit $?;
fi

WaitProcessStoped "$SERVER_PID_FILE_NAME";

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -ne $? ]; then
    NoticeMsg "$SERVER_CLUSTER_NAME can not be stoped by command, try to kill by signal";
    kill -9 $(cat "$SERVER_PID_FILE_NAME");
    WaitProcessStoped "$SERVER_PID_FILE_NAME";
fi

NoticeMsg "stop $SERVER_CLUSTER_NAME done." ;
