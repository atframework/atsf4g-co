#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Only support python implement

import glob
import os
import sys
import codecs
import re
import shutil
import sysconfig
import tempfile
from subprocess import PIPE, Popen

HANDLE_SPLIT_PBFIELD_RULE = re.compile("\\d+|_+|\\s+|\\-")
HANDLE_SPLIT_MODULE_RULE = re.compile("\\.|\\/|\\\\")
HANDLE_NUMBER_RULE = re.compile("^\\d+$")
LOCAL_PB_DB_CACHE = dict()
LOCAL_PROJECT_VCS_CACHE = dict()


def check_has_module(module_name):
    """Check module exists"""
    try:
        if sys.version_info[0] == 2:
            import imp

            return imp.find_module(module_name) is not None
        elif sys.version_info[0] == 3 and sys.version_info[1] < 4:
            import importlib

            return importlib.find_loader(module_name) is not None
        else:
            from importlib import util

            return util.find_spec(module_name) is not None
    except EnvironmentError:
        return False


class MakoModuleTempDir:
    """ RAII: Auto remove tempory directory """
    def __init__(self, prefix_path):
        if not os.path.exists(prefix_path):
            os.makedirs(prefix_path)
        self.directory_path = tempfile.mkdtemp(suffix="",
                                               prefix="",
                                               dir=prefix_path)

    def __del__(self):
        if (self.directory_path is not None
                and os.path.exists(self.directory_path)
                and os.path.isdir(self.directory_path)):
            shutil.rmtree(self.directory_path, ignore_errors=True)
            self.directory_path = None


def split_segments_for_protobuf_field_name(input_name):
    """Split field name rule"""
    ret = []
    before_start = 0
    for iter in HANDLE_SPLIT_PBFIELD_RULE.finditer(input_name):
        if iter.start() > before_start:
            ret.append(input_name[before_start:iter.start()])
        val = input_name[iter.start():iter.end()].strip()
        if val and val[0:1] != "_" and val[0:1] != "-":
            ret.append(val)
        before_start = iter.end()
    if len(input_name) > before_start:
        ret.append(input_name[before_start:])
    return ret


def add_package_prefix_paths(packag_paths):
    """See https://docs.python.org/3/install/#how-installation-works"""
    append_paths = []
    for path in packag_paths:
        add_package_bin_path = os.path.join(path, "bin")
        if os.path.exists(add_package_bin_path):
            if sys.platform.lower() == "win32":
                os.environ[
                    "PATH"] = add_package_bin_path + ";" + os.environ["PATH"]
            else:
                os.environ[
                    "PATH"] = add_package_bin_path + ":" + os.environ["PATH"]

        add_package_lib_path = os.path.join(
            path,
            "lib",
            "python{0}".format(sysconfig.get_python_version()),
            "site-packages",
        )
        if os.path.exists(add_package_lib_path):
            append_paths.append(add_package_lib_path)

        add_package_lib64_path = os.path.join(
            path,
            "lib64",
            "python{0}".format(sysconfig.get_python_version()),
            "site-packages",
        )
        if os.path.exists(add_package_lib64_path):
            append_paths.append(add_package_lib64_path)

        add_package_lib_path_for_win = os.path.join(path, "Lib",
                                                    "site-packages")
        if os.path.exists(add_package_lib_path_for_win):
            append_paths.append(add_package_lib_path_for_win)
    append_paths.extend(sys.path)
    sys.path = append_paths


class PbConvertRule:
    CONVERT_NAME_NOT_CHANGE = 0
    CONVERT_NAME_LOWERCASE = 1
    CONVERT_NAME_UPPERCASE = 2
    CONVERT_NAME_CAMEL_FIRST_LOWERCASE = 3
    CONVERT_NAME_CAMEL_CAMEL = 4


class PbObjectBase(object):
    def __init__(self, descriptor, refer_database):
        self.descriptor = descriptor
        self.refer_database = refer_database
        self._reflect_extensions = None
        self._refer_raw_proto = None
        self._cache_name_lower_rule = None
        self._cache_name_upper_rule = None

    def get_identify_name(self,
                          name,
                          mode=PbConvertRule.CONVERT_NAME_LOWERCASE,
                          package_seperator="."):
        if name is None:
            return None
        res = []
        for segment in filter(lambda x: x.strip(),
                              HANDLE_SPLIT_MODULE_RULE.split(name)):
            groups = [
                x.strip()
                for x in split_segments_for_protobuf_field_name(segment)
            ]
            sep = ""
            if mode == PbConvertRule.CONVERT_NAME_LOWERCASE:
                groups = [y for y in map(lambda x: x.lower(), groups)]
                sep = "_"
            if mode == PbConvertRule.CONVERT_NAME_UPPERCASE:
                groups = [y for y in map(lambda x: x.upper(), groups)]
                sep = "_"
            if (mode == PbConvertRule.CONVERT_NAME_CAMEL_FIRST_LOWERCASE
                    or mode == PbConvertRule.CONVERT_NAME_CAMEL_CAMEL):
                groups = [
                    y for y in map(lambda x: (x[0:1].upper() + x[1:].lower()),
                                   groups)
                ]
            if mode == PbConvertRule.CONVERT_NAME_CAMEL_FIRST_LOWERCASE and groups:
                groups[0] = groups[0].lower()
            res.append(sep.join(groups))
        return package_seperator.join(res)

    def get_identify_lower_rule(self, name):
        return self.get_identify_name(name,
                                      PbConvertRule.CONVERT_NAME_LOWERCASE,
                                      "_")

    def get_identify_upper_rule(self, name):
        return self.get_identify_name(name,
                                      PbConvertRule.CONVERT_NAME_UPPERCASE,
                                      "_")

    def get_name_lower_rule(self):
        if self._cache_name_lower_rule is None:
            self._cache_name_lower_rule = self.get_identify_lower_rule(
                self.get_name())
        return self._cache_name_lower_rule

    def get_name_upper_rule(self):
        if self._cache_name_upper_rule is None:
            self._cache_name_upper_rule = self.get_identify_upper_rule(
                self.get_name())
        return self._cache_name_upper_rule

    def _expand_extension_message(self, prefix, full_prefix, ext_value):
        pass

    def _get_extensions(self):
        if self._reflect_extensions is not None:
            return self._reflect_extensions
        self._reflect_extensions = dict()

        if not self.descriptor.GetOptions():
            return self._reflect_extensions

        for ext_handle in self.descriptor.GetOptions().Extensions:
            ext_value = self.descriptor.GetOptions().Extensions[ext_handle]
            self._reflect_extensions[ext_handle.name] = ext_value
            self._reflect_extensions[ext_handle.full_name] = ext_value
        return self._reflect_extensions

    def _get_raw_proto(self):
        if self._refer_raw_proto is not None:
            return self._refer_raw_proto
        self._refer_raw_proto = self.refer_database.get_raw_symbol(
            self.get_full_name())
        return self._refer_raw_proto

    def get_extension(self, name, default_value=None):
        current_object = self._get_extensions()
        if name in current_object:
            return current_object[name]
        return default_value

    def get_extension_field(self, name, fn, default_value=None):
        current_object = self.get_extension(name, None)
        if current_object is None:
            return default_value
        if fn:
            if callable(fn):
                current_object = fn(current_object)
            else:
                current_object = fn
        if current_object:
            return current_object
        return default_value

    def is_valid(self, ignore_request):
        return True

    def get_name(self):
        return self.descriptor.name

    def get_full_name(self):
        return self.descriptor.full_name

    def get_cpp_class_name(self):
        return self.get_full_name().replace(".", "::")

    def get_cpp_namespace_begin(self, full_name, pretty_ident="  "):
        current_ident = ""
        ret = []
        for name in HANDLE_SPLIT_MODULE_RULE.split(full_name):
            ret.append("{0}namespace {1}".format(current_ident, name) + " {")
            current_ident = current_ident + pretty_ident
        return ret

    def get_cpp_namespace_end(self, full_name, pretty_ident="  "):
        current_ident = ""
        ret = []
        for name in HANDLE_SPLIT_MODULE_RULE.split(full_name):
            ret.append(current_ident + "}  // namespace " + name)
            current_ident = current_ident + pretty_ident
        ret.reverse()
        return ret

    def get_package(self):
        return self.descriptor.file.package


class PbFile(PbObjectBase):
    def __init__(self, descriptor, refer_database):
        super(PbFile, self).__init__(descriptor, refer_database)
        refer_database._cache_files[descriptor.name] = self

    def get_package(self):
        return self.descriptor.package

    def get_full_name(self):
        return self.get_name()


class PbField(PbObjectBase):
    def __init__(self, container_message, descriptor, refer_database):
        super(PbField, self).__init__(descriptor, refer_database)
        self.file = container_message.file
        self.container = container_message

    def is_valid(self, ignore_request):
        if ignore_request:
            if self.descriptor.full_name in ignore_request:
                return False
        return True


class PbOneof(PbObjectBase):
    def __init__(self, container_message, fields, descriptor, refer_database):
        super(PbOneof, self).__init__(descriptor, refer_database)
        self.file = container_message.file
        self.container = container_message
        self.fields = fields
        self.fields_by_name = dict()
        self.fields_by_number = dict()

        for field in fields:
            self.fields_by_name[field.descriptor.name] = field
            self.fields_by_number[field.descriptor.number] = field

    def get_package(self):
        return self.container.get_package()


class PbMessage(PbObjectBase):
    def __init__(self, file, descriptor, refer_database):
        super(PbMessage, self).__init__(descriptor, refer_database)
        refer_database._cache_messages[descriptor.full_name] = self

        self.file = file
        self.fields = []
        self.fields_by_name = dict()
        self.fields_by_number = dict()
        self.oneofs = []
        self.oneofs_by_name = dict()

        for field_desc in descriptor.fields:
            field = PbField(self, field_desc, refer_database)
            self.fields.append(field)
            self.fields_by_name[field_desc.name] = field
            self.fields_by_number[field_desc.number] = field

        for oneof_desc in descriptor.oneofs:
            sub_fields = []
            for sub_field_desc in oneof_desc.fields:
                sub_fields.append(self.fields_by_number[sub_field_desc.number])
            oneof = PbOneof(self, sub_fields, oneof_desc, refer_database)
            self.oneofs.append(oneof)
            self.oneofs_by_name[oneof_desc.name] = oneof


class PbEnumValue(PbObjectBase):
    def __init__(self, container_enum, descriptor, refer_database):
        super(PbEnumValue, self).__init__(descriptor, refer_database)
        self.file = container_enum.file
        self.container = container_enum

    def get_package(self):
        return self.container.get_package()

    def get_full_name(self):
        return "{0}.{1}".format(self.container.get_full_name(),
                                self.get_name())


class PbEnum(PbObjectBase):
    def __init__(self, file, descriptor, refer_database):
        super(PbEnum, self).__init__(descriptor, refer_database)
        refer_database._cache_enums[descriptor.full_name] = self

        self.file = file
        self.values = []
        self.values_by_name = dict()
        self.values_by_number = dict()

        for value_desc in descriptor.values:
            value = PbEnumValue(self, value_desc, refer_database)
            self.values.append(value)
            self.values_by_name[value_desc.name] = value
            self.values_by_number[value_desc.number] = value


class PbRpc(PbObjectBase):
    def __init__(self, service, descriptor, refer_database):
        super(PbRpc, self).__init__(descriptor, refer_database)

        self.service = service
        self.file = service.file
        self._request = None
        self._response = None

    def is_valid(self, ignore_request):
        if ignore_request:
            if self.descriptor.input_type.full_name in ignore_request:
                return False
        return True

    def get_name(self):
        return self.descriptor.name

    def get_package(self):
        return self.service.get_package()

    def get_request(self):
        if self._request is not None:
            return self._request

        self._request = self.refer_database.get_message(
            self.descriptor.input_type.full_name)
        return self._request

    def get_request_descriptor(self):
        return self.get_request().descriptor

    def get_request_extension(self, name, default_value=None):
        return self.get_request().get_extension(name, default_value)

    def get_response(self):
        if self._response is not None:
            return self._response

        self._response = self.refer_database.get_message(
            self.descriptor.output_type.full_name)
        return self._response

    def get_response_descriptor(self):
        return self.get_response().descriptor

    def get_response_extension(self, name, default_value=None):
        return self.get_response().get_extension(name, default_value)

    def is_request_stream(self):
        raw_sym = self._get_raw_proto()
        if raw_sym is None:
            return False
        return raw_sym.client_streaming

    def is_response_stream(self):
        raw_sym = self._get_raw_proto()
        if raw_sym is None:
            return False
        return raw_sym.server_streaming


class PbService(PbObjectBase):
    def __init__(self, file, descriptor, refer_database):
        super(PbService, self).__init__(descriptor, refer_database)
        refer_database._cache_services[descriptor.full_name] = self

        self.rpcs = dict()
        self.file = file

        for method in descriptor.methods:
            rpc = PbRpc(self, method, refer_database)
            self.rpcs[rpc.get_name()] = rpc

    def get_name(self):
        return self.descriptor.name


class PbDatabase(object):
    def __init__(self):
        from google.protobuf import descriptor_pb2 as pb2
        from google.protobuf import message_factory as _message_factory

        self.raw_files = dict()
        self.raw_symbols = dict()
        self.raw_factory = _message_factory.MessageFactory()
        self.extended_factory = _message_factory.MessageFactory()
        self._cache_files = dict()
        self._cache_messages = dict()
        self._cache_enums = dict()
        self._cache_services = dict()

    def _register_by_pb_fds(self, factory, file_protos):
        file_by_name = {
            file_proto.name: file_proto
            for file_proto in file_protos
        }

        def _AddFile(file_proto):
            for dependency in file_proto.dependency:
                if dependency in file_by_name:
                    # Remove from elements to be visited, in order to cut cycles.
                    _AddFile(file_by_name.pop(dependency))
            factory.pool.Add(file_proto)

        while file_by_name:
            _AddFile(file_by_name.popitem()[1])
        return factory.GetMessages(
            [file_proto.name for file_proto in file_protos])

    def _extended_raw_message(self, package, message_proto):
        self.raw_symbols["{0}.{1}".format(package,
                                          message_proto.name)] = message_proto
        for enum_type in message_proto.enum_type:
            self._extended_raw_enum(
                "{0}.{1}".format(package, message_proto.name), enum_type)
        for nested_type in message_proto.nested_type:
            self._extended_raw_message(
                "{0}.{1}".format(package, message_proto.name), nested_type)
        for extension in message_proto.extension:
            self.raw_symbols["{0}.{1}.{2}".format(package, message_proto.name,
                                                  extension.name)] = extension
        for field in message_proto.field:
            self.raw_symbols["{0}.{1}.{2}".format(package, message_proto.name,
                                                  field.name)] = field
        for oneof_decl in message_proto.oneof_decl:
            self.raw_symbols["{0}.{1}.{2}".format(
                package, message_proto.name, oneof_decl.name)] = oneof_decl

    def _extended_raw_enum(self, package, enum_type):
        self.raw_symbols["{0}.{1}".format(package, enum_type.name)] = enum_type
        for enum_value in enum_type.value:
            self.raw_symbols["{0}.{1}.{2}".format(
                package, enum_type.name, enum_value.name)] = enum_value

    def _extended_raw_service(self, package, service_proto):
        self.raw_symbols["{0}.{1}".format(package,
                                          service_proto.name)] = service_proto
        for method in service_proto.method:
            self.raw_symbols["{0}.{1}.{2}".format(package, service_proto.name,
                                                  method.name)] = method

    def _extended_raw_file(self, file_proto):
        for enum_type in file_proto.enum_type:
            self._extended_raw_enum(file_proto.package, enum_type)
        for extension in file_proto.extension:
            self.raw_symbols["{0}.{1}".format(file_proto.package,
                                              extension.name)] = extension
        for message_type in file_proto.message_type:
            self._extended_raw_message(file_proto.package, message_type)
        for service in file_proto.service:
            self._extended_raw_service(file_proto.package, service)

    def load(self, pb_file_path):
        pb_file_buffer = open(pb_file_path, "rb").read()
        from google.protobuf import (
            descriptor_pb2,
            any_pb2,
            api_pb2,
            duration_pb2,
            empty_pb2,
            field_mask_pb2,
            source_context_pb2,
            struct_pb2,
            timestamp_pb2,
            type_pb2,
            wrappers_pb2,
        )
        from google.protobuf import message_factory as _message_factory

        pb_fds = descriptor_pb2.FileDescriptorSet.FromString(pb_file_buffer)
        pb_fds_patched = [x for x in pb_fds.file]
        pb_fds_inner = []
        protobuf_inner_descriptors = dict({
            descriptor_pb2.DESCRIPTOR.name:
            descriptor_pb2.DESCRIPTOR.serialized_pb,
            any_pb2.DESCRIPTOR.name:
            any_pb2.DESCRIPTOR.serialized_pb,
            api_pb2.DESCRIPTOR.name:
            api_pb2.DESCRIPTOR.serialized_pb,
            duration_pb2.DESCRIPTOR.name:
            duration_pb2.DESCRIPTOR.serialized_pb,
            empty_pb2.DESCRIPTOR.name:
            empty_pb2.DESCRIPTOR.serialized_pb,
            field_mask_pb2.DESCRIPTOR.name:
            field_mask_pb2.DESCRIPTOR.serialized_pb,
            source_context_pb2.DESCRIPTOR.name:
            source_context_pb2.DESCRIPTOR.serialized_pb,
            struct_pb2.DESCRIPTOR.name:
            struct_pb2.DESCRIPTOR.serialized_pb,
            timestamp_pb2.DESCRIPTOR.name:
            timestamp_pb2.DESCRIPTOR.serialized_pb,
            type_pb2.DESCRIPTOR.name:
            type_pb2.DESCRIPTOR.serialized_pb,
            wrappers_pb2.DESCRIPTOR.name:
            wrappers_pb2.DESCRIPTOR.serialized_pb,
        })
        for x in pb_fds_patched:
            if x.name in protobuf_inner_descriptors:
                protobuf_inner_descriptors[x.name] = None

        for patch_inner_name in protobuf_inner_descriptors:
            patch_inner_pb_data = protobuf_inner_descriptors[patch_inner_name]
            if patch_inner_pb_data is not None:
                pb_fds_inner.append(
                    descriptor_pb2.FileDescriptorProto.FromString(
                        patch_inner_pb_data))
        pb_fds_patched.extend(pb_fds_inner)
        msg_set = self._register_by_pb_fds(self.raw_factory, pb_fds_patched)

        # Use extensions in raw_factory to build extended_factory
        try:
            pb_fds_clazz = msg_set["google.protobuf.FileDescriptorSet"]
        except Exception as e:
            from print_color import print_style, cprintf_stderr

            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[ERROR]: get symbol google.protobuf.FileDescriptorSet failed. system error\n",
            )
            import traceback

            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[ERROR]: {0}.\n{1}\n",
                str(e),
                traceback.format_exc(),
            )
            return

        # from google.protobuf.text_format import MessageToString
        pb_fds = pb_fds_clazz.FromString(pb_file_buffer)
        for file_proto in pb_fds.file:
            self.raw_files[file_proto.name] = file_proto
            self._extended_raw_file(file_proto)
        pb_fds_patched = [x for x in pb_fds.file]
        pb_fds_patched.extend(pb_fds_inner)
        self._register_by_pb_fds(self.extended_factory, pb_fds_patched)

        # Clear all caches
        self._cache_files.clear()
        self._cache_enums.clear()
        self._cache_messages.clear()
        self._cache_services.clear()

    def get_raw_file_descriptors(self):
        return self.raw_files

    def get_raw_symbol(self, full_name):
        if full_name in self.raw_symbols:
            return self.raw_symbols[full_name]
        return None

    def get_file(self, name):
        if name in self._cache_files:
            return self._cache_files[name]

        file_desc = self.extended_factory.pool.FindFileByName(name)
        if file_desc is None:
            return None

        return PbFile(file_desc, self)

    def get_service(self, full_name):
        if not full_name:
            return None
        if full_name in self._cache_services:
            return self._cache_services[full_name]
        target_desc = self.extended_factory.pool.FindServiceByName(full_name)
        if target_desc is None:
            return None
        file_obj = self.get_file(target_desc.file.name)
        if file_obj is None:
            return None
        return PbService(file_obj, target_desc, self)

    def get_message(self, full_name):
        if not full_name:
            return None
        if full_name in self._cache_messages:
            return self._cache_messages[full_name]
        target_desc = self.extended_factory.pool.FindMessageTypeByName(
            full_name)
        if target_desc is None:
            return None
        file_obj = self.get_file(target_desc.file.name)
        if file_obj is None:
            return None
        return PbMessage(file_obj, target_desc, self)

    def get_enum(self, full_name):
        if not full_name:
            return None
        if full_name in self._cache_enums:
            return self._cache_enums[full_name]
        target_desc = self.extended_factory.pool.FindEnumTypeByName(full_name)
        if target_desc is None:
            return None
        file_obj = self.get_file(target_desc.file.name)
        if file_obj is None:
            return None
        return PbEnum(file_obj, target_desc, self)


def parse_generate_rule(rule):
    # rules from program options
    if type(rule) is str:
        dot_pos = rule.find(":")
        if dot_pos <= 0 or dot_pos > len(rule):
            temp_path = rule
            if temp_path.endswith(".mako"):
                rule = os.path.basename(temp_path[0:len(temp_path) - 5])
            else:
                rule = os.path.basename(temp_path)
        else:
            temp_path = rule[0:dot_pos]
            rule = rule[(dot_pos + 1):]

        dolar_pos = rule.find("$")
        if dolar_pos >= 0 and dolar_pos < len(rule):
            return (temp_path, rule, True, None)
        return (temp_path, rule, False, None)
    # rules from yaml
    rewrite_overwrite = None
    output_render = False
    if "overwrite" in rule:
        rewrite_overwrite = rule["overwrite"]
    input = rule["input"]
    if "output" in rule and rule["output"]:
        output = rule["output"]
    else:
        if input.endswith(".mako"):
            output = os.path.basename(input[0:len(input) - 5])
        else:
            output = os.path.basename(input)
    dolar_pos = output.find("$")
    if dolar_pos >= 0 and dolar_pos < len(output):
        output_render = True
    return (input, output, output_render, rewrite_overwrite)


if sys.version_info[0] == 2:

    def CmdArgsGetParser(usage):
        reload(sys)
        sys.setdefaultencoding("utf-8")
        from optparse import OptionParser

        return OptionParser("usage: %prog " + usage)

    def CmdArgsAddOption(parser, *args, **kwargs):
        parser.add_option(*args, **kwargs)

    def CmdArgsParse(parser):
        return parser.parse_args()

else:

    def CmdArgsGetParser(usage):
        import argparse

        ret = argparse.ArgumentParser(usage="%(prog)s " + usage)
        ret.add_argument("REMAINDER",
                         nargs=argparse.REMAINDER,
                         help="task names")
        return ret

    def CmdArgsAddOption(parser, *args, **kwargs):
        parser.add_argument(*args, **kwargs)

    def CmdArgsParse(parser):
        ret = parser.parse_args()
        return (ret, ret.REMAINDER)


def try_read_vcs_username(project_dir):
    if project_dir in LOCAL_PROJECT_VCS_CACHE:
        return LOCAL_PROJECT_VCS_CACHE[project_dir]
    local_vcs_user_name = None
    try:
        pexec = Popen(
            ["git", "config", "user.name"],
            stdin=None,
            stdout=PIPE,
            stderr=None,
            cwd=project_dir,
            shell=False,
        )
        local_vcs_user_name = pexec.stdout.read().decode("utf-8").strip()
        pexec.stdout.close()
        pexec.wait()
    except EnvironmentError:
        pass
    if local_vcs_user_name is None:
        local_vcs_user_name = os.path.basename(__file__)

    LOCAL_PROJECT_VCS_CACHE[project_dir] = local_vcs_user_name
    return local_vcs_user_name


def get_pb_db_with_cache(pb_file):
    global LOCAL_PB_DB_CACHE
    pb_file = os.path.realpath(pb_file)
    if pb_file in LOCAL_PB_DB_CACHE:
        ret = LOCAL_PB_DB_CACHE[pb_file]
        return ret
    ret = PbDatabase()
    ret.load(pb_file)
    LOCAL_PB_DB_CACHE[pb_file] = ret
    return ret


def get_real_output_directory_and_custom_variables(options, yaml_conf_item,
                                                   origin_custom_vars):
    if yaml_conf_item is None:
        return (options.output_dir, origin_custom_vars)

    if "custom_variables" in yaml_conf_item:
        conf_custom_variables = yaml_conf_item["custom_variables"]
        local_custom_variables = dict()
        for key in origin_custom_vars:
            local_custom_variables[key] = origin_custom_vars[key]
        for key in conf_custom_variables:
            local_custom_variables[key] = conf_custom_variables[key]
    else:
        local_custom_variables = origin_custom_vars
    if "output_directory" in yaml_conf_item:
        output_directory = yaml_conf_item["output_directory"]
    else:
        output_directory = options.output_dir

    return (output_directory, local_custom_variables)


def get_yaml_configure_child(yaml_conf_item,
                             name,
                             default_value,
                             transfer_into_array=False):
    if name in yaml_conf_item:
        ret = yaml_conf_item[name]
        if transfer_into_array and type(ret) is not list:
            return [ret]
        return ret
    return default_value


class PbGroupGenerator(object):
    def __init__(
        self,
        database,
        project_dir,
        output_directory,
        custom_variables,
        overwrite,
        outer_name,
        inner_name,
        inner_set_name,
        inner_include_rule,
        inner_exclude_rule,
        outer_templates,
        inner_templates,
        outer_inst,
        inner_name_map,
        inner_ignore_types
    ):
        self.database = database
        self.project_dir = project_dir
        self.outer_name = outer_name
        self.inner_name = inner_name
        self.inner_set_name = inner_set_name
        self.inner_include_rule = inner_include_rule
        self.inner_exclude_rule = inner_exclude_rule
        self.outer_templates = outer_templates
        self.inner_templates = inner_templates
        self.outer_inst = outer_inst
        self.inner_name_map = inner_name_map
        self.inner_ignore_types = inner_ignore_types
        self.output_directory = output_directory
        self.custom_variables = custom_variables
        self.overwrite = overwrite
        self.local_vcs_user_name = try_read_vcs_username(project_dir)

def write_code_if_different(output_file, encoding, data):
    content_changed = False
    if not os.path.exists(output_file):
        content_changed = True
    else:
        old_data = codecs.open(output_file, mode="r", encoding=encoding).read()
        if old_data != data:
            content_changed = True

    if content_changed:
        codecs.open(output_file, mode="w", encoding=encoding).write(data)

def generate_group(options, group):
    # type: (argparse.Namespace, PbGroupGenerator) -> None
    if group.outer_inst is None:
        return

    from print_color import print_style, cprintf_stdout, cprintf_stderr

    # render templates
    from mako.template import Template
    from mako.lookup import TemplateLookup

    make_module_cache_dir = os.path.join(
        group.project_dir,
        ".mako_modules-{0}.{1}.{2}".format(sys.version_info[0],
                                           sys.version_info[1],
                                           sys.version_info[2]),
    )

    inner_include_rule = None
    try:
        if group.inner_include_rule is not None:
            inner_include_rule = re.compile(group.inner_include_rule)
    except Exception as e:
        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            "[ERROR]: invild {0} include rule {1}, we will ignore it.\n",
            group.inner_name,
            group.inner_include_rule,
        )
        import traceback

        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            "[ERROR]: {0}.\n{1}\n",
            str(e),
            traceback.format_exc(),
        )
    inner_exclude_rule = None
    try:
        if group.inner_exclude_rule is not None:
            inner_exclude_rule = re.compile(group.inner_exclude_rule)
    except Exception as e:
        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            "[ERROR]: invild {0} exclude rule {1}, we will ignore it.\n",
            group.inner_name,
            group.inner_exclude_rule,
        )
        import traceback

        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            "[ERROR]: {0}.\n{1}\n",
            str(e),
            traceback.format_exc(),
        )

    selected_inner_items = dict()
    for inner_key in group.inner_name_map:
        inner_obj = group.inner_name_map[inner_key]
        if inner_include_rule is not None:
            if inner_include_rule.match(inner_obj.get_name()) is None:
                continue
        if inner_exclude_rule is not None:
            if inner_exclude_rule.match(inner_obj.get_name()) is not None:
                continue
        if not inner_obj.is_valid(group.inner_ignore_types):
            continue
        selected_inner_items[inner_key] = inner_obj

    # generate global templates
    for outer_rule in group.outer_templates:
        render_args = {
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": group.local_vcs_user_name,
            group.outer_name: group.outer_inst,
            group.inner_set_name: selected_inner_items,
            "output_file_path": None,
            "output_render_path": None,
            "current_instance": group.outer_inst,
            "PbConvertRule": PbConvertRule,
        }
        for k in group.custom_variables:
            render_args[k] = group.custom_variables[k]

        try:
            (
                intput_template,
                output_rule,
                output_render,
                rewrite_overwrite,
            ) = parse_generate_rule(outer_rule)
            if not os.path.exists(intput_template):
                cprintf_stderr(
                    [print_style.FC_RED, print_style.FW_BOLD],
                    "[INFO]: template file {0} not found.\n",
                    intput_template,
                )
                continue

            lookup = TemplateLookup(
                directories=[os.path.dirname(intput_template)],
                module_directory=make_module_cache_dir,
            )
            if output_render:
                output_file = Template(output_rule,
                                       lookup=lookup).render(**render_args)
            else:
                output_file = output_rule
            render_args["output_render_path"] = output_file

            if group.output_directory:
                output_file = os.path.join(group.output_directory, output_file)
            elif options.output_dir:
                output_file = os.path.join(options.output_dir, output_file)

            if options.print_output_files:
                print(output_file)
            else:
                if os.path.exists(output_file):
                    force_overwrite = rewrite_overwrite
                    if force_overwrite is None:
                        force_overwrite = group.overwrite
                    if force_overwrite is None:
                        force_overwrite = not options.no_overwrite
                    if not force_overwrite:
                        if not options.quiet:
                            cprintf_stdout(
                                [print_style.FC_YELLOW, print_style.FW_BOLD],
                                "[INFO]: file {0} is already exists, we will ignore generating template {1} to it.\n",
                                output_file,
                                intput_template,
                            )
                        continue

                render_args["output_file_path"] = output_file
                source_tmpl = lookup.get_template(
                    os.path.basename(intput_template))
                final_output_dir = os.path.dirname(output_file)
                if final_output_dir and not os.path.exists(final_output_dir):
                    os.makedirs(final_output_dir, 0o777)
                write_code_if_different(output_file, options.encoding, source_tmpl.render(**render_args))

                if not options.quiet:
                    cprintf_stdout(
                        [print_style.FC_GREEN, print_style.FW_BOLD],
                        "[INFO]: generate {0} to {1} success.\n",
                        intput_template,
                        output_file,
                    )
        except Exception as e:
            import traceback

            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[ERROR]: {0}.\n{1}\n",
                str(e),
                traceback.format_exc(),
            )

    # generate per inner templates
    for inner_rule in group.inner_templates:
        render_args = {
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": group.local_vcs_user_name,
            group.outer_name: group.outer_inst,
            group.inner_set_name: selected_inner_items,
            group.inner_name: None,
            "output_file_path": None,
            "output_render_path": None,
            "current_instance": None,
            "PbConvertRule": PbConvertRule,
        }
        for k in group.custom_variables:
            render_args[k] = group.custom_variables[k]

        (
            intput_template,
            output_rule,
            output_render,
            rewrite_overwrite,
        ) = parse_generate_rule(inner_rule)
        if not os.path.exists(intput_template):
            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[INFO]: template file {0} not found.\n",
                intput_template,
            )
            continue
        lookup = TemplateLookup(
            directories=[os.path.dirname(intput_template)],
            module_directory=make_module_cache_dir,
        )

        for selected_inner in selected_inner_items.values():
            render_args[group.inner_name] = selected_inner
            render_args["current_instance"] = selected_inner
            try:
                if output_render:
                    output_file = Template(output_rule,
                                           lookup=lookup).render(**render_args)
                else:
                    output_file = output_rule
                render_args["output_render_path"] = output_file

                if group.output_directory:
                    output_file = os.path.join(group.output_directory,
                                               output_file)
                elif options.output_dir:
                    output_file = os.path.join(options.output_dir, output_file)

                if options.print_output_files:
                    print(output_file)
                else:
                    if os.path.exists(output_file):
                        force_overwrite = rewrite_overwrite
                        if force_overwrite is None:
                            force_overwrite = group.overwrite
                        if force_overwrite is None:
                            force_overwrite = not options.no_overwrite
                        if not force_overwrite:
                            if not options.quiet:
                                cprintf_stdout(
                                    [
                                        print_style.FC_YELLOW,
                                        print_style.FW_BOLD
                                    ],
                                    "[INFO]: file {0} is already exists, we will ignore generating template {1} to it.\n",
                                    output_file,
                                    intput_template,
                                )
                            continue

                    render_args["output_file_path"] = output_file
                    source_tmpl = lookup.get_template(
                        os.path.basename(intput_template))
                    final_output_dir = os.path.dirname(output_file)
                    if final_output_dir and not os.path.exists(
                            final_output_dir):
                        os.makedirs(final_output_dir, 0o777)
                    write_code_if_different(output_file, options.encoding, source_tmpl.render(**render_args))

                    if not options.quiet:
                        cprintf_stdout(
                            [print_style.FC_GREEN, print_style.FW_BOLD],
                            "[INFO]: generate {0} to {1} success.\n",
                            intput_template,
                            output_file,
                        )
            except Exception as e:
                import traceback

                cprintf_stderr(
                    [print_style.FC_RED, print_style.FW_BOLD],
                    "[ERROR]: {0}.\n{1}\n",
                    str(e),
                    traceback.format_exc(),
                )


class PbGlobalGenerator(object):
    def __init__(
        self,
        database,
        project_dir,
        output_directory,
        custom_variables,
        global_templates,
    ):
        self.database = database
        self.project_dir = project_dir
        self.global_templates = global_templates
        self.output_directory = output_directory
        self.custom_variables = custom_variables
        self.local_vcs_user_name = try_read_vcs_username(project_dir)


def generate_global(options, global_generator):
    # type: (argparse.Namespace, PbGlobalGenerator) -> None
    if not global_generator.global_templates:
        return

    from print_color import print_style, cprintf_stdout, cprintf_stderr

    # render templates
    from mako.template import Template
    from mako.lookup import TemplateLookup

    make_module_cache_dir = os.path.join(
        global_generator.project_dir,
        ".mako_modules-{0}.{1}.{2}".format(sys.version_info[0],
                                           sys.version_info[1],
                                           sys.version_info[2]),
    )

    # generate global templates
    for global_rule in global_generator.global_templates:
        render_args = {
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": global_generator.local_vcs_user_name,
            "output_file_path": None,
            "output_render_path": None,
            "database": global_generator.database,
            "PbConvertRule": PbConvertRule,
        }
        for k in global_generator.custom_variables:
            render_args[k] = global_generator.custom_variables[k]

        try:
            (
                intput_template,
                output_rule,
                output_render,
                rewrite_overwrite,
            ) = parse_generate_rule(global_rule)
            if not os.path.exists(intput_template):
                cprintf_stderr(
                    [print_style.FC_RED, print_style.FW_BOLD],
                    "[INFO]: template file {0} not found.\n",
                    intput_template,
                )
                continue

            lookup = TemplateLookup(
                directories=[os.path.dirname(intput_template)],
                module_directory=make_module_cache_dir,
            )
            if output_render:
                output_file = Template(output_rule,
                                       lookup=lookup).render(**render_args)
            else:
                output_file = output_rule
            render_args["output_render_path"] = output_file

            if global_generator.output_directory:
                output_file = os.path.join(global_generator.output_directory,
                                           output_file)
            elif options.output_dir:
                output_file = os.path.join(options.output_dir, output_file)

            if options.print_output_files:
                print(output_file)
            else:
                if os.path.exists(output_file):
                    force_overwrite = rewrite_overwrite
                    if force_overwrite is None:
                        force_overwrite = not options.no_overwrite
                    if not force_overwrite:
                        if not options.quiet:
                            cprintf_stdout(
                                [print_style.FC_YELLOW, print_style.FW_BOLD],
                                "[INFO]: file {0} is already exists, we will ignore generating template {1} to it.\n",
                                output_file,
                                intput_template,
                            )
                        continue

                render_args["output_file_path"] = output_file
                source_tmpl = lookup.get_template(
                    os.path.basename(intput_template))
                final_output_dir = os.path.dirname(output_file)
                if final_output_dir and not os.path.exists(final_output_dir):
                    os.makedirs(final_output_dir, 0o777)
                write_code_if_different(output_file, options.encoding, source_tmpl.render(**render_args))

                if not options.quiet:
                    cprintf_stdout(
                        [print_style.FC_GREEN, print_style.FW_BOLD],
                        "[INFO]: generate {0} to {1} success.\n",
                        intput_template,
                        output_file,
                    )
        except Exception as e:
            import traceback

            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[ERROR]: {0}.\n{1}\n",
                str(e),
                traceback.format_exc(),
            )


def generate_global_templates(pb_db, options, yaml_conf, project_dir,
                              custom_vars):
    if options.global_template:
        generate_global(
            options,
            PbGlobalGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                global_templates=options.global_template,
            ),
        )

    if not yaml_conf:
        return

    if "rules" not in yaml_conf:
        return

    for rule in yaml_conf["rules"]:
        if "global" not in rule:
            continue
        global_rule = rule["global"]
        (
            output_directory,
            custom_variables,
        ) = get_real_output_directory_and_custom_variables(
            options, global_rule, custom_vars)
        generate_global(
            options,
            PbGlobalGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=output_directory,
                custom_variables=custom_variables,
                global_templates=[global_rule],
            ),
        )


def generate_service_group(pb_db, options, yaml_conf, project_dir,
                           custom_vars):
    for service_name in options.service_name:
        selected_service = pb_db.get_service(service_name)
        if selected_service is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                overwrite=None,
                outer_name="service",
                inner_name="rpc",
                inner_set_name="rpcs",
                inner_include_rule=options.rpc_include_rule,
                inner_exclude_rule=options.rpc_exclude_rule,
                outer_templates=options.service_template,
                inner_templates=options.rpc_template,
                outer_inst=selected_service,
                inner_name_map=selected_service.rpcs,
                inner_ignore_types=set(options.rpc_ignore_request),
            ),
        )

    if not yaml_conf:
        return

    if "rules" not in yaml_conf:
        return

    for rule in yaml_conf["rules"]:
        if "service" not in rule:
            continue
        rule_yaml_item = rule["service"]
        if "name" not in rule_yaml_item:
            continue
        selected_service = pb_db.get_service(rule_yaml_item["name"])
        if selected_service is None:
            continue
        (
            output_directory,
            custom_variables,
        ) = get_real_output_directory_and_custom_variables(
            options, rule_yaml_item, custom_vars)
        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=output_directory,
                custom_variables=custom_variables,
                overwrite=get_yaml_configure_child(rule_yaml_item, "overwrite",
                                                   None),
                outer_name="service",
                inner_name="rpc",
                inner_set_name="rpcs",
                inner_include_rule=get_yaml_configure_child(
                    rule_yaml_item, "rpc_include", None),
                inner_exclude_rule=get_yaml_configure_child(
                    rule_yaml_item, "rpc_exclude", None),
                outer_templates=get_yaml_configure_child(
                    rule_yaml_item, "service_template", [], True),
                inner_templates=get_yaml_configure_child(
                    rule_yaml_item, "rpc_template", [], True),
                outer_inst=selected_service,
                inner_name_map=selected_service.rpcs,
                inner_ignore_types=get_yaml_configure_child(rule_yaml_item, "rpc_ignore_request",
                                                   False),
            ),
        )


def generate_message_group(pb_db, options, yaml_conf, project_dir,
                           custom_vars):
    for message_name in options.message_name:
        selected_message = pb_db.get_message(message_name)
        if selected_message is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                overwrite=None,
                outer_name="message",
                inner_name="field",
                inner_set_name="fields",
                inner_include_rule=options.field_include_rule,
                inner_exclude_rule=options.field_exclude_rule,
                outer_templates=options.message_template,
                inner_templates=options.field_template,
                outer_inst=selected_message,
                inner_name_map=selected_message.fields_by_name,
                inner_ignore_types=set(options.field_ignore_type),
            ),
        )

    if not yaml_conf:
        return

    if "rules" not in yaml_conf:
        return

    for rule in yaml_conf["rules"]:
        if "message" not in rule:
            continue
        rule_yaml_item = rule["message"]
        if "name" not in rule_yaml_item:
            continue
        selected_message = pb_db.get_message(rule_yaml_item["name"])
        if selected_message is None:
            continue
        (
            output_directory,
            custom_variables,
        ) = get_real_output_directory_and_custom_variables(
            options, rule_yaml_item, custom_vars)
        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=output_directory,
                custom_variables=custom_variables,
                overwrite=get_yaml_configure_child(rule_yaml_item, "overwrite",
                                                   None),
                outer_name="message",
                inner_name="field",
                inner_set_name="fields",
                inner_include_rule=get_yaml_configure_child(
                    rule_yaml_item, "field_include", None),
                inner_exclude_rule=get_yaml_configure_child(
                    rule_yaml_item, "field_exclude", None),
                outer_templates=get_yaml_configure_child(
                    rule_yaml_item, "message_template", [], True),
                inner_templates=get_yaml_configure_child(
                    rule_yaml_item, "field_template", [], True),
                outer_inst=selected_message,
                inner_name_map=selected_message.fields_by_name,
                inner_ignore_types=get_yaml_configure_child(rule_yaml_item, "field_ignore_type",
                                                   False),
            ),
        )


def generate_enum_group(pb_db, options, yaml_conf, project_dir, custom_vars):
    for enum_name in options.enum_name:
        selected_enum = pb_db.get_enum(enum_name)
        if selected_enum is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                overwrite=None,
                outer_name="enum",
                inner_name="enumvalue",
                inner_set_name="enumvalues",
                inner_include_rule=options.enumvalue_include_rule,
                inner_exclude_rule=options.enumvalue_exclude_rule,
                outer_templates=options.enum_template,
                inner_templates=options.enumvalue_template,
                outer_inst=selected_enum,
                inner_name_map=selected_enum.values_by_name,
                inner_ignore_types=set(),
            ),
        )

    if not yaml_conf:
        return

    if "rules" not in yaml_conf:
        return

    for rule in yaml_conf["rules"]:
        if "enum" not in rule:
            continue
        rule_yaml_item = rule["enum"]
        if "name" not in rule_yaml_item:
            continue
        selected_enum = pb_db.get_enum(rule_yaml_item["name"])
        if selected_enum is None:
            continue
        (
            output_directory,
            custom_variables,
        ) = get_real_output_directory_and_custom_variables(
            options, rule_yaml_item, custom_vars)
        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                output_directory=output_directory,
                custom_variables=custom_variables,
                overwrite=get_yaml_configure_child(rule_yaml_item, "overwrite",
                                                   None),
                outer_name="enum",
                inner_name="enumvalue",
                inner_set_name="enumvalues",
                inner_include_rule=get_yaml_configure_child(
                    rule_yaml_item, "value_include", None),
                inner_exclude_rule=get_yaml_configure_child(
                    rule_yaml_item, "value_exclude", None),
                outer_templates=get_yaml_configure_child(
                    rule_yaml_item, "enum_template", [], True),
                inner_templates=get_yaml_configure_child(
                    rule_yaml_item, "value_template", [], True),
                outer_inst=selected_enum,
                inner_name_map=selected_enum.values_by_name,
                inner_ignore_types=set(),
            ),
        )


def main():
    # lizard forgives
    script_dir = os.path.dirname(os.path.realpath(__file__))
    work_dir = os.getcwd()
    ret = 0
    os.environ["PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION"] = "python"

    usage = '[--service-name <server-name>] --proto-files "*.proto" [--rpc-template TEMPLATE:OUTPUT ...] [--service-template TEMPLATE:OUTPUT ...] [other options...]'
    parser = CmdArgsGetParser(usage)
    CmdArgsAddOption(
        parser,
        "-v",
        "--version",
        action="store_true",
        help="show version and exit",
        dest="version",
        default=False,
    )
    CmdArgsAddOption(
        parser,
        "-o",
        "--output",
        action="store",
        help="set output directory",
        dest="output_dir",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--add-path",
        action="append",
        help=
        "add path to python module(where to find protobuf,six,mako,print_style and etc...)",
        dest="add_path",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--add-package-prefix",
        action="append",
        help=
        "add path to python module install prefix(where to find protobuf,six,mako,print_style and etc...)",
        dest="add_package_prefix",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "-p",
        "--protoc-bin",
        action="store",
        help="set path to google protoc/protoc.exe",
        dest="protoc_bin",
        default="protoc",
    )
    CmdArgsAddOption(
        parser,
        "--protoc-flag",
        action="append",
        help="add protoc flag when running protoc/protoc.exe",
        dest="protoc_flags",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--protoc-include",
        action="append",
        help="add -I<dir> when running protoc/protoc.exe",
        dest="protoc_includes",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "-P",
        "--proto-files",
        action="append",
        help="add *.proto for analysis",
        dest="proto_files",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--pb-file",
        action="store",
        help=
        "set and using pb file instead of generate it with -P/--proto-files",
        dest="pb_file",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--encoding",
        action="store",
        help="set encoding of output files",
        dest="encoding",
        default="utf-8",
    )
    CmdArgsAddOption(
        parser,
        "--output-pb-file",
        action="store",
        help="set output pb file path",
        dest="output_pb_file",
        default=os.path.join(work_dir, "service-protocol.pb"),
    )
    CmdArgsAddOption(
        parser,
        "--keep-pb-file",
        action="store_true",
        help="do not delete generated pb file when exit",
        dest="keep_pb_file",
        default=False,
    )
    CmdArgsAddOption(
        parser,
        "--project-dir",
        action="store",
        help="set project directory",
        dest="project_dir",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--print-output-files",
        action="store_true",
        help="print output file list but generate it",
        dest="print_output_files",
        default=False,
    )
    CmdArgsAddOption(
        parser,
        "--no-overwrite",
        action="store_true",
        help="do not overwrite output file if it's already exists.",
        dest="no_overwrite",
        default=False,
    )
    CmdArgsAddOption(
        parser,
        "--quiet",
        action="store_true",
        help="do not show the detail of generated files.",
        dest="quiet",
        default=False,
    )
    CmdArgsAddOption(
        parser,
        "--set",
        action="append",
        help="set custom variables for rendering templates.",
        dest="set_vars",
        default=[],
    )
    # For service - rpc
    CmdArgsAddOption(
        parser,
        "--rpc-template",
        action="append",
        help="add template rules for each rpc(<template PATH>:<output rule>)",
        dest="rpc_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--rpc-ignore-request",
        action="append",
        help="ignore rpc with request of specify types",
        dest="rpc_ignore_request",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--service-template",
        action="append",
        help="add template rules for service(<template PATH>:<output rule>)",
        dest="service_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "-s",
        "--service-name",
        action="append",
        help="add service name to generate",
        dest="service_name",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--rpc-include",
        action="store",
        help="select only rpc name match the include rule(by regex)",
        dest="rpc_include_rule",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--rpc-exclude",
        action="store",
        help="skip rpc name match the exclude rule(by regex)",
        dest="rpc_exclude_rule",
        default=None,
    )

    # For message - field
    CmdArgsAddOption(
        parser,
        "--field-template",
        action="append",
        help="add template rules for each field(<template PATH>:<output rule>)",
        dest="field_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--message-template",
        action="append",
        help="add template rules for message(<template PATH>:<output rule>)",
        dest="message_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--message-name",
        action="append",
        help="add message name tp generate",
        dest="message_name",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--field-include",
        action="store",
        help="select only field name match the include rule(by regex)",
        dest="field_include_rule",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--field-exclude",
        action="store",
        help="skip field name match the exclude rule(by regex)",
        dest="field_exclude_rule",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--field-ignore-type",
        action="append",
        help="ignore fields with specify types",
        dest="field_ignore_type",
        default=[],
    )

    # For enum - enumvalue
    CmdArgsAddOption(
        parser,
        "--enumvalue-template",
        action="append",
        help=
        "add template rules for each enumvalue(<template PATH>:<output rule>)",
        dest="enumvalue_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--enum-template",
        action="append",
        help="add template rules for enum(<template PATH>:<output rule>)",
        dest="enum_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--enum-name",
        action="append",
        help="add enum name tp generate",
        dest="enum_name",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--enumvalue-include",
        action="store",
        help="select only enumvalue name match the include rule(by regex)",
        dest="enumvalue_include_rule",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--enumvalue-exclude",
        action="store",
        help="skip enumvalue name match the exclude rule(by regex)",
        dest="enumvalue_exclude_rule",
        default=None,
    )

    # For global templates
    CmdArgsAddOption(
        parser,
        "--global-template",
        action="append",
        help="add template rules for global(<template PATH>:<output rule>)",
        dest="global_template",
        default=[],
    )

    # For yaml configures
    CmdArgsAddOption(
        parser,
        "-c",
        "--configure",
        action="store",
        help="use YAML configure file to batch generating",
        dest="yaml_configure",
        default=None,
    )

    (options, left_args) = CmdArgsParse(parser)

    if options.version:
        print("1.1.0")
        return 0
    if options.add_path:
        prepend_paths = [x for x in options.add_path]
        prepend_paths.extend(sys.path)
        sys.path = prepend_paths
    add_package_prefix_paths(options.add_package_prefix)

    # Merge configure from YAML file
    if options.yaml_configure and not check_has_module("yaml"):
        sys.stderr.write(
            "[ERROR]: module {0} is required to using configure file\n".format(
                "PyYAML"))
        options.yaml_configure = None

    yaml_conf = None
    custom_vars = dict()
    for custom_var in options.set_vars:
        key_value_pair = custom_var.split("=")
        if len(key_value_pair) > 1:
            custom_vars[key_value_pair[0].strip()] = key_value_pair[1].strip()
        elif key_value_pair:
            custom_vars[key_value_pair[0].strip()] = ""
    if options.yaml_configure is not None:
        import yaml

        yaml_conf = yaml.load(
            codecs.open(options.yaml_configure,
                        mode="r",
                        encoding=options.encoding).read(),
            Loader=yaml.SafeLoader,
        )
        if "configure" in yaml_conf:
            globla_setting = yaml_conf["configure"]
            if "encoding" in globla_setting:
                options.encoding = globla_setting["encoding"]
            if "output_directory" in globla_setting:
                options.output_dir = globla_setting["output_directory"]
            if "overwrite" in globla_setting:
                options.no_overwrite = not globla_setting["overwrite"]
            if "paths" in globla_setting:
                prepend_paths = [x for x in globla_setting["paths"]]
                if prepend_paths:
                    prepend_paths.extend(sys.path)
                    sys.path = prepend_paths
            if "package_prefix" in globla_setting:
                add_package_prefix_paths(globla_setting["package_prefix"])
            if "protoc" in globla_setting:
                options.protoc_bin = globla_setting["protoc"]
            if "protoc_flags" in globla_setting:
                options.protoc_flags.extend(globla_setting["protoc_flags"])
            if "protoc_includes" in globla_setting:
                options.protoc_includes.extend(
                    globla_setting["protoc_includes"])
            if "protocol_files" in globla_setting:
                options.proto_files.extend(globla_setting["protocol_files"])
            if "protocol_input_pb_file" in globla_setting:
                options.pb_file = globla_setting["protocol_input_pb_file"]
            if "protocol_output_pb_file" in globla_setting:
                options.output_pb_file = globla_setting[
                    "protocol_output_pb_file"]
            if "protocol_project_directory" in globla_setting:
                options.project_dir = globla_setting[
                    "protocol_project_directory"]
            if "custom_variables" in globla_setting:
                for custom_var_name in globla_setting["custom_variables"]:
                    custom_vars[custom_var_name] = globla_setting[
                        "custom_variables"][custom_var_name]

    if not options.proto_files and not options.pb_file:
        sys.stderr.write(
            "-P/--proto-files <*.proto> or --pb-file <something.pb> is required.\n"
        )
        print("[RUNNING]: {0} '{1}'".format(sys.executable,
                                            "' '".join(sys.argv)))
        parser.print_help()
        return 1

    # setup env
    if options.project_dir:
        project_dir = options.project_dir
    else:
        project_dir = None
        test_project_dirs = [x for x in os.path.split(script_dir)]
        for dir_num in range(len(test_project_dirs), 0, -1):
            # compact for python 2.7
            test_project_dir = test_project_dirs[0:dir_num]
            test_project_dir = os.path.join(*test_project_dir)
            if os.path.exists(os.path.join(test_project_dir, ".git")):
                project_dir = test_project_dir
                break
        test_project_dirs = None
        if project_dir is None:
            sys.stderr.write(
                "Can not find project directory please add --project-dir <project directory> with .git in it.\n"
            )
            print("[RUNNING]: {0} '{1}'".format(sys.executable,
                                                "' '".join(sys.argv)))
            parser.print_help()
            return 1

    if options.pb_file:
        if not os.path.exists(options.pb_file):
            sys.stderr.write("Can not find --pb-file {0}.\n".format(
                options.pb_file))
            print("[RUNNING]: {0} '{1}'".format(sys.executable,
                                                "' '".join(sys.argv)))
            parser.print_help()
            return 1
        tmp_pb_file = options.pb_file
    else:
        proto_files = []
        for proto_rule in options.proto_files:
            for proto_file in glob.glob(proto_rule):
                if os.path.dirname(proto_file):
                    proto_files.append(proto_file)
                else:
                    proto_files.append("./{0}".format(proto_file))

        protoc_addition_include_dirs = []
        protoc_addition_include_map = dict()
        for proto_file in proto_files:
            proto_file_dir = os.path.dirname(proto_file)
            if proto_file_dir in protoc_addition_include_map:
                continue
            protoc_addition_include_map[proto_file_dir] = True
            protoc_addition_include_dirs.append("-I{0}".format(proto_file_dir))

        tmp_pb_file = options.output_pb_file
        protoc_run_args = [options.protoc_bin, "-o", tmp_pb_file]
        if not os.path.exists(os.path.dirname(tmp_pb_file)):
            os.makedirs(os.path.dirname(tmp_pb_file), mode=0o777)

        for path in options.protoc_includes:
            if path not in protoc_addition_include_map:
                protoc_run_args.append("-I{0}".format(path))
        protoc_addition_include_map = None

        protoc_run_args.extend(protoc_addition_include_dirs)
        protoc_run_args.extend(proto_files)

        protoc_run_args.extend(options.protoc_flags)
        if not options.quiet and not options.print_output_files:
            print("[DEBUG]: '" + "' '".join(protoc_run_args) + "'")
        pexec = Popen(protoc_run_args,
                      stdin=None,
                      stdout=None,
                      stderr=None,
                      shell=False)
        pexec.wait()

    try:
        pb_db = get_pb_db_with_cache(tmp_pb_file)
        generate_service_group(pb_db, options, yaml_conf, project_dir,
                               custom_vars)
        generate_message_group(pb_db, options, yaml_conf, project_dir,
                               custom_vars)
        generate_enum_group(pb_db, options, yaml_conf, project_dir,
                            custom_vars)
        generate_global_templates(pb_db, options, yaml_conf, project_dir,
                                  custom_vars)

    except Exception as e:
        if (not options.keep_pb_file and os.path.exists(tmp_pb_file)
                and options.pb_file != tmp_pb_file):
            os.remove(tmp_pb_file)

        import traceback
        from print_color import print_style, cprintf_stderr

        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            "[ERROR]: {0}\n{1}\n",
            str(e),
            traceback.format_exc(),
        )
        print("[RUNNING]: {0} '{1}'".format(sys.executable,
                                            "' '".join(sys.argv)))
        ret = 1

    if (not options.keep_pb_file and os.path.exists(tmp_pb_file)
            and options.pb_file != tmp_pb_file):
        os.remove(tmp_pb_file)
    return ret


if __name__ == "__main__":
    exit(main())
