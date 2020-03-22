//
// Created by owt50 on 2016/10/9.
//
#include <cstdlib>
#include <std/smart_ptr.h>

#include <cli/cmd_option_phoenix.h>
#include <utility/client_config.h>
#include <utility/client_simulator.h>


#include <libatgw_inner_v1_c.h>

int main(int argc, char *argv[]) {
    libatgw_inner_v1_c_global_init_algorithms();

    std::shared_ptr<client_simulator> client = std::make_shared<client_simulator>();
    client->init();

    client->get_option_manager()
        ->bind_cmd("-ip, --host", util::cli::phoenix::assign(client_config::host))
        ->set_help_msg("<domain or ip> set host or ip address of loginsvr");

    client->get_option_manager()->bind_cmd("-p, --port", util::cli::phoenix::assign(client_config::port))->set_help_msg("<port> set port of loginsvr");

    client_player::init_handles();
    return client->run(argc, (const char **)argv);
}