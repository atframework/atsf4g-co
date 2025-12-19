#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Copyright (c) 2022 atframework
"""

# Only support python implement

import glob
import os
import sys
import codecs
import re
import time
import shutil
import sysconfig
import tempfile
import threading
import concurrent.futures
from subprocess import PIPE, Popen, TimeoutExpired

HANDLE_SPLIT_PBFIELD_RULE = re.compile("\\d+|_+|\\s+|\\-")
HANDLE_SPLIT_MODULE_RULE = re.compile("\\.|\\/|\\\\")
HANDLE_NUMBER_RULE = re.compile("^\\d+$")
LOCAL_PB_DB_CACHE = dict()
LOCAL_PROJECT_VCS_CACHE = dict()
LOCAL_WOKER_POOL: concurrent.futures.ThreadPoolExecutor = None
LOCAL_WOKER_FUTURES = dict()


def print_exception_with_traceback(e: Exception, fmt: str = None, *args):
    import traceback
    from print_color import print_style, cprintf_stderr

    if fmt:
        if not fmt.startswith("[ERROR]:"):
            fmt = "[ERROR]: " + fmt
        if not fmt.endswith("\n") and not fmt.endswith("\r"):
            fmt = fmt + "\n"
        cprintf_stderr(
            [print_style.FC_RED, print_style.FW_BOLD],
            fmt,
            *args,
        )

    cprintf_stderr(
        [print_style.FC_RED, print_style.FW_BOLD],
        "[ERROR]: {0}.\n{1}\n",
        str(e),
        traceback.format_exc(),
    )


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
    """RAII: Auto remove tempory directory"""

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
        for add_package_bin_path in [
                os.path.join(path, "bin"),
                os.path.join(path, "local", "bin"),
        ]:
            if os.path.exists(add_package_bin_path):
                if sys.platform.lower() == "win32":
                    os.environ[
                        "PATH"] = add_package_bin_path + ";" + os.environ[
                            "PATH"]
                else:
                    os.environ[
                        "PATH"] = add_package_bin_path + ":" + os.environ[
                            "PATH"]

        python_version_path = "python{0}".format(
            sysconfig.get_python_version())
        for add_package_lib_path in [
                os.path.join(path, "lib", python_version_path,
                             "site-packages"),
                os.path.join(path, "local", "lib", python_version_path,
                             "site-packages"),
                os.path.join(path, "lib64", python_version_path,
                             "site-packages"),
                os.path.join(path, "local", "lib64", python_version_path,
                             "site-packages"),
        ]:
            if os.path.exists(add_package_lib_path):
                append_paths.append(add_package_lib_path)

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

    def is_in_dataset(self, checked_names):
        return False

    def get_name(self):
        return self.descriptor.name

    def get_full_name(self):
        return self.descriptor.full_name

    def get_cpp_class_name(self):
        return self.get_full_name().replace(".", "::")

    def get_cpp_namespace_begin(self, full_name, pretty_ident=""):
        current_ident = ""
        ret = []
        for name in HANDLE_SPLIT_MODULE_RULE.split(full_name):
            ret.append("{0}namespace {1}".format(current_ident, name) + " {")
            current_ident = current_ident + pretty_ident
        return ret

    def get_cpp_namespace_end(self, full_name, pretty_ident=""):
        current_ident = ""
        ret = []
        for name in HANDLE_SPLIT_MODULE_RULE.split(full_name):
            ret.append(current_ident + "}  // namespace " + name)
            current_ident = current_ident + pretty_ident
        ret.reverse()
        return ret

    def get_cpp_namespace_prefix(self, full_name):
        return "::".join(HANDLE_SPLIT_MODULE_RULE.split(full_name))

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

    def is_in_dataset(self, checked_names):
        if not checked_names:
            return False
        return self.get_package() in checked_names

    def get_file_path_without_ext(self):
        full_name = self.get_full_name()
        if full_name.endswith(".proto"):
            return full_name[:-len(".proto")]
        return full_name


class PbField(PbObjectBase):

    def __init__(self, container_message, descriptor, refer_database):
        super(PbField, self).__init__(descriptor, refer_database)
        self.file = container_message.file
        self.container = container_message

    def is_in_dataset(self, checked_names):
        if not checked_names:
            return False
        return self.descriptor.full_name in checked_names


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

    def is_in_dataset(self, checked_names):
        if not checked_names:
            return False
        return self.descriptor.input_type.full_name in checked_names

    def get_name(self):
        return self.descriptor.name

    def get_service(self):
        return self.service

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
        from google.protobuf import descriptor_pool as _descriptor_pool

        self.raw_files = dict()
        self.raw_symbols = dict()
        self.default_factory = _message_factory.MessageFactory(
            _descriptor_pool.Default())
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
        added_file = set()

        def _AddFile(file_proto):
            if file_proto.name in added_file:
                return
            added_file.add(file_proto.name)
            try:
                _already_exists = factory.pool.FindFileByName(file_proto.name)
                return
            except KeyError as _:
                pass

            for dependency in file_proto.dependency:
                if dependency in file_by_name:
                    # Remove from elements to be visited, in order to cut cycles.
                    _AddFile(file_by_name.pop(dependency))
            factory.pool.Add(file_proto)

        while file_by_name:
            _AddFile(file_by_name.popitem()[1])
        import google.protobuf as _protobuf

        protobuf_version = [int(x) for x in _protobuf.__version__.split(".")]
        if protobuf_version[0] > 4 or (protobuf_version[0] == 4
                                       and protobuf_version[1] >= 23):
            from google.protobuf import message_factory as _message_factory

            return _message_factory.GetMessageClassesForFiles(
                [file_proto.name for file_proto in file_protos], factory.pool)
        else:
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
                                              extension.name)] = (extension)
        for message_type in file_proto.message_type:
            self._extended_raw_message(file_proto.package, message_type)
        for service in file_proto.service:
            self._extended_raw_service(file_proto.package, service)

    def load(self, pb_file_path, external_pb_files):
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

        # Load external pb files
        external_pb_file_buffers = []
        if external_pb_files:
            pb_fds_loaded = set([x.name for x in pb_fds_patched])
            for external_pb_file in external_pb_files:
                external_pb_file_buffer = open(external_pb_file, "rb").read()
                external_pb_file_buffers.append(external_pb_file_buffer)
                external_pb_fds = descriptor_pb2.FileDescriptorSet.FromString(
                    external_pb_file_buffer)
                for x in external_pb_fds.file:
                    if x.name in pb_fds_loaded:
                        continue
                    pb_fds_patched.append(x)
                    pb_fds_loaded.add(x.name)

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
        try:
            msg_set = self._register_by_pb_fds(self.default_factory,
                                               pb_fds_patched)
        except Exception as e:
            pb_files = [pb_file_path]
            if external_pb_files:
                pb_files.extend(external_pb_files)
            print_exception_with_traceback(
                e,
                "register proto files for extensions failed:\n- proto files:\n{0}\n- pb files:\n{1}"
                .format("\n".join(["  - " + x.name for x in pb_fds_patched]),
                        "\n".join(["  - " + x for x in pb_files])))
            return

        # Use extensions in default_factory to build extended_factory
        try:
            pb_fds_clazz = msg_set["google.protobuf.FileDescriptorSet"]
        except Exception as e:
            print_exception_with_traceback(
                e,
                "get symbol google.protobuf.FileDescriptorSet failed. system error"
            )
            return

        # from google.protobuf.text_format import MessageToString
        pb_fds = pb_fds_clazz.FromString(pb_file_buffer)
        for file_proto in pb_fds.file:
            self.raw_files[file_proto.name] = file_proto
            self._extended_raw_file(file_proto)
        pb_fds_patched = [x for x in pb_fds.file]
        # Load external pb files to extended_factory
        if external_pb_file_buffers:
            pb_fds_loaded = set([x.name for x in pb_fds_patched])
            for external_pb_file_buffer in external_pb_file_buffers:
                external_pb_fds = pb_fds_clazz.FromString(
                    external_pb_file_buffer)
                for x in external_pb_fds.file:
                    if x.name in pb_fds_loaded:
                        continue
                    pb_fds_patched.append(x)
                    pb_fds_loaded.add(x.name)

        pb_fds_patched.extend(pb_fds_inner)
        try:
            self._register_by_pb_fds(self.extended_factory, pb_fds_patched)
        except Exception as e:
            pb_files = [pb_file_path]
            if external_pb_files:
                pb_files.extend(external_pb_files)
            print_exception_with_traceback(
                e,
                "register final proto files failed:\n- proto files:\n{0}\n- pb files:\n{1}"
                .format("\n".join(["  - " + x.name for x in pb_fds_patched]),
                        "\n".join(["  - " + x for x in pb_files])))
            return

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

    def get_extension(self, full_name):
        if not full_name:
            return None
        if full_name in self._cache_enums:
            return self._cache_enums[full_name]
        # extension should find from default_factory
        target_desc = self.default_factory.pool.FindExtensionByName(full_name)

        if target_desc is None:
            return None
        return target_desc


def remove_well_known_template_suffix(name):
    while True:
        if name.endswith(".template"):
            name = name[0:len(name) - 9]
        elif name.endswith(".tpl"):
            name = name[0:len(name) - 4]
        elif name.endswith(".jinja2"):
            name = name[0:len(name) - 7]
        else:
            break
    return name


def parse_generate_rule(rule):
    # rules from program options
    if type(rule) is str:
        dot_pos = rule.find(":")
        if dot_pos <= 0 or dot_pos > len(rule):
            temp_path = rule
            rule = remove_well_known_template_suffix(
                os.path.basename(temp_path))
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
        output = remove_well_known_template_suffix(os.path.basename(input))
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


def get_pb_db_with_cache(pb_file, external_pb_files):
    global LOCAL_PB_DB_CACHE
    pb_file = os.path.realpath(pb_file)
    if pb_file in LOCAL_PB_DB_CACHE:
        ret = LOCAL_PB_DB_CACHE[pb_file]
        return ret
    ret = PbDatabase()
    ret.load(pb_file, external_pb_files)
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
        clang_format_path,
        clang_format_rule,
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
        inner_include_types,
        inner_exclude_types,
        outer_dllexport_decl,
        inner_dllexport_decl,
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
        self.inner_include_types = inner_include_types
        self.inner_exclude_types = inner_exclude_types
        self.output_directory = output_directory
        self.custom_variables = custom_variables
        self.overwrite = overwrite
        self.outer_dllexport_decl = outer_dllexport_decl
        self.inner_dllexport_decl = inner_dllexport_decl
        self.local_vcs_user_name = try_read_vcs_username(project_dir)
        self.clang_format_path = clang_format_path
        self.clang_format_rule = clang_format_rule
        self.clang_format_rule_re = None

        if self.clang_format_rule and self.clang_format_path:
            try:
                self.clang_format_rule_re = re.compile(self.clang_format_rule,
                                                       re.IGNORECASE)
            except Exception as e:
                print_exception_with_traceback(
                    e, "regex compile rule {0} failed.",
                    self.clang_format_rule)


def __format_codes(project_dir, output_file, data, clang_format_path,
                   clang_format_rule_re):
    if not clang_format_path or not clang_format_rule_re:
        return data
    if clang_format_rule_re.search(output_file) is None:
        return data
    try:
        pexec = Popen(
            [clang_format_path, "--assume-filename={}".format(output_file)],
            stdin=PIPE,
            stdout=PIPE,
            stderr=None,
            cwd=project_dir,
            shell=False,
        )
        (stdout, _stderr) = pexec.communicate(data)
        if pexec.returncode == 0:
            return stdout
        return data

    except Exception as e:
        print_exception_with_traceback(e, "format code file {0} failed.",
                                       output_file)
        return data


def __worker_action_write_code_if_different(project_dir, output_file, encoding,
                                            content, clang_format_path,
                                            clang_format_rule_re):
    data = __format_codes(
        project_dir,
        output_file,
        content.encode(encoding),
        clang_format_path,
        clang_format_rule_re,
    )

    content_changed = False
    if not os.path.exists(output_file):
        content_changed = True
    else:
        old_data = open(output_file, mode="rb").read()
        if old_data != data:
            content_changed = True

    if content_changed:
        open(output_file, mode="wb").write(data)


def write_code_if_different(project_dir, output_file, encoding, content,
                            clang_format_path, clang_format_rule_re):
    global LOCAL_WOKER_POOL
    global LOCAL_WOKER_FUTURES
    if LOCAL_WOKER_POOL is None:
        LOCAL_WOKER_POOL = concurrent.futures.ThreadPoolExecutor()

    future = LOCAL_WOKER_POOL.submit(
        __worker_action_write_code_if_different,
        project_dir,
        output_file,
        encoding,
        content,
        clang_format_path,
        clang_format_rule_re,
    )
    LOCAL_WOKER_FUTURES[future] = {"output_file": output_file}


def generate_group(options, group):
    # type: (argparse.Namespace, PbGroupGenerator) -> None
    if group.outer_inst is None:
        return

    from print_color import print_style, cprintf_stdout, cprintf_stderr

    # render templates
    from jinja2 import (
        Environment,
        FileSystemLoader,
        FileSystemBytecodeCache,
        select_autoescape,
    )

    if options.module_directory:
        if os.path.isabs(options.module_directory):
            make_module_cache_dir = os.path.join(
                options.module_directory,
                "group/{0}".format(
                    os.path.relpath(os.getcwd(), group.project_dir)),
            )
        else:
            make_module_cache_dir = os.path.join(
                group.project_dir,
                options.module_directory,
                "group/{0}".format(
                    os.path.relpath(os.getcwd(), group.project_dir)),
            )
    else:
        make_module_cache_dir = os.path.join(
            group.project_dir,
            ".jinja2_modules/group/{0}".format(
                os.path.relpath(os.getcwd(), group.project_dir)),
        )
    os.makedirs(make_module_cache_dir, mode=0o777, exist_ok=True)

    inner_include_rule = None
    try:
        if group.inner_include_rule is not None:
            inner_include_rule = re.compile(group.inner_include_rule)
    except Exception as e:
        print_exception_with_traceback(
            e,
            "invild {0} include rule {1}, we will ignore it.",
            group.inner_name,
            group.inner_include_rule,
        )
        raise

    inner_exclude_rule = None
    try:
        if group.inner_exclude_rule is not None:
            inner_exclude_rule = re.compile(group.inner_exclude_rule)
    except Exception as e:
        print_exception_with_traceback(
            e,
            "invild {0} exclude rule {1}, we will ignore it.",
            group.inner_name,
            group.inner_include_rule,
        )
        raise

    selected_inner_items = dict()
    for inner_key in group.inner_name_map:
        inner_obj = group.inner_name_map[inner_key]
        if inner_include_rule is not None:
            if inner_include_rule.match(inner_obj.get_name()) is None:
                continue
        if inner_exclude_rule is not None:
            if inner_exclude_rule.match(inner_obj.get_name()) is not None:
                continue
        if group.inner_exclude_types and inner_obj.is_in_dataset(
                group.inner_exclude_types):
            continue
        if group.inner_include_types and not inner_obj.is_in_dataset(
                group.inner_include_types):
            continue
        selected_inner_items[inner_key] = inner_obj

    # generate global templates
    for outer_rule in group.outer_templates:
        render_args = {
            "os": os,
            "sys": sys,
            "time": time,
            "re": re,
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": group.local_vcs_user_name,
            group.outer_name: group.outer_inst,
            group.inner_set_name: selected_inner_items,
            "output_file_path": None,
            "output_render_path": None,
            "current_instance": group.outer_inst,
            group.outer_name + "_dllexport_decl": group.outer_dllexport_decl,
            group.inner_name + "_dllexport_decl": group.inner_dllexport_decl,
            "PbConvertRule": PbConvertRule,
        }
        for k in group.custom_variables:
            render_args[k] = group.custom_variables[k]

        try:
            (
                input_template,
                output_rule,
                output_render,
                rewrite_overwrite,
            ) = parse_generate_rule(outer_rule)
            if not os.path.exists(input_template):
                cprintf_stderr(
                    [print_style.FC_RED, print_style.FW_BOLD],
                    "[INFO]: template file {0} not found.\n",
                    input_template,
                )
                continue

            lookup = FileSystemLoader([os.path.dirname(input_template)])
            jinja2_env = Environment(
                bytecode_cache=FileSystemBytecodeCache(
                    directory=make_module_cache_dir),
                loader=lookup,
                autoescape=select_autoescape(),
                keep_trailing_newline=True,
            )
            if output_render:
                output_file = jinja2_env.from_string(output_rule).render(
                    **render_args)
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
                                input_template,
                            )
                        continue

                render_args["output_file_path"] = output_file
                source_tmpl = jinja2_env.get_template(
                    os.path.basename(input_template))
                final_output_dir = os.path.dirname(output_file)
                if final_output_dir and not os.path.exists(final_output_dir):
                    os.makedirs(final_output_dir, 0o777)
                write_code_if_different(
                    group.project_dir,
                    output_file,
                    options.encoding,
                    source_tmpl.render(**render_args),
                    group.clang_format_path,
                    group.clang_format_rule_re,
                )

                if not options.quiet:
                    cprintf_stdout(
                        [print_style.FC_GREEN, print_style.FW_BOLD],
                        "[INFO]: generate {0} to {1} success.\n",
                        input_template,
                        output_file,
                    )
        except Exception as e:
            print_exception_with_traceback(e)
            raise

    # generate per inner templates
    for inner_rule in group.inner_templates:
        render_args = {
            "os": os,
            "sys": sys,
            "time": time,
            "re": re,
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": group.local_vcs_user_name,
            group.outer_name: group.outer_inst,
            group.inner_set_name: selected_inner_items,
            group.inner_name: None,
            "output_file_path": None,
            "output_render_path": None,
            "current_instance": None,
            group.outer_name + "_dllexport_decl": group.outer_dllexport_decl,
            group.inner_name + "_dllexport_decl": group.inner_dllexport_decl,
            "PbConvertRule": PbConvertRule,
        }
        [].extend
        for k in group.custom_variables:
            render_args[k] = group.custom_variables[k]

        (
            input_template,
            output_rule,
            output_render,
            rewrite_overwrite,
        ) = parse_generate_rule(inner_rule)
        if not os.path.exists(input_template):
            cprintf_stderr(
                [print_style.FC_RED, print_style.FW_BOLD],
                "[INFO]: template file {0} not found.\n",
                input_template,
            )
            continue
        lookup = FileSystemLoader([os.path.dirname(input_template)])
        jinja2_env = Environment(
            bytecode_cache=FileSystemBytecodeCache(
                directory=make_module_cache_dir),
            loader=lookup,
            autoescape=select_autoescape(),
            keep_trailing_newline=True,
        )

        for selected_inner in selected_inner_items.values():
            render_args[group.inner_name] = selected_inner
            render_args["current_instance"] = selected_inner
            try:
                if output_render:
                    output_file = jinja2_env.from_string(output_rule).render(
                        **render_args)
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
                                    input_template,
                                )
                            continue

                    render_args["output_file_path"] = output_file
                    source_tmpl = jinja2_env.get_template(
                        os.path.basename(input_template))
                    final_output_dir = os.path.dirname(output_file)
                    if final_output_dir and not os.path.exists(
                            final_output_dir):
                        os.makedirs(final_output_dir, 0o777)
                    write_code_if_different(
                        group.project_dir,
                        output_file,
                        options.encoding,
                        source_tmpl.render(**render_args),
                        group.clang_format_path,
                        group.clang_format_rule_re,
                    )

                    if not options.quiet:
                        cprintf_stdout(
                            [print_style.FC_GREEN, print_style.FW_BOLD],
                            "[INFO]: generate {0} to {1} success.\n",
                            input_template,
                            output_file,
                        )
            except Exception as e:
                print_exception_with_traceback(e)
                raise


class PbGlobalGenerator(object):

    def __init__(
        self,
        database,
        project_dir,
        clang_format_path,
        clang_format_rule,
        output_directory,
        custom_variables,
        global_templates,
        global_dllexport_decl,
    ):
        self.database = database
        self.project_dir = project_dir
        self.global_templates = global_templates
        self.output_directory = output_directory
        self.custom_variables = custom_variables
        self.local_vcs_user_name = try_read_vcs_username(project_dir)
        self.global_dllexport_decl = global_dllexport_decl
        self.clang_format_path = clang_format_path
        self.clang_format_rule = clang_format_rule
        self.clang_format_rule_re = None

        if self.clang_format_rule and self.clang_format_path:
            try:
                self.clang_format_rule_re = re.compile(self.clang_format_rule,
                                                       re.IGNORECASE)
            except Exception as e:
                print_exception_with_traceback(
                    e, "regex compile rule {0} failed.",
                    self.clang_format_rule)


def generate_global(options, global_generator):
    # type: (argparse.Namespace, PbGlobalGenerator) -> None
    if not global_generator.global_templates:
        return

    from print_color import print_style, cprintf_stdout, cprintf_stderr

    # render templates
    from jinja2 import (
        Environment,
        FileSystemLoader,
        FileSystemBytecodeCache,
        select_autoescape,
    )

    if options.module_directory:
        if os.path.isabs(options.module_directory):
            make_module_cache_dir = os.path.join(
                options.module_directory,
                "group/{0}".format(
                    os.path.relpath(os.getcwd(),
                                    global_generator.project_dir)),
            )
        else:
            make_module_cache_dir = os.path.join(
                global_generator.project_dir,
                options.module_directory,
                "group/{0}".format(
                    os.path.relpath(os.getcwd(),
                                    global_generator.project_dir)),
            )
    else:
        make_module_cache_dir = os.path.join(
            global_generator.project_dir,
            ".jinja2_modules/group/{0}".format(
                os.path.relpath(os.getcwd(), global_generator.project_dir)),
        )
    os.makedirs(make_module_cache_dir, mode=0o777, exist_ok=True)

    # generate global templates
    for global_rule in global_generator.global_templates:
        render_args = {
            "os": os,
            "sys": sys,
            "time": time,
            "re": re,
            "generator": os.path.basename(__file__),
            "local_vcs_user_name": global_generator.local_vcs_user_name,
            "output_file_path": None,
            "output_render_path": None,
            "database": global_generator.database,
            "global_dllexport_decl": global_generator.global_dllexport_decl,
            "PbConvertRule": PbConvertRule,
        }
        for k in global_generator.custom_variables:
            render_args[k] = global_generator.custom_variables[k]

        try:
            (
                input_template,
                output_rule,
                output_render,
                rewrite_overwrite,
            ) = parse_generate_rule(global_rule)
            if not os.path.exists(input_template):
                cprintf_stderr(
                    [print_style.FC_RED, print_style.FW_BOLD],
                    "[INFO]: template file {0} not found.\n",
                    input_template,
                )
                continue

            lookup = FileSystemLoader([os.path.dirname(input_template)])
            jinja2_env = Environment(
                bytecode_cache=FileSystemBytecodeCache(
                    directory=make_module_cache_dir),
                loader=lookup,
                autoescape=select_autoescape(),
                keep_trailing_newline=True,
            )
            if output_render:
                output_file = jinja2_env.from_string(output_rule).render(
                    **render_args)
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
                                input_template,
                            )
                        continue

                render_args["output_file_path"] = output_file
                source_tmpl = jinja2_env.get_template(
                    os.path.basename(input_template))
                final_output_dir = os.path.dirname(output_file)
                if final_output_dir and not os.path.exists(final_output_dir):
                    os.makedirs(final_output_dir, 0o777)
                write_code_if_different(
                    global_generator.project_dir,
                    output_file,
                    options.encoding,
                    output_file,
                    source_tmpl.render(**render_args),
                    global_generator.clang_format_path,
                    global_generator.clang_format_rule_re,
                )

                if not options.quiet:
                    cprintf_stdout(
                        [print_style.FC_GREEN, print_style.FW_BOLD],
                        "[INFO]: generate {0} to {1} success.\n",
                        input_template,
                        output_file,
                    )
        except Exception as e:
            print_exception_with_traceback(e)
            raise


def generate_global_templates(pb_db, options, yaml_conf, project_dir,
                              custom_vars):
    outer_dllexport_decl = options.global_dllexport_decl
    if not outer_dllexport_decl:
        outer_dllexport_decl = options.dllexport_decl
    if options.global_template:
        generate_global(
            options,
            PbGlobalGenerator(
                database=pb_db,
                project_dir=project_dir,
                clang_format_path=options.clang_format_path,
                clang_format_rule=options.clang_format_rule,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                global_templates=options.global_template,
                global_dllexport_decl=outer_dllexport_decl,
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
        if "global_dllexport_decl" in global_rule:
            global_dllexport_decl = global_rule["global_dllexport_decl"]
        else:
            global_dllexport_decl = outer_dllexport_decl
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
                clang_format_path=get_yaml_configure_child(
                    global_rule, "clang_format_path",
                    options.clang_format_path),
                clang_format_rule=get_yaml_configure_child(
                    global_rule, "clang_format_rule",
                    options.clang_format_rule),
                output_directory=output_directory,
                custom_variables=custom_variables,
                global_templates=[global_rule],
                global_dllexport_decl=global_dllexport_decl,
            ),
        )


def generate_service_group(pb_db, options, yaml_conf, project_dir,
                           custom_vars):
    outer_dllexport_decl = options.service_dllexport_decl
    if not outer_dllexport_decl:
        outer_dllexport_decl = options.dllexport_decl
    inner_dllexport_decl = options.rpc_dllexport_decl
    if not inner_dllexport_decl:
        inner_dllexport_decl = options.dllexport_decl

    for service_name in options.service_name:
        selected_service = pb_db.get_service(service_name)
        if selected_service is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                clang_format_path=options.clang_format_path,
                clang_format_rule=options.clang_format_rule,
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
                inner_include_types=set(options.rpc_include_request),
                inner_exclude_types=set(options.rpc_exclude_request),
                outer_dllexport_decl=outer_dllexport_decl,
                inner_dllexport_decl=inner_dllexport_decl,
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
        if "service_dllexport_decl" in rule_yaml_item:
            service_dllexport_decl = rule_yaml_item["service_dllexport_decl"]
        else:
            service_dllexport_decl = outer_dllexport_decl
        if "rpc_dllexport_decl" in rule_yaml_item:
            rpc_dllexport_decl = rule_yaml_item["rpc_dllexport_decl"]
        else:
            rpc_dllexport_decl = inner_dllexport_decl
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
                clang_format_path=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_path",
                    options.clang_format_path),
                clang_format_rule=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_rule",
                    options.clang_format_rule),
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
                inner_include_types=get_yaml_configure_child(
                    rule_yaml_item, "rpc_include_request", False),
                inner_exclude_types=get_yaml_configure_child(
                    rule_yaml_item, "rpc_exclude_request", False),
                outer_dllexport_decl=service_dllexport_decl,
                inner_dllexport_decl=rpc_dllexport_decl,
            ),
        )


def generate_message_group(pb_db, options, yaml_conf, project_dir,
                           custom_vars):
    outer_dllexport_decl = options.message_dllexport_decl
    if not outer_dllexport_decl:
        outer_dllexport_decl = options.dllexport_decl
    inner_dllexport_decl = options.field_dllexport_decl
    if not inner_dllexport_decl:
        inner_dllexport_decl = options.dllexport_decl
    for message_name in options.message_name:
        selected_message = pb_db.get_message(message_name)
        if selected_message is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                clang_format_path=options.clang_format_path,
                clang_format_rule=options.clang_format_rule,
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
                inner_include_types=set(options.field_include_type),
                inner_exclude_types=set(options.field_exclude_type),
                outer_dllexport_decl=outer_dllexport_decl,
                inner_dllexport_decl=inner_dllexport_decl,
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
        if "message_dllexport_decl" in rule_yaml_item:
            message_dllexport_decl = rule_yaml_item["message_dllexport_decl"]
        else:
            message_dllexport_decl = outer_dllexport_decl
        if "field_dllexport_decl" in rule_yaml_item:
            field_dllexport_decl = rule_yaml_item["field_dllexport_decl"]
        else:
            field_dllexport_decl = inner_dllexport_decl
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
                clang_format_path=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_path",
                    options.clang_format_path),
                clang_format_rule=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_rule",
                    options.clang_format_rule),
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
                inner_include_types=get_yaml_configure_child(
                    rule_yaml_item, "field_include_type", False),
                inner_exclude_types=get_yaml_configure_child(
                    rule_yaml_item, "field_exclude_type", False),
                outer_dllexport_decl=message_dllexport_decl,
                inner_dllexport_decl=field_dllexport_decl,
            ),
        )


def generate_enum_group(pb_db, options, yaml_conf, project_dir, custom_vars):
    outer_dllexport_decl = options.enum_dllexport_decl
    if not outer_dllexport_decl:
        outer_dllexport_decl = options.dllexport_decl
    inner_dllexport_decl = options.enumvalue_dllexport_decl
    if not inner_dllexport_decl:
        inner_dllexport_decl = options.dllexport_decl
    for enum_name in options.enum_name:
        selected_enum = pb_db.get_enum(enum_name)
        if selected_enum is None:
            continue

        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                clang_format_path=options.clang_format_path,
                clang_format_rule=options.clang_format_rule,
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
                inner_include_types=set(),
                inner_exclude_types=set(),
                outer_dllexport_decl=outer_dllexport_decl,
                inner_dllexport_decl=inner_dllexport_decl,
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
        if "enum_dllexport_decl" in rule_yaml_item:
            enum_dllexport_decl = rule_yaml_item["enum_dllexport_decl"]
        else:
            enum_dllexport_decl = outer_dllexport_decl
        if "enumvalue_dllexport_decl" in rule_yaml_item:
            enumvalue_dllexport_decl = rule_yaml_item[
                "enumvalue_dllexport_decl"]
        else:
            enumvalue_dllexport_decl = inner_dllexport_decl
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
                clang_format_path=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_path",
                    options.clang_format_path),
                clang_format_rule=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_rule",
                    options.clang_format_rule),
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
                inner_include_types=set(),
                inner_exclude_types=set(),
                outer_dllexport_decl=enum_dllexport_decl,
                inner_dllexport_decl=enumvalue_dllexport_decl,
            ),
        )


def generate_file_group(pb_db, options, yaml_conf, project_dir, custom_vars):
    outer_dllexport_decl = options.file_dllexport_decl
    if not outer_dllexport_decl:
        outer_dllexport_decl = options.dllexport_decl
    inner_dllexport_decl = options.file_dllexport_decl
    if not inner_dllexport_decl:
        inner_dllexport_decl = options.dllexport_decl
    values_by_name = None
    if options.file_template:
        values_by_name = dict()
        for file_path in pb_db.raw_files:
            file_inst = pb_db.get_file(file_path)
            if file_inst:
                values_by_name[file_path] = file_inst
        generate_group(
            options,
            PbGroupGenerator(
                database=pb_db,
                project_dir=project_dir,
                clang_format_path=options.clang_format_path,
                clang_format_rule=options.clang_format_rule,
                output_directory=options.output_dir,
                custom_variables=custom_vars,
                overwrite=None,
                outer_name="file_descriptor_set",
                inner_name="file",
                inner_set_name="files",
                inner_include_rule=options.file_include_rule,
                inner_exclude_rule=options.file_exclude_rule,
                outer_templates=None,
                inner_templates=options.file_template,
                outer_inst=pb_db,
                inner_name_map=values_by_name,
                inner_include_types=set(options.file_include_package),
                inner_exclude_types=set(options.file_exclude_package),
                outer_dllexport_decl=outer_dllexport_decl,
                inner_dllexport_decl=inner_dllexport_decl,
            ),
        )

    if not yaml_conf:
        return

    if "rules" not in yaml_conf:
        return

    for rule in yaml_conf["rules"]:
        if "file" not in rule:
            continue
        if values_by_name is None:
            values_by_name = dict()
            for file_path in pb_db.raw_files:
                file_inst = pb_db.get_file(file_path)
                if file_inst:
                    values_by_name[file_path] = file_inst
        rule_yaml_item = rule["file"]
        if "file_dllexport_decl" in rule_yaml_item:
            file_dllexport_decl = rule_yaml_item["file_dllexport_decl"]
        else:
            file_dllexport_decl = inner_dllexport_decl
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
                clang_format_path=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_path",
                    options.clang_format_path),
                clang_format_rule=get_yaml_configure_child(
                    rule_yaml_item, "clang_format_rule",
                    options.clang_format_rule),
                output_directory=output_directory,
                custom_variables=custom_variables,
                overwrite=get_yaml_configure_child(rule_yaml_item, "overwrite",
                                                   None),
                outer_name="file_descriptor_set",
                inner_name="file",
                inner_set_name="files",
                inner_include_rule=get_yaml_configure_child(
                    rule_yaml_item, "file_include", None),
                inner_exclude_rule=get_yaml_configure_child(
                    rule_yaml_item, "file_exclude", None),
                outer_templates=None,
                inner_templates=get_yaml_configure_child(
                    rule_yaml_item, "file_template", [], True),
                outer_inst=pb_db,
                inner_name_map=values_by_name,
                inner_include_types=get_yaml_configure_child(
                    rule_yaml_item, "file_include_package", False),
                inner_exclude_types=get_yaml_configure_child(
                    rule_yaml_item, "file_exclude_package", False),
                outer_dllexport_decl=file_dllexport_decl,
                inner_dllexport_decl=file_dllexport_decl,
            ),
        )


def main():
    # lizard forgives
    global LOCAL_WOKER_POOL
    global LOCAL_WOKER_FUTURES

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
        "--module-directory",
        action="store",
        help="set module directory",
        dest="module_directory",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--add-path",
        action="append",
        help=
        "add path to python module(where to find protobuf,six,jinja2,print_style and etc...)",
        dest="add_path",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--add-package-prefix",
        action="append",
        help=
        "add path to python module install prefix(where to find protobuf,six,jinja2,print_style and etc...)",
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
        "--external-pb-files",
        action="append",
        help="append external pb files to load",
        dest="external_pb_files",
        default=[],
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
        "--console-encoding",
        action="append",
        help="try encoding of console output",
        dest="console_encoding",
        default=[],
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
    CmdArgsAddOption(
        parser,
        "--dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing.",
        dest="dllexport_decl",
        default="",
    )
    CmdArgsAddOption(
        parser,
        "--clang-format-path",
        action="store",
        help="set path of clang-format to format output codes",
        dest="clang_format_path",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--clang-format-rule",
        action="store",
        help="set regex rule for file path to use clang-format to format",
        dest="clang_format_rule",
        default="\\.(c|cc|cpp|cxx|h|hpp|hxx|i|ii|ixx|tcc|cppm|c\\+\\+|proto)$",
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
        "--rpc-include-request",
        action="append",
        help="include rpc with request of specify types",
        dest="rpc_include_request",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--rpc-exclude-request",
        action="append",
        help="exclude rpc with request of specify types",
        dest="rpc_exclude_request",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--rpc-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for rpc.",
        dest="rpc_dllexport_decl",
        default=None,
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
        "--service-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for service.",
        dest="service_dllexport_decl",
        default=None,
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
        "--field-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for field.",
        dest="field_dllexport_decl",
        default=None,
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
        "--message-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for message.",
        dest="message_dllexport_decl",
        default=None,
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
        "--field-include-type",
        action="append",
        help="include fields with specify types",
        dest="field_include_type",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--field-exclude-type",
        action="append",
        help="exclude fields with specify types",
        dest="field_exclude_type",
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
        "--enumvalue-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for enumvalue.",
        dest="enumvalue_dllexport_decl",
        default=None,
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
        "--enum-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for enum.",
        dest="enum_dllexport_decl",
        default=None,
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

    # For file
    CmdArgsAddOption(
        parser,
        "--file-include-package",
        action="append",
        help="include file in of packages",
        dest="file_include_package",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--file-exclude-package",
        action="append",
        help="exclude file in of packages",
        dest="file_exclude_package",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--file-template",
        action="append",
        help="add template rules for each file(<template PATH>:<output rule>)",
        dest="file_template",
        default=[],
    )
    CmdArgsAddOption(
        parser,
        "--file-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for file.",
        dest="file_dllexport_decl",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--file-include",
        action="store",
        help="select only file name match the include rule(by regex)",
        dest="file_include_rule",
        default=None,
    )
    CmdArgsAddOption(
        parser,
        "--file-exclude",
        action="store",
        help="skip file name match the exclude rule(by regex)",
        dest="file_exclude_rule",
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
    CmdArgsAddOption(
        parser,
        "--global-dllexport-decl",
        action="store",
        help="set definition for DLL exporting/importing for global.",
        dest="global_dllexport_decl",
        default=None,
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
        print("1.2.0")
        return 0
    if options.add_path:
        prepend_paths = [x for x in options.add_path]
        prepend_paths.extend(sys.path)
        sys.path = prepend_paths
    add_package_prefix_paths(options.add_package_prefix)

    if options.console_encoding:
        console_encoding = options.console_encoding
    else:
        console_encoding = ["utf-8", "utf-8-sig", "GB18030"]

    def print_buffer_to_fd(fd, buffer):
        if not buffer:
            return

        for try_encoding in console_encoding:
            try:
                fd.write(buffer.decode(try_encoding))
                return
            except Exception as _:
                pass

        # console_encoding = sys.getfilesystemencoding()
        fd.buffer.write(buffer)

    def print_stdout_func(pexec):
        for output_line in pexec.stdout.readlines():
            print_buffer_to_fd(sys.stdout, output_line)

    def print_stderr_func(pexec):
        for output_line in pexec.stderr.readlines():
            print_buffer_to_fd(sys.stderr, output_line)

    def wait_print_pexec(pexec, timeout=300):
        if pexec.stdout:
            worker_thd_print_stdout = threading.Thread(
                target=print_stdout_func, args=[pexec])
            worker_thd_print_stdout.start()
        else:
            worker_thd_print_stdout = None
        if pexec.stderr:
            worker_thd_print_stderr = threading.Thread(
                target=print_stderr_func, args=[pexec])
            worker_thd_print_stderr.start()
        else:
            worker_thd_print_stderr = None

        try:
            pexec.wait(timeout=timeout)
        except TimeoutExpired:
            pexec.kill()
            pexec.wait()

        if worker_thd_print_stdout:
            worker_thd_print_stdout.join()
        if worker_thd_print_stderr:
            worker_thd_print_stderr.join()

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
            if "protocol_external_pb_files" in globla_setting:
                options.external_pb_files.extend(
                    globla_setting["protocol_external_pb_files"])
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

    if not options.quiet and not options.print_output_files:
        print("[RUNNING]: {0} '{1}'".format(sys.executable,
                                            "' '".join(sys.argv)))
    if options.pb_file:
        if not os.path.exists(options.pb_file):
            sys.stderr.write("Can not find --pb-file {0}.\n".format(
                options.pb_file))
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
        wait_print_pexec(pexec)

    try:
        pb_db = get_pb_db_with_cache(tmp_pb_file, options.external_pb_files)
        generate_service_group(pb_db, options, yaml_conf, project_dir,
                               custom_vars)
        generate_message_group(pb_db, options, yaml_conf, project_dir,
                               custom_vars)
        generate_enum_group(pb_db, options, yaml_conf, project_dir,
                            custom_vars)
        generate_file_group(pb_db, options, yaml_conf, project_dir,
                            custom_vars)
        generate_global_templates(pb_db, options, yaml_conf, project_dir,
                                  custom_vars)

    except Exception as e:
        if (not options.keep_pb_file and os.path.exists(tmp_pb_file)
                and options.pb_file != tmp_pb_file):
            os.remove(tmp_pb_file)

        print_exception_with_traceback(e)
        ret = 1

    if (not options.keep_pb_file and os.path.exists(tmp_pb_file)
            and options.pb_file != tmp_pb_file):
        os.remove(tmp_pb_file)

    if LOCAL_WOKER_POOL is not None:
        LOCAL_WOKER_POOL.shutdown(wait=True)
    for future in concurrent.futures.as_completed(LOCAL_WOKER_FUTURES):
        future_data = LOCAL_WOKER_FUTURES[future]
        try:
            future_result = future.result()
            if future_result is not None and future_result != 0:
                ret = 1
        except Exception as e:
            print_exception_with_traceback(e, "generate file {0} failed.",
                                           future_data["output_file"])
            ret = 1
    return ret


if __name__ == "__main__":
    exit(main())
