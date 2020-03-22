#ifndef DATA_PLAYER_CACHE_H
#define DATA_PLAYER_CACHE_H

#pragma once

#include <bitset>

#include <config/compiler_features.h>
#include <design_pattern/noncopyable.h>
#include <std/smart_ptr.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.table.pb.h>

#include <utility/protobuf_mini_dumper.h>

#include <dispatcher/task_manager.h>

class session;

/**
 * @brief 用户数据包装，自动标记写脏
 * @note 能够隐式转换到只读类型，手动使用get或ref函数提取数据会视为即将写脏
 */
template <typename Ty>
class player_cache_dirty_wrapper {
public:
    typedef Ty value_type;

    player_cache_dirty_wrapper() : dirty_(false) {}

    inline bool is_dirty() const { return dirty_; }

    inline void mark_dirty() { dirty_ = true; }

    inline void clear_dirty() { dirty_ = false; }

    const value_type *operator->() const UTIL_CONFIG_NOEXCEPT { return &real_data_; }

    operator const value_type &() const UTIL_CONFIG_NOEXCEPT { return real_data_; }

    const value_type &operator*() const UTIL_CONFIG_NOEXCEPT { return real_data_; }

    const value_type *get() const { return &real_data_; }

    value_type *get() {
        mark_dirty();
        return &real_data_;
    }

    const value_type &ref() const { return real_data_; }

    value_type &ref() {
        mark_dirty();
        return real_data_;
    }

private:
    value_type real_data_;
    bool       dirty_;
};

class player_cache : public std::enable_shared_from_this<player_cache> {
public:
    typedef std::shared_ptr<player_cache> ptr_t;
    friend class player_manager;

protected:
    struct fake_constructor {};

public:
    player_cache(fake_constructor &);
    virtual ~player_cache();

    virtual bool can_be_writable() const;

    virtual bool is_writable() const;

    // 初始化，默认数据
    virtual void init(uint64_t user_id, uint32_t zone_id, const std::string &openid);

    static ptr_t create(uint64_t user_id, uint32_t zone_id, const std::string &openid);

    // 创建默认角色数据
    virtual void create_init(uint32_t version_type);

    // 登入读取用户数据
    virtual void login_init();

    // 刷新功能限制次数
    virtual void refresh_feature_limit();

    // GM操作
    virtual bool gm_init();

    // 是否GM操作
    virtual bool is_gm() const;

    // 登入事件
    virtual void on_login();

    // 登出事件
    virtual void on_logout();

    // 移除事件
    virtual void on_remove();

    // 从table数据初始化
    virtual void init_from_table_data(const hello::table_user &stTableplayer_cache);

    /**
     * @brief 转储数据
     * @param user 转储目标
     * @param always 是否忽略脏数据
     * @return 0或错误码
     */
    virtual int dump(hello::table_user &user, bool always);

    /**
     * @brief 下发同步消息
     */
    virtual void send_all_syn_msg();

    /**
     * @brief 监视关联的Session
     * @param session_ptr 关联的Session
     */
    void set_session(std::shared_ptr<session> session_ptr);

    /**
     * @brief 获取关联的Session
     * @return 关联的Session
     */
    std::shared_ptr<session> get_session();

    bool has_session() const;

    inline const std::string &get_open_id() const { return openid_id_; };
    inline uint64_t           get_user_id() const { return user_id_; };
    inline unsigned long long get_user_id_llu() const { return static_cast<unsigned long long>(get_user_id()); };

    const std::string &get_version() const { return version_; };
    std::string &      get_version() { return version_; };
    void               set_version(const std::string &version) { version_ = version; };

    /**
     * @brief 获取大区号
     */
    inline uint32_t get_zone_id() const { return zone_id_; }

    inline const hello::table_login &get_login_info() const { return login_info_; }
    inline hello::table_login &get_login_info() { return login_info_; }
    void load_and_move_login_info(hello::table_login COPP_MACRO_RV_REF lg, const std::string& ver);

    inline const std::string &get_login_version() const { return login_info_version_; }
    inline std::string &      get_login_version() { return login_info_version_; }

    inline const hello::account_information &get_account_info() const { return account_info_; }
    inline hello::account_information &      get_account_info() { return account_info_.ref(); }

    inline const hello::player_options &get_player_options() const { return player_options_; }
    inline hello::player_options &      get_player_options() { return player_options_.ref(); }

    inline const hello::player_data &get_player_data() const { return player_data_; }

    inline uint32_t get_data_version() const { return data_version_; }

private:
    inline hello::player_data &mutable_player_data() { return player_data_.ref(); }

protected:
    inline void set_data_version(uint32_t ver) { data_version_ = ver; }

private:
    std::string        openid_id_;
    uint64_t           user_id_;
    uint32_t           zone_id_;
    hello::table_login login_info_;
    std::string        login_info_version_;

    std::string version_;
    uint32_t    data_version_;

    std::weak_ptr<session> session_;

    player_cache_dirty_wrapper<hello::account_information>  account_info_;
    player_cache_dirty_wrapper<hello::player_data>          player_data_;
    player_cache_dirty_wrapper<hello::player_options>       player_options_;
};


// 玩家日志输出工具
#ifdef _MSC_VER
#define WPLOGTRACE(PLAYER, fmt, ...) WLOGTRACE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGDEBUG(PLAYER, fmt, ...) WLOGDEBUG("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGNOTICE(PLAYER, fmt, ...) WLOGNOTICE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGINFO(PLAYER, fmt, ...) WLOGINFO("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGWARNING(PLAYER, fmt, ...) WLOGWARNING("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGERROR(PLAYER, fmt, ...) WLOGERROR("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#define WPLOGFATAL(PLAYER, fmt, ...) WLOGFATAL("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), __VA_ARGS__)
#else
#define WPLOGTRACE(PLAYER, fmt, args...) WLOGTRACE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGDEBUG(PLAYER, fmt, args...) WLOGDEBUG("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGNOTICE(PLAYER, fmt, args...) WLOGNOTICE("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGINFO(PLAYER, fmt, args...) WLOGINFO("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGWARNING(PLAYER, fmt, args...) WLOGWARNING("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGERROR(PLAYER, fmt, args...) WLOGERROR("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#define WPLOGFATAL(PLAYER, fmt, args...) WLOGFATAL("player %s(%u:%llu) " fmt, (PLAYER).get_open_id().c_str(), (PLAYER).get_zone_id(), (PLAYER).get_user_id_llu(), ##args)
#endif

#endif