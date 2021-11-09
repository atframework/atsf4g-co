<%!
    import common.project_utils as project
%><%include file="common.template.sh" />

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -eq $? ]; then
	ErrorMsg "send reload command to $SERVER_FULL_NAME failed, not running";
	exit 1;
fi

./$SERVERD_NAME -id $SERVER_BUS_ID -c ../etc/$SERVER_FULL_NAME.yaml -p $SERVER_PID_FILE_NAME reload

export LD_PRELOAD=;

if [ $? -ne 0 ]; then
	ErrorMsg "send reload command to $SERVER_FULL_NAME failed.";
	exit $?;
fi

NoticeMsg "reload $SERVER_FULL_NAME done.";