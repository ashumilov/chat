#include <cstdlib>
#include <iostream>
#include <deque>
#include <set>
#include "message.pb.h"
#include "CLI11.hpp"
#include <boost/asio.hpp>
#include "Message.hpp"
#include "Log.hpp"

using boost::asio::ip::tcp;

struct Server;
struct Session : public std::enable_shared_from_this<Session> {
    Session( boost::asio::io_service& io_service, tcp::socket socket, Server& server )
        : io_service_( io_service ), socket_( std::move( socket ) ),
          server_( server ), inactivity_timer_( io_service, boost::posix_time::seconds( int (INACTIVITY_SECONDS) ) )
    {}
    enum { INACTIVITY_SECONDS = 10 };
    enum class Error {
        NO_ERROR,
        INACTIVITY,
        DUPLICATE,
    };

    void run();
    bool process_message( Message& message );

    void send_message( const Message& message )
    {
        LL("server: send message(%s -> %s)", message.nickname().c_str(), nickname_.c_str());
        if( nickname_.empty() || nickname_ == message.nickname() ) {
            return;
        }
        if( !message.serialized() ) {
            LL("server: message is not serialized");
            return;
        }
        bool send_in_progress = messages_to_send_.size();
        messages_to_send_.push_back( message );
        if( !send_in_progress ) {
            send_message();
        }
    }
    void send_message_and_close( const Message& message )
    {
        LL("server: sending message...");
        if( !message.serialized() ) {
            return;
        }
        boost::asio::async_write( socket_,
                           boost::asio::buffer( message.data(), message.size() ),
                           [this]( std::error_code ec, size_t )
                           {
                               LL("server: message sent, close");
                               error_ = Error::DUPLICATE;
                               socket_.close();
                           }
            );
    }
    void start_inactivity_timer()
    {
        LL("server: inactivity timer started");
        inactivity_timer_.async_wait(
            [this]( const boost::system::error_code& ec ) {
                if( !ec ) {
                    LL("server: inactivity timer expired");
                    error_ = Error::INACTIVITY;
                    socket_.close();
                }
            } );
    }
    void restart_inactivity_timer()
    {
        LL("server: trying to restart inactivity timer ");
        if( inactivity_timer_.expires_from_now( boost::posix_time::seconds( int(INACTIVITY_SECONDS) ) ) > 0 )
        {
            LL("server: inactivity timer restarted");
            start_inactivity_timer();
        }
        else
        {
            LL("server: too late ");
        }
    }
    const std::string& nickname() const { return nickname_; }
    Error error() const { return error_; }
private:
    boost::asio::io_service& io_service_;
    void receive_message_header();
    void receive_message_body();
    void send_message();
    tcp::socket socket_;
    Server& server_;
    Message received_message_;
    std::deque<Message> messages_to_send_;
    std::string nickname_;
    boost::asio::deadline_timer inactivity_timer_;
    Error error_;
};

struct Server {
    Server( boost::asio::io_service& io_service, int port )
        : io_service_( io_service ), acceptor_( io_service, tcp::endpoint( tcp::v4(), port )),
          socket_( io_service )
    {
        LL("server: started with port %d", port );
        accept_connection();
    }
    void accept_connection()
    {
        LL("server: accept waiting for connection...");
        acceptor_.async_accept( socket_, [this] (std::error_code ec) {
                LL("server: client connected");
                if( !ec ) {
                    auto session = std::make_shared<Session>( io_service_, std::move( socket_ ), *this );
                    session->run();
                }
                accept_connection();
            } );
    }
    void add( std::shared_ptr<Session> session )
    {
        LL("server: add session to server");
        sessions_.insert( session );
    }
    void remove( std::shared_ptr<Session> session )
    {
        auto error = session->error();
        auto nickname = session->nickname();
        LL("server: remove session from server");
        sessions_.erase( session );
        switch( error ) {
            case Session::Error::INACTIVITY: {
                Message message = RemoveInactivityMessage( nickname );
                send_broadcast( message );
                break;
            }
            case Session::Error::DUPLICATE:
                ; // do nothing
                break;
            default: {
                Message message = RemoveDisconnectedMessage( nickname );
                send_broadcast( message );
            }
        }
    }
    void send_broadcast( const Message& message )
    {
        LL("server: send message broadcast");
        for( auto session: sessions_ )
            session->send_message( message );
    }
    bool validate_nickname( const std::string& nickname ) const
    {
        LL("server: validate nickname %s", nickname.c_str());
        for( auto session: sessions_ ) {
            LL("server: comparing %s with %s ...", nickname.c_str(), session->nickname().c_str() );
            if( nickname == session->nickname() )
                return false;
        }
        return true;
    }

private:
    boost::asio::io_service& io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    std::set<std::shared_ptr<Session> > sessions_;
};

void Session::run()
{
    LL("server: start session");
    server_.add( shared_from_this() );
    start_inactivity_timer();
    receive_message_header();
}
void Session::receive_message_header()
{
    LL("server: receiving message header...");
    boost::asio::async_read( socket_,
                      boost::asio::buffer( received_message_.data(), Message::HEADER_SIZE),
                      [this]( std::error_code ec, size_t )
                      {
                          LL("server: received message header");
                          if( !ec && received_message_.get_header() ) {
                              restart_inactivity_timer();
                              receive_message_body();
                          } else {
                              LL("server: receive header error: %s", ec.message().c_str());
                              server_.remove( shared_from_this() );
                          }
                      }
        );
}
void Session::receive_message_body()
{
    LL("server: receiving message body...");
    boost::asio::async_read( socket_,
                      boost::asio::buffer( received_message_.body(), received_message_.body_size()),
                      [this]( std::error_code ec, size_t )
                      {
                          LL("server: message received (%s,%d)", received_message_.data(), received_message_.size());
                          if( !ec ) {
                              restart_inactivity_timer();
                              if( process_message( received_message_ ) ) {
                                  server_.send_broadcast( received_message_ );
                                  receive_message_header();
                              }
                          } else {
                              LL("server: receive body error: %s", ec.message().c_str());
                              server_.remove( shared_from_this() );
                          }
                      }
        );
}
void Session::send_message()
{
    LL("server: sending message message...");
    auto message = messages_to_send_.front();
    boost::asio::async_write( socket_,
                       boost::asio::buffer( message.data(), message.size() ),
                       [this]( std::error_code ec, size_t )
                       {
                           LL("server: message sent");
                           if( !ec ) {
                               messages_to_send_.pop_front();
                               if( messages_to_send_.size() ) {
                                   send_message();
                               }
                           } else {
                               LL("server: send error: %s", ec.message().c_str());
                               server_.remove( shared_from_this() );
                           }
                       }
        );
}
bool Session::process_message( Message& message )
{
    LL("server: process message");
    if( !message.parse() ) {
        return false;
    }
    if( message.type() == MessageBody::ADD ) {
        if( !server_.validate_nickname( message.nickname() ) ) {
            LL("server: nickname %s already exists", message.nickname().c_str());
            send_message_and_close( RemoveDuplicateMessage( message.nickname() ) );
            return false;
        } else {
            nickname_ = message.nickname();
            LL("server: nickname %s added", nickname_.c_str());
        }
    }
    return true;
}

int main( int argc, char *argv[] )
{
    CLI::App app("Chat server");
    int port;
    app.add_option("-p,--port", port, "port number to listen")->required();
    CLI11_PARSE(app, argc, argv);

    try {
        boost::asio::io_service io_service;
        Server server( io_service, port );
        io_service.run();
    }
    catch( std::exception& e ) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
