<%!
    import common.project_utils as project
%><%include file="common.template.sh" />

if [[ -e "$SERVER_PID_FILE_NAME" ]]; then
  PROC_PID=$(cat $SERVER_PID_FILE_NAME 2>/dev/null)
else
  PROC_PID=0
fi

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -eq $? ] && [ $PROC_PID -gt 0 ]; then
	NoticeMsg "$SERVER_FULL_NAME already stopped";
	exit 0;
fi

./$SERVERD_NAME -id $SERVER_BUS_ID -c ../etc/$SERVER_FULL_NAME.yaml -p $SERVER_PID_FILE_NAME stop

export LD_PRELOAD=;

if [ $? -ne 0 ]; then
	ErrorMsg "send stop command to $SERVER_FULL_NAME failed.";
else
	WaitProcessStoped "$SERVER_PID_FILE_NAME";
fi

CheckProcessRunning "$SERVER_PID_FILE_NAME";
if [ 0 -ne $? ] && [ $PROC_PID -gt 0 ]; then
	NoticeMsg "$SERVER_FULL_NAME can not be stoped by command, try to kill by signal";
	kill $(cat "$SERVER_PID_FILE_NAME");
	WaitProcessStoped "$SERVER_PID_FILE_NAME";
fi

NoticeMsg "stop $SERVER_FULL_NAME done." ;