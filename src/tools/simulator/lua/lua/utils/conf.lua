--region geometricconf.lua
--Author : OWenT
--Date   : 2014/10/22
--全局配置

return {
    LOG_LEVEL = {
        DISABLED    = 0,
        FATAL       = 1,
        ERROR       = 2,
        WARNING     = 3,
        INFO        = 4,
        NOTICE      = 5,
        DEBUG       = 6,
        TRACE       = 7,
    },
    log_min_level = 1,
    log_max_level = 6,
    debug_mode = true,
    
    app_setting = {
    }
}

--endregion
