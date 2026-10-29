  ::PROJECT_NAMESPACE_ID::config::ExcelConstConfig const_settings;
  std::unordered_map<int32_t, size_t> matching_pool_faction_capacity;
  rank_index_t rank_index;
  std::unordered_map<uint32_t, ::excel::excel_config_type_traits::shared_ptr<::atframework::dtmq::DChannelConfigure> >
    dtmq_channel_type_configure;

  using mail_template_engine_t = simple_template<mail_render_data_type>;
  struct mail_template_set_t {
    mail_template_engine_t::shared_ptr_t title;
    mail_template_engine_t::shared_ptr_t content;
  };
  using mail_template_lang_set_t = std::unordered_map<int, mail_template_set_t>;
  std::unordered_map<int32_t, mail_template_lang_set_t> mail_template_engine;
  mail_template_engine_t::config_t mail_template_engine_conf;
  std::unordered_set<int32_t> mail_user_major_types;
  std::unordered_set<int32_t> mail_global_major_types;
