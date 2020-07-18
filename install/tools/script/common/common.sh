#!/bin/sh

# 公共变量
WORKING_DIR=$PWD;
COMMON_LIB_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )";

# 获取当前脚本目录
function get_script_dir()
{
	echo "$( cd "$( dirname "$0" )" && pwd )";
}

# 检测当前shell编码
CURRENT_ENCODING="GB18030";
CURRENT_ENCODING_CHECK1="$(set | grep UTF-8 -i)";
CURRENT_ENCODING_CHECK2="$(set | grep UTF8 -i)";
if [ ! -z "$CURRENT_ENCODING_CHECK1" ] || [ ! -z "$CURRENT_ENCODING_CHECK2" ]; then
	CURRENT_ENCODING="UTF-8";
fi

# 设置本地语言
function set_local_lang()
{
	TARGET_LANG="zh_CN.UTF-8";
	if [ $# -gt 0 ]; then
		TARGET_LANG="$1";
	fi

	export LANG="$TARGET_LANG"
	export LC_CTYPE="$TARGET_LANG"
	export LC_NUMERIC="$TARGET_LANG"
	export LC_TIME="$TARGET_LANG"
	export LC_COLLATE="$TARGET_LANG"
	export LC_MONETARY="$TARGET_LANG"
	export LC_MESSAGES="$TARGET_LANG"
	export LC_PAPER="$TARGET_LANG"
	export LC_NAME="$TARGET_LANG"
	export LC_ADDRESS="$TARGET_LANG"
	export LC_TELEPHONE="$TARGET_LANG"
	export LC_MEASUREMENT="$TARGET_LANG"
	export LC_IDENTIFICATION="$TARGET_LANG"
	export LC_ALL="$TARGET_LANG"
	export RC_LANG="$TARGET_LANG"
	export RC_LC_CTYPE="$TARGET_LANG"
	export AUTO_DETECT_UTF8="yes"

	CURRENT_ENCODING="$(echo $TARGET_LANG | cut -d . -f2)"
}

# 保留指定个数的文件
function remove_more_than()
{
    filter="$1";
    number_left=$2

    FILE_LIST=( $(ls -dt $filter) );
    for (( i=$number_left; i<${#FILE_LIST[@]}; i++)); do
    	rm -rf "${FILE_LIST[$i]}";
    done
}

# 远程指令
function auto_scp()
{
    src=$1
    dst=$2
    pass=$3

    expect -c "set timeout -1;
            spawn scp -p -o StrictHostKeyChecking=no -r $src $dst;
            expect "*assword:*" { send $pass\r\n; };
            expect eof {exit;};
            "
}

function auto_ssh_exec()
{
    host_ip=$1
    host_port=$2
    host_user=$3
    host_pwd=$4
    cmd=$5
	cmd="${cmd//\\/\\\\}";
    cmd="${cmd//\"/\\\"}";
    cmd="${cmd//\$/\\\$}";
    expect -c "set timeout -1;
            spawn ssh -o StrictHostKeyChecking=no ${host_user}@${host_ip} -p ${host_port} \"${cmd}\";
            expect "*assword:*" { send $host_pwd\r\n; };
            expect eof {exit;};
            "
}

# 清空Linux缓存
function free_useless_memory()
{
	CURRENT_USER_NAME=$(whoami)
	if [ "$CURRENT_USER_NAME" != "root" ]; then
		echo "Must run as root";
		exit -1;
	fi

	sync
	echo 3 > /proc/sys/vm/drop_caches
}

# 清空未被引用的用户共享内存
function remove_user_empty_ipc()
{
	CURRENT_USER_NAME=$(whoami)
	for i in $(ipcs | grep $CURRENT_USER_NAME | awk '{ if( $6 == 0 ) print $2}'); do
		ipcrm -m $i
		ipcrm -s $i
	done
}

# 清空用户共享内存
function remove_user_ipc()
{
	CURRENT_USER_NAME=$(whoami)
	for i in $(ipcs | grep $CURRENT_USER_NAME | awk '{print $2}'); do
		ipcrm -m $i
		ipcrm -s $i
	done
}

# 获取系统IPv4地址
function get_ipv4_address()
{
    ALL_IP_ADDRESS=($(ip -4 -o addr | awk '{print $4;}' | cut -d/ -f1));
	if [ $# -gt 0 ]; then
		if [ "$1" == "count" ] || [ "$1" == "number" ]; then
			echo ${#ALL_IP_ADDRESS[@]};
		else
			echo ${ALL_IP_ADDRESS[$1]};
		fi
		return;
	fi
	echo ${ALL_IP_ADDRESS[@]};
}

# 获取系统IPv6地址
function get_ipv6_address()
{
	ALL_IP_ADDRESS=($(ip -6 -o addr | awk '{print $4;}' | cut -d/ -f1));
	if [ $# -gt 0 ]; then
		if [ "$1" == "count" ] || [ "$1" == "number" ]; then
			echo ${#ALL_IP_ADDRESS[@]};
		else
			echo ${ALL_IP_ADDRESS[$1]};
		fi
		return;
	fi
	echo ${ALL_IP_ADDRESS[@]};
}

function is_in_server_list()
{
	shell_server_name="$1";
	shell_server_type_name="${shell_server_name%%-*}";
	shift;
	
	for spec_server in $*; do
		if [ "$shell_server_name" == "$spec_server" ] || [ "$shell_server_type_name" == "$spec_server" ]; then
			echo 0;
			return 0;
		fi
	done
	
	echo 1;
	return 1;
}

function WaitForMS() {
	if [ $# -lt 1 ]; then
		return 0;
	fi

	WAITTIME_MS=$1;
	which usleep > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		let WAITTIME_MS=$WAITTIME_MS*1000;
		usleep $WAITTIME_MS;
		return 0;
	fi

	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 -c "import time; time.sleep($WAITTIME_MS / 1000.0)"
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python -c "import time; time.sleep($WAITTIME_MS / 1000.0)"
		return 0;
	fi

	return 1;
}

function Message() {
	COLOR_CODE="$1";
	shift;

	# if [ -z "$WINDIR" ] || [ "$TERM" != "cygwin" ]; then
		echo -e "\\033[${COLOR_CODE}m$*\\033[39;49;0m";
	# else # Windows 下 cmake 直接调用cmd的，所以 Mingw 不支持着色
	# 	echo "$*";
	# fi
}

function AlertMsg() {
	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 "$COMMON_LIB_DIR/print_color.py" -c green "{0}" "Alert: $*";
		echo "";
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python "$COMMON_LIB_DIR/print_color.py" -c green "{0}" "Alert: $*";
		echo "";
		return 0;
	fi
	echo "Alert: $*";
}

function NoticeMsg() {
	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 "$COMMON_LIB_DIR/print_color.py" -c yellow -B "{0}" "Notice: $*";
		echo "";
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python "$COMMON_LIB_DIR/print_color.py" -c yellow -B "{0}" "Notice: $*";
		echo "";
		return 0;
	fi
	echo "Notice: $*";
}

function ErrorMsg() {
	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 "$COMMON_LIB_DIR/print_color.py" -c red -B "{0}" "Error: $*";
		echo "";
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python "$COMMON_LIB_DIR/print_color.py" -c red -B "{0}" "Error: $*";
		echo "";
		return 0;
	fi
	echo "Error: $*";
}

function WarningMsg() {
	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 "$COMMON_LIB_DIR/print_color.py" -c magenta -B "{0}" "Warning: $*";
		echo "";
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python "$COMMON_LIB_DIR/print_color.py" -c magenta -B "{0}" "Warning: $*";
		echo "";
		return 0;
	fi
	echo "Warning: $*";
}

function StatusMsg() {
	which python3 > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python3 "$COMMON_LIB_DIR/print_color.py" -c cyan "{0}" "Status: $*";
		echo "";
		return 0;
	fi

	which python > /dev/null 2>&1;
	if [ 0 -eq $? ]; then
		python "$COMMON_LIB_DIR/print_color.py" -c cyan "{0}" "Status: $*";
		echo "";
		return 0;
	fi
	echo "Status: $*";
}

function CheckProcessRunning() {
	if [ $# -lt 1 ]; then
		return 0;
	fi
	PROC_NAME="$1" ;
	if [ $# -gt 1 ]; then
		PROC_EXPECT_PID=$2;
	else
		PROC_EXPECT_PID="";
	fi

	if [ ! -f "$PROC_NAME" ]; then
		return 0;
	fi

	PROC_PID=$(cat "$PROC_NAME" 2>/dev/null);

	if [[ "x$PROC_EXPECT_PID" != "x" ]] && [[ "x$PROC_PID" != "x$PROC_EXPECT_PID" ]]; then
		return 0;
	fi

	SYSFLAGS=($PROC_PID);
	if [[ "x${MSYSTEM:0:5}" == "xMINGW" ]] || [[ "x${MSYSTEM:0:4}" == "xMSYS" ]]; then
		SYSFLAGS=($PROC_PID -W);
	fi
	if [ ! -z "$PROC_PID" ] && [ ! -z "$(ps -p ${SYSFLAGS[@]} 2>&1 | grep $PROC_PID)" ]; then
		return 1;
	fi
	
	return 0;
}

function WaitProcessStarted() {
	if [ $# -lt 1 ]; then
		return 1;
	fi

	WAIT_TIME=5000;
	PROC_NAME="$1"

	if [ $# -gt 1 ]; then
		WAIT_TIME=$2;
	fi

	if [ $# -gt 2 ]; then
		PROC_EXPECT_PID=$3;
	else
		PROC_EXPECT_PID="";
	fi

	while [ $WAIT_TIME -gt 0 ] ; do
		CheckProcessRunning "$PROC_NAME" "$PROC_EXPECT_PID";
		if [ 1 -eq $? ]; then
			return 0;
		fi

		WaitForMS 100;
		let WAIT_TIME=$WAIT_TIME-100;
	done

	return 2;
}

function WaitProcessStoped() {
	if [ $# -lt 1 ]; then
		return 1;
	fi

	WAIT_TIME=5000;
	PROC_NAME="$1"

	if [ $# -gt 1 ]; then
		WAIT_TIME=$2;
	fi

	while [ 1 -eq 1 ]; do
		CheckProcessRunning "$PROC_NAME";
		if [ 0 -eq $? ]; then
			return 0;
		fi
		
		if [ $WAIT_TIME -gt 0 ]; then
			WaitForMS 100;
			let WAIT_TIME=$WAIT_TIME-100;
		else
			return 2;
		fi
	done
	
	return 0;
}
