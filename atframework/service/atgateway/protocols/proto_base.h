#ifndef ATFRAME_SERVICE_ATGATEWAY_PROTOCOL_BASE_H
#define ATFRAME_SERVICE_ATGATEWAY_PROTOCOL_BASE_H

#pragma once

#include <cstddef>
#include <std/functional.h>
#include <stdint.h>

namespace atframe {
    namespace gateway {
        struct error_code_t {
            enum type {
                EN_ECT_SUCCESS = 0,
                EN_ECT_FIRST_IDEL = -1001,
                EN_ECT_HANDSHAKE = -1002,
                EN_ECT_BUSY = -1003,
                EN_ECT_SESSION_EXPIRED = -1004,
                EN_ECT_REFUSE_RECONNECT = -1005,
                EN_ECT_MISS_CALLBACKS = -1006,
                EN_ECT_INVALID_ROUTER = -1007,
                EN_ECT_INVALID_ADDRESS = -1008,
                EN_ECT_NETWORK = -1009,
                EN_ECT_BAD_PROTOCOL = -1010,
                EN_ECT_CLOSING = -1011,
                EN_ECT_LOST_MANAGER = -1012,
                EN_ECT_MSG_TOO_LARGE = -1013,
                EN_ECT_HANDLE_NOT_FOUND = -1014,
                EN_ECT_ALREADY_HAS_FD = -1015,
                EN_ECT_SESSION_NOT_FOUND = -1016,
                EN_ECT_SESSION_ALREADY_EXIST = -1017,
                EN_ECT_NOT_WRITING = -1018,
                EN_ECT_CRYPT_NOT_SUPPORTED = -1019,
                EN_ECT_PARAM = -1020,
                EN_ECT_BAD_DATA = -1021,
                EN_ECT_INVALID_SIZE = -1022,
                EN_ECT_NO_DATA = -1023,
                EN_ECT_MALLOC = -1024,
                EN_ECT_CRYPT_ALREADY_INITED = -1101,
                EN_ECT_CRYPT_VERIFY = -1102,
                EN_ECT_CRYPT_OPERATION = -1103,
                EN_ECT_CRYPT_READ_DHPARAM_FILE = -1211,
                EN_ECT_CRYPT_INIT_DHPARAM = -1212,
                EN_ECT_CRYPT_READ_RSA_PUBKEY = -1221,
                EN_ECT_CRYPT_READ_RSA_PRIKEY = -1222,
            };
        };

        struct close_reason_t {
            enum type {
                EN_CRT_UNKNOWN = 0x0000,
                EN_CRT_EAGAIN = 0x0001, // resource temporary unavailable
                EN_CRT_TRAFIC_EXTENDED = 0x0002,
                EN_CRT_INVALID_DATA = 0x0003,
                EN_CRT_RESET = 0x0004,
                EN_CRT_RECONNECT_INNER_BOUND = 0x0100,
                EN_CRT_RECONNECT_BOUND = 0x10000,
                EN_CRT_FIRST_IDLE = 0x10001,
                EN_CRT_SERVER_CLOSED = 0x10002,
                EN_CRT_SERVER_BUSY = 0x10003,
                EN_CRT_KICKOFF = 0x10004,
                EN_CRT_HANDSHAKE = 0x10005,
                EN_CRT_LOGOUT = 0x10006,
                EN_CRT_ADMINISTRATOR = 0x10007, // kickoff by administrator
                EN_CRT_MAINTENANCE = 0x10008,   // closed to maintenance
                EN_CRT_EOF = 0x10009,           // EOF means everything is finished and no more need this connection
                EN_CRT_NO_RECONNECT_INNER_BOUND = 0x10100,
            };
        };

        class proto_base {
        public:
            /**
             * SPECIFY: callback when write any data. the last boolean return false means async write, you must call write_done(status)
             * when the writing finished. so any engine use it can determine how to send data to peer.
             * PARAMETER:
             *   0: proto object
             *   1: buffer to write, the first write_header_offset_ bytes are headspace and are not the real data need to send to peer
             *   2: buffer length, should be greater than write_header_offset_
             *   3: output if it's already done
             * RETURN: 0 or error code
             * REQUIRED
             * PROTOCOL: any custom protocol must call this when there is any data to send to peer.
             */
            typedef std::function<int(proto_base *, void *, size_t, bool *)> on_write_start_fn_t;

            /**
             * SPECIFY: callback when receive any custom message
             * PARAMETER:
             *   0: proto object
             *   1: custom message buffer
             *   2: custom message buffer length
             * RETURN: 0 or error code
             * REQUIRED
             * PROTOCOL: any custom protocol should call this when receive a custom message. check_flag(flag_t::EN_PFT_IN_CALLBACK) must
             *           return true here
             */
            typedef std::function<int(proto_base *, const void *, size_t)> on_message_fn_t;

            /**
             * SPECIFY: callback when start init a new session
             * PARAMETER:
             *   0: proto object
             *   1: output the new session's id
             * RETURN: 0 or error code
             * REQUIRED
             * PROTOCOL: any custom protocol must call this when the new connection is a new session. check_flag(flag_t::EN_PFT_IN_CALLBACK)
             *           must return true here
             */
            typedef std::function<int(proto_base *, uint64_t &)> on_init_new_session_fn_t;

            /**
             * SPECIFY: callback when start init a reconnect session
             * PARAMETER:
             *   0: proto object
             *   1: old session id
             * RETURN: 0 or error code
             * OPTIONAL
             * PROTOCOL: if not provided, we think reconnect is not supported. check_flag(flag_t::EN_PFT_IN_CALLBACK) must
             *           return true here
             */
            typedef std::function<int(proto_base *, uint64_t)> on_init_reconnect_fn_t;

            /**
             * SPECIFY: callback when all recource closed and do not use this object's resource any more
             *   any resource can only be freed after proto closed, you can use both **on_close_fn_t** or check_flag(flag_t::EN_PFT_CLOSED)
             * PARAMETER:
             *   0: proto object
             *   1: close reason
             * RETURN: 0 or error code
             * REQUIRED
             * PROTOCOL: any custom protocol must set_flag(flag_t::EN_PFT_CLOSED, true) and then call this when all resource closed.
             */
            typedef std::function<int(proto_base *, int)> on_close_fn_t;

            /**
             * SPECIFY: callback when handshake done
             *   any resource can only be freed after proto closed, you can use both **on_close_fn_t** or check_flag(flag_t::EN_PFT_CLOSED)
             * PARAMETER:
             *   0: proto object
             *   1: status
             * RETURN: 0 or error code
             * OPTIONAL
             * PROTOCOL: just notify when handshake finished. check_flag(flag_t::EN_PFT_IN_CALLBACK) must
             *           return true here
             */
            typedef std::function<int(proto_base *, int)> on_handshake_done_fn_t;

            /**
             * SPECIFY: callback when any error happen
             * PARAMETER:
             *   0: proto object
             *   1: file name
             *   2: line
             *   3: error code
             *   4: error message
             * RETURN: 0 or error code
             * OPTIONAL
             * PROTOCOL: any custom protocol should call this when any error happen.
             */
            typedef std::function<int(proto_base *, const char *, int, int, const char *)> on_error_fn_t;

            struct tls_buffer_t {
                enum type {
                    EN_TBT_MERGE = 0,
                    EN_TBT_CRYPT,
                    EN_TBT_ZIP,
                    EN_TBT_CUSTOM,
                    EN_TBT_MAX,
                };
            };

            struct flag_t {
                enum type {
                    EN_PFT_WRITING = 0x0001,
                    EN_PFT_CLOSING = 0x0002,
                    EN_PFT_CLOSED = 0x0004,
                    EN_PFT_IN_CALLBACK = 0x0008,
                    EN_PFT_HANDSHAKE_DONE = 0x0100,
                    EN_PFT_HANDSHAKE_UPDATE = 0x0200,
                };
            };

            struct flag_guard_t {
                int *flags_;
                int v_;
                flag_guard_t(int &f, int v);
                ~flag_guard_t();

                flag_guard_t(const flag_guard_t &other);
                flag_guard_t &operator=(const flag_guard_t &other);
            };

            struct proto_callbacks_t {
                on_write_start_fn_t write_fn;
                on_message_fn_t message_fn;
                on_init_new_session_fn_t new_session_fn;
                on_init_reconnect_fn_t reconnect_fn;
                on_close_fn_t close_fn;
                on_handshake_done_fn_t on_handshake_done_fn;
                on_handshake_done_fn_t on_handshake_update_fn;
                on_error_fn_t on_error_fn;
            };

        protected:
            proto_base();

        public:
            virtual ~proto_base() = 0;

            bool check_flag(flag_t::type t) const;

            void set_flag(flag_t::type t, bool v);

            /**
             * @biref call this when need to allocate buffer block to store received data. custom protocol must implement this
             * @param suggested_size suggested size
             * @param out_buf output the allocated buffer address
             * @param out_len output the allocated buffer length
             */
            virtual void alloc_recv_buffer(size_t suggested_size, char *&out_buf, size_t &out_len) = 0;

            /**
             * @biref call this when received any data from engine. custom protocol must implement this
             * @param ssz read size or error code
             * @param buff read buffer address
             * @param len read buffer length
             * @param errcode if there is any error , output it
             */
            virtual void read(int ssz, const char *buff, size_t len, int &errcode) = 0;

            /**
             * @biref call this when need to write custem message to peer. custom protocol must implement this
             * @param buffer written buffer address
             * @param len written buffer length
             * @note if there is any writing not finished, call set_flag(flag_t::EN_PFT_WRITING, true) and call write_done(status) when
             *       writing finished
             * @return 0 or error code
             */
            virtual int write(const void *buffer, size_t len) = 0;

            /**
             * @biref call this to notify protocol object last write is finished.
             * @param status written status
             * @return 0 or error code
             */
            virtual int write_done(int status);

            /**
             * @biref call this to close protocol's resource.
             * @note must call set_flag(flag_t::EN_PFT_CLOSING, true), and call set_flag(flag_t::EN_PFT_CLOSED, true) only if all resource
             *       are real closed.
             * @param reason close reason
             * @return 0 or error code
             */
            virtual int close(int reason);

            /**
             * @biref call this to check if reconnect is accepted
             * @param other old protocol object
             * @return 0 or error code
             */
            virtual bool check_reconnect(const proto_base *other);

            /**
             * @biref set receive buffer limit, it's useful only if custom protocol implement this
             * @param max_size max size, 0 for umlimited
             * @param max_number max max_number, 0 for umlimited
             */
            virtual void set_recv_buffer_limit(size_t max_size, size_t max_number);

            /**
             * @biref set send buffer limit, it's useful only if custom protocol implement this
             * @param max_size max size, 0 for umlimited
             * @param max_number max max_number, 0 for umlimited
             */
            virtual void set_send_buffer_limit(size_t max_size, size_t max_number);

            /**
             * @biref notify handshake finished
             * @note custom protocol should call it when handshake is finished or updated no matter if it's success
             * @note it will set EN_PFT_HANDSHAKE_DONE to true, EN_PFT_HANDSHAKE_UPDATE to false, and then call the on_handshake_done_fn
             * callback
             * @param status status
             * @return 0 or error code
             */
            virtual int handshake_done(int status);
            int inner_handshake_done(int status);

            /**
             * @biref update and do handshake again
             * @note custom protocol could use this function to implement the update of crypt secret, access token or other data
             * @note it will set EN_PFT_HANDSHAKE_UPDATE to true
             * @param status status
             * @return 0 or error code
             */
            virtual int handshake_update();

            /**
            * @biref get protocol information
            * @return protocol information
            */
            virtual std::string get_info() const;

        public:
            /**
             * @biref get thread-local storage buffer limit for message encrypt/decrypt, zip/unzip and etc
             * @param tls_type type, different type has different address
             * @return thread-local storage buffer address
             */
            static void *get_tls_buffer(tls_buffer_t::type tls_type);

            /**
             * @biref get thread-local storage buffer length for message encrypt/decrypt, zip/unzip and etc
             * @param tls_type type, different type has different length
             * @return thread-local storage buffer length
             */
            static size_t get_tls_length(tls_buffer_t::type tls_type);

        public:
            /**
             * @biref get callback group
             * @return callback group
             */
            inline proto_callbacks_t *get_callbacks() const { return callbacks_; }

            /**
             * @biref set callback group, it's usually used for outer engine to do the real things to write or read data.
             * @param callbacks callback group
             */
            inline void set_callbacks(proto_callbacks_t *callbacks) { callbacks_ = callbacks; }

            /**
             * @biref get private data
             * @return private data pointer
             */
            inline void *get_private_data() const { return private_data_; }

            /**
             * @biref set private data
             * @param priv_data private data pointer
             */
            inline void set_private_data(void *priv_data) { private_data_ = priv_data; }

            /**
             * @biref get write header offset
             * @note write header offset means how many bytes headspace available at the front of passed data when call
             *       **on_write_start_fn_t**
             * @return write header offset
             */
            inline size_t get_write_header_offset() const { return write_header_offset_; }

            /**
             * @biref set write header offset
             * @note write header offset means how many bytes headspace available at the front of passed data when call
             *       **on_write_start_fn_t**
             * @param sz write header offset
             */
            inline void set_write_header_offset(size_t sz) {
                if (0 == sz) {
                    write_header_offset_ = sz;
                } else {
                    // padding to sizeof(size_t)
                    write_header_offset_ = (sz + sizeof(size_t) - 1) & (~(sizeof(size_t) - 1));
                }
            }

        protected:
            int flags_;

            /**
             * @brief instead of malloc new data block, we can use some buffer in write buffer to hold the additional data.
             *      For example, we can store uv_write_t at the head of buffer when we use libuv
             */
            size_t write_header_offset_;

            proto_callbacks_t *callbacks_;
            void *private_data_;
        };
    }
}

#define ATFRAME_GATEWAY_ON_ERROR(errcode, errmsg) \
    if (NULL != callbacks_ && callbacks_->on_error_fn) callbacks_->on_error_fn(this, __FILE__, __LINE__, errcode, errmsg)

#endif
