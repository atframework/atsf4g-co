#!/usr/bin/env python
# -*- coding: utf-8 -*-

import sys
import os
import ctypes
import platform
import cgi
import re
import hashlib
import stat
import codecs
import common.print_color

environment_check_shm = None
global_opts = None
global_cahce = dict(id_offset=0, services_type=[])
server_opts = None
server_name = ''
server_index = 1
server_alloc_listen_port = 0
server_proxy_addr = ''
server_cache_id = None
server_cache_full_name = None
server_cache_ip = dict()
project_templete_engine_lookup = None

def set_global_opts(opts, id_offset):
    global global_opts
    global global_cahce
    global_opts = opts
    global_cahce['id_offset'] = id_offset
    for server_type in sorted(opts.items('atservice'), key=lambda x: int(x[1])):
        if 'atgateway' == server_type[0]:
            continue
        svr_name = 'server.{0}'.format(server_type[0])
        if global_opts.has_section(svr_name):
            global_cahce['services_type'].append(server_type[0])

def set_templete_engine(engine):
    global project_templete_engine_lookup
    project_templete_engine_lookup = engine

def render_string(content, **render_options):
    from mako.template import Template
    tmpl = Template(content)
    return tmpl.render(**render_options)

def render(template_name, **render_options):
    if project_templete_engine_lookup is None:
        common.print_color.cprintf_stderr([common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD],
                                    'template not available now\r\n')
        return ""
    tmpl = project_templete_engine_lookup.get_template(template_name)
    if tmpl is None:
        common.print_color.cprintf_stderr([common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD],
                                    'template {0} not found\r\n', template_name)
        return ""

    return tmpl.render(**render_options)

def render_to(template_name, output_path, **render_options):
    dir_path = os.path.dirname(output_path)
    if not os.path.exists(dir_path):
        os.makedirs(dir_path, stat.S_IRWXU + stat.S_IRWXG + stat.S_IRWXO)

    output_file = codecs.open(output_path, mode='w', encoding='utf-8')
    if not output_file:
        common.print_color.cprintf_stderr([common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD],
                                    'try to render {0} but open {1} for writing failed\r\n', template_name, output_path)
        return

    output_file.write(render(template_name, **render_options))
    os.chmod(output_path, stat.S_IRWXU + stat.S_IRWXG + stat.S_IROTH + stat.S_IXOTH)

def get_service_index_range(number=1):
    return range(1 + global_cahce['id_offset'], 1 + global_cahce['id_offset'] + number)


def get_global_all_services():
    return global_cahce['services_type']


def set_server_inst(opts, key, index):
    global server_opts
    global server_name
    global server_index
    global server_cache_id
    global server_cache_full_name
    server_opts = opts
    server_name = key
    server_index = index
    server_cache_id = None
    server_cache_full_name = None

def get_ipv4_level(ip_addr):
    ip_addrs = [int(x) for x in ip_addr.split('.')]
    #                   => invalid ipv4 level 99
    if len(ip_addrs) != 4:
        return 99

    # 10.0.0.0/8        => private      level 1
    # 172.16.0.0/12     => private      level 2
    # 192.168.0.0/16    => private      level 3
    if ip_addrs[0] == 10:
        return 1
    if ip_addrs[0] == 172 and (ip_addrs[1] & 0x10) == 0x10:
        return 2
    if ip_addrs[0] == 192 and ip_addrs[1] == 168:
        return 3
    # 169.254.0.0/16    => link-local   level 11
    if ip_addrs[0] == 169 and ip_addrs[1] == 254:
        return 11
    # 127.0.0.0/8       => loopback     level 21
    if ip_addrs[0] == 127:
        return 21
    # 224.0.0.0/4       => group-cast   level 31
    if (ip_addrs[0] & 0xE0) == 0xE0:
        return 31
    # 240.0.0.0/4       => for-test     level 32
    if (ip_addrs[0] & 0xF0) == 0xF0:
        return 32
    # 255.255.255.255   => multi-cast   level 51
    if ip_addrs[0] == 255 and ip_addrs[1] == 255 and ip_addrs[2] == 255 and ip_addrs[3] == 255:
        return 51
    # public address    =>              level 0
    return 0

def is_ipv4_link_local(ip_addr):
    return get_ipv4_level(ip_addr) >= 11

def is_ipv6_link_local(ip_addr):
    ip_addr = ip_addr.lower()
    if ip_addr == "::1" or ip_addr == "0:0:0:0:0:0:0:1":
        return True

    # fe80:/10 => Link local address
    # FEC0:/10 => Site local address
    if len(ip_addr) > 4 and ip_addr[0:4] == "fe80":
        return True

    # IPv4-mapped IPv6 addresses
    if ip_addr == '::127.0.0.1' or ip_addr == '::ffff:127.0.0.1':
        return True

    return False


def get_ip_list_v4():
    global server_cache_ip
    if 'ipv4' not in server_cache_ip:
        import socket
        server_cache_ip['ipv4'] = []
        try:
            for ip_pair in socket.getaddrinfo(socket.gethostname(), 0, socket.AF_INET, socket.SOCK_STREAM):
                ip_addr = ip_pair[4][0]
                if not is_ipv4_link_local(ip_addr):
                    server_cache_ip['ipv4'].append(ip_addr)
            # use socket to detect ipv6 address if can not find any address
            if 0 == len(server_cache_ip['ipv4']):
                csock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                csock.connect(('8.8.8.8', 53))  # use google's DNS
                res = csock.getsockname()
                if res:
                    server_cache_ip['ipv4'].append(res[0])
                csock.close()
            server_cache_ip['ipv4'] = sorted(server_cache_ip['ipv4'], key=get_ipv4_level)
        except:
            pass
    return server_cache_ip['ipv4']


def get_ip_list_v6():
    global server_cache_ip
    if 'ipv6' not in server_cache_ip:
        import socket
        server_cache_ip['ipv6'] = []
        try:
            for ip_pair in socket.getaddrinfo(socket.gethostname(), 0, socket.AF_INET6, socket.SOCK_STREAM):
                ip_addr = ip_pair[4][0]
                interface_index = ip_addr.find('%')
                # remove interface name
                if interface_index > 0:
                    ip_addr = ip_addr[0:interface_index]
                if not is_ipv6_link_local(ip_addr):
                    server_cache_ip['ipv6'].append(ip_addr)
            # use socket to detect ipv6 address if can not find any address
            if 0 == len(server_cache_ip['ipv6']):
                csock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
                csock.connect(('2001:4860:4860::8888', 53))  # use google's DNS
                res = csock.getsockname()
                if res:
                    ip_addr = res[0]
                    interface_index = ip_addr.find('%')
                    # remove interface name
                    if interface_index > 0:
                        ip_addr = ip_addr[0:interface_index]
                    if not is_ipv6_link_local(ip_addr):
                        server_cache_ip['ipv6'].append(ip_addr)
                csock.close()
        except:
            pass
    return server_cache_ip['ipv6']


def is_ip_v6_enabled():
    ipv6s = get_ip_list_v6()
    return len(ipv6s) > 0


def get_inner_ipv4():
    if 'SYSTEM_MACRO_INNER_IPV4' in os.environ:
        return os.environ['SYSTEM_MACRO_INNER_IPV4']
    # detect inner ip address
    res = get_ip_list_v4()
    if 0 == len(res):
        return '127.0.0.1'
    return res[0]


def get_outer_ipv4():
    if 'SYSTEM_MACRO_OUTER_IPV4' in os.environ:
        return os.environ['SYSTEM_MACRO_OUTER_IPV4']
    # detect inner ip address
    res = get_ip_list_v4()
    if 0 == len(res):
        return '0.0.0.0'
    ret = res[0]
    if '127.0.0.1' == ret:
        ret = '0.0.0.0'
    return ret


def get_inner_ipv6():
    if 'SYSTEM_MACRO_INNER_IPV6' in os.environ:
        return os.environ['SYSTEM_MACRO_INNER_IPV6']
    # detect inner ip address
    res = get_ip_list_v6()
    if 0 == len(res):
        return '::1'
    return res[0]


def get_outer_ipv6():
    if 'SYSTEM_MACRO_OUTER_IPV6' in os.environ:
        return os.environ['SYSTEM_MACRO_OUTER_IPV6']
    # detect inner ip address
    res = get_ip_list_v6()
    if 0 == len(res):
        return '::'
    ret = res[0]
    if '::1' == ret:
        ret = '::'
    return ret


def get_global_option(section, key, default_val, env_name=None):
    global global_opts
    if not env_name is None and env_name in os.environ:
        return os.environ[env_name]

    if global_opts.has_option(section, key):
        return global_opts.get(section, key)

    return default_val


def get_hostname():
    global server_cache_ip
    if 'hostname' not in server_cache_ip:
        server_cache_ip['hostname'] = get_global_option(
            'atsystem', 'hostname', '', 'SYSTEM_MACRO_HOSTNAME')
        server_cache_ip['hostname_is_uuid'] = False
        if server_cache_ip['hostname'] is None or len(server_cache_ip['hostname']) == 0:
            # using uuid module to find physic address
            import uuid
            server_cache_ip['hostname'] = uuid.UUID(
                int=uuid.getnode()).hex[-12:]
            server_cache_ip['hostname_is_uuid'] = True
    return server_cache_ip['hostname'], server_cache_ip['hostname_is_uuid']


def str_to_list(val):
    ret = []
    if val is None:
        return ret
    for item in str(val).split(','):
        item_strip = item.strip()
        if len(item_strip) > 0:
            ret.append(item_strip)
    return ret


def list_to_hosts(val):
    ret = []
    mat = re.compile('(.*):(\d+)-(\d+)(.*)$')
    for item in val:
        mat_res = mat.match(item)
        if not mat_res is None:
            for i in range(int(mat_res.group(2)), int(mat_res.group(3)) + 1):
                ret.append('{0}:{1}{2}'.format(
                    mat_res.group(1), i, mat_res.group(4)))
        else:
            ret.append(item)
    return ret


def str_to_hosts(val):
    return list_to_hosts(str_to_list(val))


def get_global_list(section, key, default_val, env_name=None):
    res = get_global_option(section, key, default_val, env_name)
    if res is None:
        return []
    return str_to_list(res)


def get_global_list_to_hosts(section, key, default_val, env_name=None):
    res = get_global_list(section, key, default_val, env_name)
    return list_to_hosts(res)


def get_global_option_bool(section, key, default_val, env_name=None):
    val = get_global_option(section, key, default_val, env_name)
    if not val:
        return False
    val = str(val).lower().strip()
    return len(val) > 0 and '0' != val and 'false' != val and 'no' != val and 'disable' != val


def get_server_name():
    global server_name
    return server_name

def get_server_type_id(server_name=None):
    if server_name is None:
        server_name = get_server_name()
    if not global_opts.has_option('atservice', server_name):
        return 0
    return int(get_global_option('atservice', server_name, 0))

def get_server_option(key, default_val, env_name=None):
    return get_global_option('server.{0}'.format(get_server_name()), key, default_val, env_name)


def get_server_list(key, default_val, env_name=None):
    return get_global_list('server.{0}'.format(get_server_name()), key, default_val, env_name)


def get_server_list_to_hosts(key, default_val, env_name=None):
    return get_global_list_to_hosts('server.{0}'.format(get_server_name()), key, default_val, env_name)

def get_server_option_bool(key, default_val, env_name=None):
    return get_global_option_bool('server.{0}'.format(get_server_name()), key, default_val, env_name)

def get_server_or_global_option(section, key, default_val, env_name=None):
    ret = get_server_option('{0}.{1}'.format(section, key), None, None)
    if ret is None:
        return get_global_option(section, key, default_val, env_name)
    return ret

def get_server_or_global_list(section, key, default_val, env_name=None):
    ret = get_server_list('{0}.{1}'.format(section, key), None, None)
    if ret is None or len(ret) == 0:
        return get_global_list(section, key, default_val, env_name)
    return ret


def get_server_or_global_list_to_hosts(section, key, default_val, env_name=None):
    ret = get_server_list_to_hosts('{0}.{1}'.format(section, key), None, None)
    if ret is None or len(ret) == 0:
        return get_global_list_to_hosts(section, key, default_val, env_name)
    return ret

def get_server_or_global_bool(section, key, default_val, env_name=None):
    try_section_name = '{0}.{1}'.format(section, key)
    if get_server_option(try_section_name, None) is None:
        return get_global_option_bool(section, key, default_val, env_name)
    else:
        return get_server_option_bool(try_section_name, default_val, env_name)

def get_server_index():
    global server_index
    return server_index


def get_server_group_inner_id(server_name=None, server_index=None):
    global global_opts
    if server_name is None:
        server_name = get_server_name()
    if server_index is None:
        server_index = get_server_index()

    if not global_opts.has_option('atservice', server_name):
        return 0
    type_step = int(get_global_option('global', 'type_step', 0x100))
    type_id = int(get_global_option('atservice', server_name, 0))
    return type_step * type_id + server_index


def get_server_proc_id(server_name=None, server_index=None):
    group_id = int(get_global_option(
        'global', 'group_id', 1, 'SYSTEM_MACRO_GROUP_ID'))
    group_step = int(get_global_option('global', 'group_step',
                                       0x10000, 'SYSTEM_MACRO_GROUP_STEP'))
    return group_id * group_step + get_server_group_inner_id(server_name, server_index)


def get_server_id():
    global server_cache_id
    global global_opts
    if not server_cache_id is None:
        return server_cache_id

    if not global_opts.has_option('atservice', get_server_name()):
        return 0
    server_cache_id = get_server_proc_id()
    return server_cache_id


def get_server_full_name():
    global server_cache_full_name
    if not server_cache_full_name is None:
        return server_cache_full_name
    server_cache_full_name = '{0}-{1}'.format(
        get_server_name(), get_server_index())
    return server_cache_full_name


def get_log_level():
    return get_global_option('global', 'log_level', 'debug', 'SYSTEM_MACRO_CUSTOM_LOG_LEVEL')


def get_log_dir():
    return get_global_option('global', 'log_dir', '../log', 'SYSTEM_MACRO_CUSTOM_LOG_DIR')


def get_server_atbus_shm():
    global environment_check_shm
    if environment_check_shm is None:
        # check if it support shm
        if not os.path.exists('/proc/sys/kernel/shmmax'):
            environment_check_shm = False
        else:
            shm_max_sz = int(open('/proc/sys/kernel/shmmax', 'r').read())
            environment_check_shm = shm_max_sz > 0

    if not environment_check_shm:
        return None
    port_offset = int(get_global_option('global', 'port_offset', 0, 'SYSTEM_MACRO_GLOBAL_PORT_OFFSET'))
    base_key = int(get_global_option('atsystem', 'shm_key_pool', 0x16000000, 'SYSTEM_MACRO_CUSTOM_SHM_KEY'))
    shm_key_offset = int(get_global_option('atsystem', 'shm_key_offset', 0, 'SYSTEM_MACRO_CUSTOM_SHM_KEY_OFFSET'))
    shm_key = base_key + shm_key_offset + get_server_group_inner_id(get_server_name(), get_server_index()) + port_offset
    return 'shm://{0}'.format(hex(shm_key))

def disable_server_atbus_shm():
    global environment_check_shm
    environment_check_shm = False

def get_calc_listen_port(server_name=None, server_index=None, base_port='port'):
    if server_name is None:
        server_name = get_server_name()
    if server_index is None:
        server_index = get_server_index()

    ret = int(get_global_option(
        'server.{0}'.format(server_name), base_port, 0))
    port_offset = int(get_global_option('global', 'port_offset', 0, 'SYSTEM_MACRO_GLOBAL_PORT_OFFSET'))
    if ret == 0:
        base_port = int(get_global_option(
            'atsystem', 'listen_port', 12000, 'SYSTEM_MACRO_CUSTOM_BASE_PORT'))
        type_step = int(get_global_option('global', 'type_step', 0x100))
        type_id = int(get_global_option('atservice', server_name, 0))
        return base_port + type_step * server_index + type_id + port_offset
    else:
        return ret + server_index + port_offset


def get_server_atbus_port():
    return get_calc_listen_port()


def get_server_atbus_tcp():
    if is_ip_v6_enabled():
        if 'atproxy' == get_server_name():
            return 'ipv6://{0}:{1}'.format(get_outer_ipv6(), get_server_atbus_port())
        else:
            return 'ipv6://{0}:{1}'.format(get_inner_ipv6(), get_server_atbus_port())
    else:
        if 'atproxy' == get_server_name():
            return 'ipv4://{0}:{1}'.format(get_outer_ipv4(), get_server_atbus_port())
        else:
            return 'ipv4://{0}:{1}'.format(get_inner_ipv4(), get_server_atbus_port())


def get_server_atbus_unix():
    h = hashlib.sha1(__file__.encode('utf-8')).hexdigest()
    if os.path.exists('/run/tmp'):
        default_base = '/run/tmp/atapp/{0}/'.format(h)
    elif os.path.exists('/run'):
        default_base = '/run/atapp/{0}/'.format(h)
    elif os.path.exists('/tmp'):
        default_base = '/tmp/atapp/{0}/'.format(h)
    else:
        default_base = './'
    dir_path = get_global_option(
        'atsystem', 'unix_sock_dir', default_base, 'SYSTEM_MACRO_CUSTOM_UNIX_SOCK_DIR')
    return 'unix://{0}{1}-{2:x}.sock'.format(dir_path, get_server_full_name(), get_server_id())


def get_server_atbus_listen():
    global server_cache_ip
    ret = []
    res = get_server_atbus_shm()
    if not res is None:
        ret.append(res)

    if 'support_unix_sock' not in server_cache_ip:
        import socket
        if 'AF_UNIX' in socket.__dict__:
            # test unix sock, maybe defined but not available
            test_file_path = 'project-utils-test-unixx-sock.sock'
            try:
                test_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                test_sock.bind(test_file_path)
                test_sock.close()
                server_cache_ip['support_unix_sock'] = True
            except:
                server_cache_ip['support_unix_sock'] = False
            if os.path.exists(test_file_path):
                os.remove(test_file_path)
        else:
            server_cache_ip['support_unix_sock'] = False
    if 0 == len(ret) or False == server_cache_ip['support_unix_sock'] or 'atproxy' == get_server_name():
        ret.append(get_server_atbus_tcp())
    else:
        ret.append(get_server_atbus_unix())
    return ret

def disable_server_atbus_unix_sock():
    global server_cache_ip
    server_cache_ip['support_unix_sock'] = False

def get_server_proxy():
    global server_proxy_addr
    if 'atproxy' == get_server_name():
        server_proxy_addr = get_server_atbus_tcp()
        return ''
    return server_proxy_addr


def get_server_subnets():
    ret = []
    for subnet in get_server_list('subnets', '0/0'):
        if subnet.isdigit():
            ret.append('0/{0}'.format(subnet))
        else:
            ret.append(subnet)
    return ret


def get_server_recv_buffer_size():
    return get_global_option('atsystem', 'shm_channel_size', 8 * 1024 * 1024)


def get_server_send_buffer_size():
    return get_global_option('atsystem', 'iostream_channel_size', 2 * 1024 * 1024)


def get_server_gateway_index(server_name=None, server_index=None, gateway_name=None):
    if server_name is None:
        server_name = get_server_name()
    if server_index is None:
        server_index = get_server_index()

    if gateway_name is None:
        gateway_name = 'atgateway'
    gateway_section_name = 'server.{0}'.format(gateway_name)

    step = int(get_global_option(gateway_section_name, 'index_type_number', 1))
    offset = get_global_option(gateway_section_name, 'index_map_{0}'.format(server_name), None)
    if offset is None:
        raise Exception(
            'index_map_{0} is not found in {1}'.format(server_name, gateway_section_name))
    return step * server_index + int(offset)


def get_server_gateway_port(server_name, server_index, gateway_name=None, base_port='default_port'):
    if gateway_name is None:
        gateway_name = 'atgateway'
    gateway_section_name = 'server.{0}'.format(gateway_name)
    ret = int(get_global_option(gateway_section_name, base_port, 0))
    port_offset = int(get_global_option('global', 'port_offset', 0, 'SYSTEM_MACRO_GLOBAL_PORT_OFFSET'))
    if ret <= 0:
        ret = int(get_global_option('server.{0}'.format(gateway_name), 'default_port', 8000))
    return ret + get_server_gateway_index(server_name, server_index, gateway_name) + port_offset


def get_gateway_server_names(gateway_name=None):
    global global_opts
    ret = []
    if gateway_name is None:
        gateway_name = 'atgateway'
    for maybe_svr_name in global_opts.options('server.{0}'.format(gateway_name)):
        if maybe_svr_name[0:10] != "index_map_":
            continue
        ret.append(maybe_svr_name[10:])
    return ret

def get_etcd_client_urls():
    etcd_number = int(get_global_option('server.etcd', 'number', '0'))
    if etcd_number <= 0:
        return get_server_or_global_option('etcd', 'hosts', 'http://127.0.0.1:2379', 'SYSTEM_MACRO_CUSTOM_ETCD_HOST')
    
    client_urls = []
    for svr_index in get_service_index_range(etcd_number):
        client_urls.append('http://{0}:{1}'.format(get_outer_ipv4(), get_calc_listen_port('etcd', svr_index, 'client_port')))
    
    return ','.join(client_urls)

