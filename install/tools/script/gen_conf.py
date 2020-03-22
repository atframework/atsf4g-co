#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import os
import platform
import locale
import stat
import shutil
import re
import string
import codecs
from argparse import ArgumentParser

import glob

console_encoding = sys.getfilesystemencoding()
script_dir = os.path.dirname(os.path.realpath(__file__))

def merge_config_options(config, cmd_line):
    equal_idx = cmd_line.find("=")
    if equal_idx and equal_idx > 0:
        key = cmd_line[0:equal_idx]
        value = cmd_line[equal_idx + 1:]
        if len(value) >= 2 and (value[0:1] == '"' or value[0:1] == "'") and value[0:1] == value[-1:]:
            value = value[1:len(value)-1]
        key_groups = key.split(".")
        for i in range(1, len(key_groups)):
            section_name = ".".join(key_groups[0:i])
            option_name = ".".join(key_groups[i:])
            if config.has_option(section_name, option_name):
                config.set(section_name, option_name, value)
                return True
    return False

if __name__ == '__main__':
    sys.path.append(script_dir)
    python3_mode = sys.version_info[0] >= 3
    if python3_mode:
        import configparser
    else:
        import ConfigParser as configparser

    import common.print_color
    import common.project_utils as project

    os.chdir(script_dir)
    from mako.template import Template
    from mako.lookup import TemplateLookup
    project_template_dir = os.path.join(script_dir, 'helper', 'template')
    etc_template_dir = os.path.join(project_template_dir, 'etc')
    script_template_dir = os.path.join(project_template_dir, 'script')
    project_lookup = TemplateLookup(
        directories=[
            etc_template_dir, 
            script_template_dir, 
            os.path.join(project_template_dir, 'custom')
        ], 
        module_directory=os.path.join(script_dir, '.mako_modules-{0}.{1}.{2}'.format(sys.version_info[0], sys.version_info[1], sys.version_info[2])),
        input_encoding='utf-8'
    )
    project.set_templete_engine(project_lookup)

    parser = ArgumentParser(usage="usage: %(prog)s [options...]")
    parser.add_argument("-e", "--env-prefix", action='store', dest="env_prefix",
                        default='AUTOBUILD_', help="prefix when read parameters from environment variables")
    parser.add_argument("-c", "--config", action='store', dest="config", default=os.path.join(script_dir,
                                                                                              'config.conf'), help="configure file(default: {0})".format(os.path.join(script_dir, 'config.conf')))
    parser.add_argument("-s", "--set", action='append',
                        dest="set_vars", default=[], help="set configures")
    parser.add_argument("-n", "--number", action='store', dest="reset_number",
                        default=None, type=int, help="set default server numbers")
    parser.add_argument("-i", "--id-offset", action='store', dest="server_id_offset",
                        default=0, type=int, help="set server id offset(default: 0)")
    parser.add_argument("--disable-shm", action='store_true', dest="disable_shm",
                        default=False, help="disable shared memory address")
    parser.add_argument("--disable-unix-sock", action='store_true', dest="disable_unix_sock",
                        default=False, help="disable unix sock/pipe address")

    opts = parser.parse_args()
    if python3_mode:
        config = configparser.ConfigParser(inline_comment_prefixes=('#', ';'))
        config.read(opts.config, encoding="UTF-8")
    else:
        config = configparser.ConfigParser()
        config.read(opts.config)

    project.set_global_opts(config, opts.server_id_offset)

    # function switcher
    if opts.disable_shm:
        project.disable_server_atbus_shm()
    if opts.disable_unix_sock:
        project.disable_server_atbus_unix_sock()

    # all custon environment start with SYSTEM_MACRO_CUSTOM_
    # reset all servers's number
    if opts.reset_number is not None:
        for svr_name in project.get_global_all_services():
            config.set('server.{0}'.format(svr_name),
                       'number', str(opts.reset_number))

    # set all custom configures
    custom_tmpl_rule = re.compile('(?P<DIR>[^:]+):(?P<SRC>[^=]+)=(?P<DST>[^\\|]+)(\\|(?P<GLOBAL>[^\\|]+))?')
    custom_global_rule = re.compile('(?P<SRC>[^=]+)=(?P<DST>[^\\r\\n]+)')
    for cmd in opts.set_vars:
        if not merge_config_options(config, cmd):
            common.print_color.cprintf_stdout([common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD],
                                              'set command {0} invalid, must be SECTION.KEY=VALUE\r\n', cmd)

    # copy script templates
    all_service_temps = {
        'restart_all.sh': {
            'in': os.path.join(script_dir, 'helper', 'template', 'script', 'restart_all.template.sh'),
            'out': os.path.join(script_dir, 'restart_all.sh'),
            'content': []
        },
        'reload_all.sh': {
            'in': os.path.join(script_dir, 'helper', 'template', 'script', 'reload_all.template.sh'),
            'out': os.path.join(script_dir, 'reload_all.sh'),
            'content': []
        },
        'stop_all.sh': {
            'in': os.path.join(script_dir, 'helper', 'template', 'script', 'stop_all.template.sh'),
            'out': os.path.join(script_dir, 'stop_all.sh'),
            'content': [],
            'reverse': True
        }
    }

    for all_svr_temp in all_service_temps:
        all_temp_cfg = all_service_temps[all_svr_temp]
        shutil.copy2(all_temp_cfg['in'], all_temp_cfg['out'])
        os.chmod(all_temp_cfg['out'], stat.S_IRWXU +
                 stat.S_IRWXG + stat.S_IROTH + stat.S_IXOTH)

    def generate_service(svr_name, svr_index, install_prefix, section_name, **ext_options):
        project.set_server_inst(config.items(
            section_name), svr_name, svr_index)
        common.print_color.cprintf_stdout([common.print_color.print_style.FC_YELLOW, common.print_color.print_style.FW_BOLD],
                                          'start to generate etc and script of {0}-{1}\r\n', svr_name, svr_index)

        install_abs_prefix = os.path.normpath(os.path.join(script_dir, '..', '..', install_prefix))

        if not os.path.exists(os.path.join(install_abs_prefix, 'etc')):
            os.makedirs(os.path.join(install_abs_prefix, 'etc'))
        if not os.path.exists(os.path.join(install_abs_prefix, 'bin')):
            os.makedirs(os.path.join(install_abs_prefix, 'bin'))

        def generate_template(temp_dir, temp_path, out_dir, out_path, all_content_script=None):
            gen_in_path = os.path.join(temp_dir, temp_path)
            gen_out_path = os.path.join(install_abs_prefix, out_dir, out_path)
            if os.path.exists(gen_in_path):
                project.render_to(temp_path, gen_out_path, project_install_prefix=os.path.relpath(
                    '.', os.path.join(install_prefix, out_dir)), **ext_options)
                if all_content_script is not None and all_content_script in all_service_temps:
                    all_service_temps[all_content_script]['content'].append("""
# ==================== {0} ==================== 
if [ $# -eq 0 ] || [ "0" == "$(is_in_server_list {1} $*)" ]; then 
    bash {2}
fi
                    """.format(
                        project.get_server_full_name(),
                        project.get_server_full_name(),
                        os.path.relpath(gen_out_path, script_dir)
                    ))

        custom_rule_file_path = os.path.join(script_dir, 'helper', 'custom_template_rules', svr_name)
        if os.path.exists(custom_rule_file_path): # 自定义服务配置列表
            rules_data = project.render_string(
                codecs.open(custom_rule_file_path, mode='r', encoding='utf-8').read(), 
                **ext_options
            )
            for rule_line in rules_data.splitlines():
                rule_data = rule_line.strip()
                if not rule_data:
                    continue
                if rule_data[0:1] == "#" or rule_data[0:1] == ";":
                    continue
                mat_res = custom_tmpl_rule.match(rule_data)
                if mat_res is None:
                    common.print_color.cprintf_stderr(
                        [common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD], '"{0}" is not a valid custom service rule.\r\n', rule_line)
                    continue
                tmpl_dir = mat_res.group('DIR').strip()
                tmpl_src = mat_res.group('SRC').strip()
                tmpl_dst = mat_res.group('DST').strip()
                if mat_res.group('GLOBAL') is not None:
                    tmpl_global = mat_res.group('GLOBAL').strip()
                else:
                    tmpl_global = None
                generate_template(os.path.join(project_template_dir, tmpl_dir), tmpl_src, os.path.dirname(tmpl_dst), os.path.basename(tmpl_dst), tmpl_global)

        else: # 标准服务配置
            # etc
            generate_template(etc_template_dir, '{0}.conf'.format(
                svr_name), 'etc', '{0}-{1}.conf'.format(svr_name, svr_index))

            # scripts
            generate_template(script_template_dir, 'start.sh', 'bin',
                            'start-{0}.sh'.format(svr_index), 'restart_all.sh')
            generate_template(script_template_dir, 'stop.sh', 'bin',
                            'stop-{0}.sh'.format(svr_index), 'stop_all.sh')
            generate_template(script_template_dir, 'reload.sh', 'bin',
                            'reload-{0}.sh'.format(svr_index), 'reload_all.sh')
            generate_template(script_template_dir, 'debug.sh',
                            'bin', 'debug-{0}.sh'.format(svr_index))
            generate_template(script_template_dir, 'run.sh',
                            'bin', 'run-{0}.sh'.format(svr_index))

    # parse all services
    for svr_name in project.get_global_all_services():
        section_name = 'server.{0}'.format(svr_name)
        install_prefix = project.get_global_option(section_name, 'install_prefix', svr_name)
        for svr_index in project.get_service_index_range(int(project.get_global_option(section_name, 'number', 0))):
            generate_service(svr_name, svr_index, install_prefix, section_name)

    # special services - atgateway
    for svr_name in project.get_gateway_server_names('atgateway'):
        section_name = 'server.{0}'.format(svr_name)
        install_prefix = project.get_global_option('server.atgateway', 'install_prefix', 'atframe/atgateway')
        for svr_index in project.get_service_index_range(int(project.get_global_option(section_name, 'number', 0))):
            gateway_index = project.get_server_gateway_index(svr_name, svr_index, 'atgateway')
            for_server_id = project.get_server_proc_id(svr_name, svr_index)
            for_server_type_id = project.get_server_type_id(svr_name)
            generate_service('atgateway', gateway_index, install_prefix, 'server.atgateway',
                for_server_name=svr_name,
                for_server_index=svr_index,
                for_server_id=for_server_id,
                for_server_type_id=for_server_type_id
            )

    # custom global rules
    project_install_abs_path = os.path.normpath(os.path.join(script_dir, '..', '..'))
    for custom_global_rule_file in glob.glob(os.path.join(script_dir, 'helper', 'custom_global_rules', '*')):
        if os.path.exists(custom_global_rule_file): # 自定义全局模板列表
            rules_data = project.render_string(
                codecs.open(custom_global_rule_file, mode='r', encoding='utf-8').read()
            )
            for rule_line in rules_data.splitlines():
                rule_data = rule_line.strip()
                if not rule_data:
                    continue
                if rule_data[0:1] == "#" or rule_data[0:1] == ";":
                    continue
                mat_res = custom_global_rule.match(rule_data)
                if mat_res is None:
                    common.print_color.cprintf_stderr(
                        [common.print_color.print_style.FC_RED, common.print_color.print_style.FW_BOLD], '"{0}" is not a valid global rule.\r\n', rule_line)
                    continue
                tmpl_src = mat_res.group('SRC').strip()
                tmpl_dst = mat_res.group('DST').strip()
                if not tmpl_src or not tmpl_dst:
                    continue
                
                gen_out_path = os.path.join(project_install_abs_path, tmpl_dst)
                project.render_to(tmpl_src, gen_out_path, 
                    project_install_prefix=os.path.relpath(project_install_abs_path, os.path.dirname(gen_out_path))
                )


    for all_svr_temp in all_service_temps:
        all_temp_cfg = all_service_temps[all_svr_temp]
        if 'reverse' in all_temp_cfg and all_temp_cfg['reverse']:
            all_temp_cfg['content'].reverse()
        open(all_temp_cfg['out'], mode='a').write(os.linesep.join(all_temp_cfg['content']))

    common.print_color.cprintf_stdout(
        [common.print_color.print_style.FC_YELLOW, common.print_color.print_style.FW_BOLD], 'all jobs done.\r\n')
