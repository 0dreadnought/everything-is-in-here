
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cstdlib>

#include <atomic>
#include <iostream>

// IOT socket api
#include <iot/socket.hpp>

#include <chat.hpp>
#include <gui.hpp>
#include <colors.hpp>
#include <util.hpp>

namespace {
std::atomic<bool> sent_leave{false};
};

//---------------------------------------------------------------------------------------

/**
 * @brief Convert a string command from the UI into a chat command.
 *  NOTE: It is only a subset of all command types.
 * 
 * @param cmd command to convert
 * @return command type ID representing the passed in command
*/
chat::chat_type to_type(std::string cmd) {
  switch(string_to_int(cmd.c_str())) {
    // case string_to_int("join"): return chat::JOIN;
    // case string_to_int("bc"): return chat::BROADCAST;
    // case string_to_int("dm"): return chat::DIRECTMESSAGE;
    case string_to_int("list"): return chat::LIST;
    case string_to_int("leave"): return chat::LEAVE;
    case string_to_int("exit"): return chat::EXIT;
    default:
      return chat::UNKNOWN; 
  }

  return chat::UNKNOWN; // unknowntype
}

//----------------------------------------------------------------------------------------

std::pair<std::thread, Channel<chat::chat_message>> make_receiver(uwe::socket* sock) {
  auto [tx, rx] = make_channel<chat::chat_message>();
  
  std::thread receiver_thread{[](Channel<chat::chat_message> tx, uwe::socket* sock) { 
    try {
        for (;;) {
            chat::chat_message msg;
            
            // you need to fill in
            // receive message from server
            // send it over channel (tx) to main UI thread

            // receive message from server, send it over channel (tx) to main UI thread
            int len = sock->recvfrom(reinterpret_cast<char*>(&msg), sizeof(chat::chat_message), 0,nullptr,nullptr);
            tx.send(msg);
            
            // exit receiver thread
            if (msg.type_ == chat::EXIT || (msg.type_ == chat::LACK && sent_leave)) {
                break;
            }
        }
    }
    catch(...) {
        DEBUG("caught exception\n");
    };
  }, std::move(tx), sock};

  return {std::move(receiver_thread), std::move(rx)};
}

int main(int argc, char ** argv) {
    if (argc != 4) {
        printf("USAGE: %s <ipaddress> <port> <username>\n", argv[0]);
        exit(0);
    }

    std::string username{argv[3]};
    // Set client IP address
    uwe::set_ipaddr(argv[1]);

    const char* server_name = "192.168.1.8";
	
	const int server_port = 8866;

    sockaddr_in server_address;
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;

	// creates binary representation of server name and stores it as sin_addr
	inet_pton(AF_INET, server_name, &server_address.sin_addr);

	// htons: port in network order format
	server_address.sin_port = htons(server_port);

	// open socket
	uwe::socket sock{AF_INET, SOCK_DGRAM, 0};

	// port for client
	const int client_port = std::atoi(argv[2]);

	// socket address used for the client
	struct sockaddr_in client_address;
	memset(&client_address, 0, sizeof(client_address));
	client_address.sin_family = AF_INET;
	client_address.sin_port = htons(client_port);
	inet_pton(AF_INET, uwe::get_ipaddr().c_str(), &client_address.sin_addr);

	sock.bind((struct sockaddr *)&client_address, sizeof(client_address));

    chat::chat_message msg = chat::join_msg(username);

    // send data
	int len = sock.sendto(
        reinterpret_cast<const char*>(&msg), sizeof(chat::chat_message), 0,
	    (sockaddr*)&server_address, sizeof(server_address));
        
    DEBUG("Join message (%s) sent, waiting for JACK\n", username.c_str());
    // wait for JACK
    sock.recvfrom(reinterpret_cast<char*>(&msg), sizeof(chat::chat_message), 0, nullptr, nullptr);

    if (msg.type_ == chat::JACK) {
        DEBUG("Received jack\n");

        // create GUI thread and communication channels
        auto [gui_thread, gui_tx, gui_rx] = chat::make_gui();
        auto [rec_thread, rec_rx] = make_receiver(&sock);

        // going to need recv thread for messages from server

        bool exit_loop = false;
        for(;!exit_loop;) {
            // check and see if any GUI messages to handle
            if (!gui_rx.empty() && !sent_leave) {
                auto result = gui_rx.recv();
                if (result) {
                    auto cmds = split(*result, ':');
                    if (cmds.size() > 1) {
                        chat::chat_type type = to_type(cmds[0]);
                        switch(type) {
                            case chat::EXIT: {
                                DEBUG("Received Exit from GUI\n");
                                //set up am exit message
                                auto msg =chat::exit_msg();
                                //send it
                                int len = sock.sendto(reinterpret_cast<const char*>(&msg),sizeof(chat::chat_message),
                                0, (sockaddr*)&server_address, sizeof(server_address));
                                //terminate
                                sent_leave = true;
                                exit_loop=true;
                                // you need to fill in
                                break;
                            }
                            case chat::LEAVE: {
                                DEBUG("Received LEAVE from GUI\n");
                                //set up leave message
                                auto msg = chat::leave_msg();
                                //send it so the server knows
                                int len = sock.sendto(reinterpret_cast<const char*>(&msg), sizeof(chat::chat_message), 0,
                                    (sockaddr*)&server_address, sizeof(server_address));
                                //terminate
                                sent_leave=true;
                                // you need to fill in
                                break;
                            }
                            case chat::LIST: {
                                //set up a list message
                                auto msg = chat::list_msg();
                                //send it
                                int len = sock.sendto(reinterpret_cast<const char*>(&msg), sizeof(chat::chat_message), 0,
                                    (sockaddr*)&server_address, sizeof(server_address));
                                // you need to fill in
                                break;
                            }
                            default: {
                                // the default case is that the command is a username for DM
                                // <username> : message
                                if (cmds.size() == 2) {
                                    //set up a dm
                                    auto msg = chat::dm_msg(cmds[0],cmds[1]);
                                    //send it
                                    int len = sock.sendto(reinterpret_cast<const char*>(&msg), sizeof(chat::chat_message), 0,
                                        (sockaddr*)&server_address, sizeof(server_address));
                                }
                                break;
                            }
                        } 
                    }
                    else {
                        // message to broadcast to everyone online
                        chat::chat_message msg = chat::broadcast_msg(username, *result);
                        // send data
                        int len = sock.sendto(
                            reinterpret_cast<const char*>(&msg), sizeof(chat::chat_message), 0,
                            (sockaddr*)&server_address, sizeof(server_address));
                    }
                }
            }
            //check to see if any messages received from the server
            if (!rec_rx.empty() && !exit_loop) {
                auto result = rec_rx.recv();
                if (result) {
                    switch ((*result).type_) {
                        case chat::LEAVE: {
                            chat::display_command cmd{chat::GUI_USER_REMOVE};
                            cmd.text_ = std::string{(char*)(*result).username_};
                            gui_tx.send(cmd);
                            break;
                        }
                        case chat::EXIT: {
                            DEBUG("Received EXIT\n");
                            exit_loop = true;
                            break;
                        }
                        case chat::LACK: {
                            DEBUG("Received LACK\n");
                            if (sent_leave) {
                                exit_loop = true;
                                break;
                            }
                        }
                        case chat::BROADCAST: {
                            std::string msg{(char*)(*result).username_};
                            msg.append(": ");
                            msg.append((char*)(*result).message_);
                            chat::display_command cmd{chat::GUI_CONSOLE, msg};
                            gui_tx.send(cmd);
                            break;
                        }
                        case chat::DIRECTMESSAGE: {
                            std::string msg{"dm("};
                            msg.append((char*)(*result).username_);
                            msg.append("): ");
                            msg.append((char*)(*result).message_);
                            chat::display_command cmd{chat::GUI_CONSOLE, msg};
                            gui_tx.send(cmd);
                            break;
                        }
                        case chat::LIST: {
                            bool end = false;
                            auto users = split(std::string{(char*)(*result).username_}, ':');
                            for (auto u: users) {
                                if (u.compare("END") == 0) {   
                                    end = true;
                                    break;
                                }
                                chat::display_command cmd{chat::GUI_USER_ADD, u};
                                gui_tx.send(cmd);
                            }

                            if (!end) {
                                auto users = split(std::string{(char*)(*result).message_}, ':');
                                for (auto u: users) {
                                    if (u.compare("END") == 0) {
                                        break;
                                    }
                                }
                            }

                            break;
                        }
                        case chat::ERROR: {
                            break;
                        }
                        default: {

                        }
                    }
                }
            }
        }

        DEBUG("Exited loop\n");
        // send message to GUI to exit
        chat::display_command cmd{chat::GUI_EXIT};
        gui_tx.send(cmd);
        gui_thread.join();
        rec_thread.join();
        
        // so done...
        DEBUG("Time to rest\n");
    }
    else {
        DEBUG("Received invalid jack\n");
    }

    return 0;
}