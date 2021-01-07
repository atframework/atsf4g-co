//
// Created by owent on 2016/10/4.
//

#ifndef RPC_RPC_UTILS_H
#define RPC_RPC_UTILS_H

#pragma once

#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>
#include <std/smart_ptr.h>
#include <time/time_utility.h>

#include <config/compiler/protobuf_prefix.h>

#include <google/protobuf/arena.h>
#include <google/protobuf/timestamp.pb.h>

#include <config/compiler/protobuf_suffix.h>

namespace atapp {
    class app;
}

namespace atframework {
    class RpcTraceSpan;
} // namespace atframework

class dispatcher_implement;

namespace hello {
    class SSMsg;
    class table_all_message;
} // namespace hello

namespace rpc {

    class context {
        UTIL_DESIGN_PATTERN_NOMOVABLE(context)

    public:
        class tracer {
        public:
            tracer();
            ~tracer();

            int return_code(int ret);

        private:
            friend class context;
            int32_t                                     return_code_;
            atframework::RpcTraceSpan *                 trace_span_;
            std::shared_ptr<dispatcher_implement>       dispatcher_;
            std::shared_ptr< ::google::protobuf::Arena> allocator_;
            util::time::time_utility::raw_time_t        start_timepoint_;
        };

        template <class TMsg>
        struct message_holder {

            message_holder(context &ctx) : arena_msg_ptr_(ctx.create<TMsg>()) {}
            message_holder(message_holder &&other) : arena_msg_ptr_(nullptr) {
                using std::swap;

                if (other->GetArena() != nullptr) {
                    arena_msg_ptr_ = ::google::protobuf::Arena::CreateMessage<TMsg>(other->GetArena());
                }
                swap(arena_msg_ptr_, other.arena_msg_ptr_);
                local_msg_.Swap(&other.local_msg_);
            }

            message_holder &operator=(message_holder &&other) {
                using std::swap;

                swap(arena_msg_ptr_, other.arena_msg_ptr_);
                local_msg_.Swap(&other.local_msg_);

                return *this;
            }

            inline const TMsg *operator->() const {
                if (unlikely(nullptr == arena_msg_ptr_)) {
                    return &local_msg_;
                }

                return arena_msg_ptr_;
            }

            inline TMsg *operator->() {
                if (unlikely(nullptr == arena_msg_ptr_)) {
                    return &local_msg_;
                }

                return arena_msg_ptr_;
            }

            inline const TMsg &operator*() const {
                if (unlikely(nullptr == arena_msg_ptr_)) {
                    return local_msg_;
                }

                return *arena_msg_ptr_;
            }

            inline TMsg &operator*() {
                if (unlikely(nullptr == arena_msg_ptr_)) {
                    return local_msg_;
                }

                return *arena_msg_ptr_;
            }

            UTIL_DESIGN_PATTERN_NOCOPYABLE(message_holder)
        private:
            TMsg *arena_msg_ptr_;
            TMsg  local_msg_;
        };

    private:
        context &operator=(const context &) UTIL_CONFIG_DELETED_FUNCTION;

    public:
        context();
        context(context &parent);
        ~context();

        void setup_tracer(tracer &, const char *name, const std::shared_ptr<dispatcher_implement> &dispatcher);

        /**
         * @brief 使用内置的Arena创建protobuf对象。注意，该对象必须是局部变量，不允许转移给外部使用
         *
         * @tparam message类型
         * @return 在arena上分配的对象，失败返回nullptr
         */
        template <class TMSG>
        TMSG *create() {
            // 上面的分支减少一次atomic操作
            if (allocator_) {
                return ::google::protobuf::Arena::CreateMessage<TMSG>(allocator_.get());
            }

            auto arena = mutable_protobuf_arena();
            if (!arena) {
                return nullptr;
            }

            return ::google::protobuf::Arena::CreateMessage<TMSG>(arena.get());
        }

        std::shared_ptr< ::google::protobuf::Arena>        mutable_protobuf_arena();
        const std::shared_ptr< ::google::protobuf::Arena> &get_protobuf_arena() const;
        bool                                               try_reuse_protobuf_arena(const std::shared_ptr< ::google::protobuf::Arena> &arena);

        atframework::RpcTraceSpan *      mutable_trace_span();
        const atframework::RpcTraceSpan *get_trace_span() const;
        void                             set_trace_parent(const atframework::RpcTraceSpan &parent_span);

        /**
         * @brief Set the trace name
         *
         * @param name
         */
        void set_trace_name(const std::string &name);

        /**
         * @brief Set the trace span id
         *
         * @param span_id The ID for a particular span. This may or may not be the same as the trace id.
         */
        void set_trace_span_id(const std::string &span_id);

        /**
         * @brief Set the trace id
         *
         * @param trace_id Every span in a trace shares this ID.
         */
        void set_trace_id(const std::string &trace_id);

        /**
         * @brief Set the current service object, it's used for tracer
         * @param app atapp instance
         */
        static void set_current_service(const atapp::app &app);

    private:
        std::shared_ptr< ::google::protobuf::Arena> allocator_;
        atframework::RpcTraceSpan *                 trace_span_;
    };

    int wait(hello::SSMsg &msg, uint64_t check_sequence);
    int wait(hello::table_all_message &msg, uint64_t check_sequence);
    int wait(std::unordered_map<uint64_t, hello::SSMsg> &msg_waiters);
    int wait(std::unordered_map<uint64_t, hello::SSMsg *> &msg_waiters);
} // namespace rpc

#endif //_RPC_RPC_UTILS_H
