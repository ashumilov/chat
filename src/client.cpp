#include <cstdlib>
#include <iostream>
#include "message.pb.h"
#include "CLI11.hpp"
#include <asio.hpp>
#include "Message.hpp"
#include "Log.hpp"

#if !defined(ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
int main() {}
#else

using asio::ip::tcp;

struct Client {
    Client( asio::io_service& io_service, const std::string& address,
            const std::string& port, const std::string& nickname )
        : io_service_( io_service ), socket_( io_service ), nickname_( nickname ),
          input_( io_service, ::dup( STDIN_FILENO ) ),
          output_( io_service, ::dup( STDOUT_FILENO ) ),
          input_buffer_( Message::MAX_BODY_SIZE ),
          connection_timer_( io_service ),
          retry_timer_( io_service )
    {
        tcp::resolver resolver( io_service );
        endpoint_iterator_ = resolver.resolve( { address, port } );
        connect();
    }
private:
    enum { CONNECTION_SECONDS = 3, RETRY_SECONDS = 5 };
    void start_connection_timer()
    {
        LL("%s: start connection timer", nickname_.c_str());
        connection_timer_.expires_from_now( boost::posix_time::seconds( CONNECTION_SECONDS ) );
        connection_timer_.async_wait( [this]( const asio::error_code& ) {
                if( connection_timer_.expires_at() <= asio::deadline_timer::traits_type::now() )
                {
                    LL("%s: connection timer expired", nickname_.c_str());
                    socket_.close();
                    connection_timer_.expires_at( boost::posix_time::pos_infin );
                }
            } );
    }
    void stop_connection_timer()
    {
        LL("%s: stop connection timer", nickname_.c_str());
        connection_timer_.expires_at( boost::posix_time::pos_infin );
    }
    void reconnect()
    {
        LL("%s: reconnecting...", nickname_.c_str());
        retry_timer_.expires_from_now( boost::posix_time::seconds( RETRY_SECONDS ) );
        LL("%s: waiting for %d seconds...", nickname_.c_str(), RETRY_SECONDS);
        retry_timer_.async_wait( [this]( const asio::error_code& ec ) {
                if( retry_timer_.expires_at() <= asio::deadline_timer::traits_type::now() )
                {
                    LL("%s: retry timer expired", nickname_.c_str());
                    connect();
                }
            } );
    }
    void stop_retry_timer()
    {
        LL("%s: stop retry timer", nickname_.c_str());
        retry_timer_.expires_at( boost::posix_time::pos_infin );
    }
    void connect()
    {
        LL("%s: connecting...", nickname_.c_str());
        stop_retry_timer();
        start_connection_timer();
        asio::async_connect( socket_, endpoint_iterator_,
                             [this]( std::error_code ec, tcp::resolver::iterator )
                             {
                                 stop_connection_timer();
                                 LL("%s: handle connect", nickname_.c_str());
                                 if( !socket_.is_open() )
                                 {
                                     LL("%s: connect timout", nickname_.c_str());
                                     reconnect();
                                 }
                                 else if( ec ) {
                                     LL("%s: connect error: %s", nickname_.c_str(), ec.message().c_str());
                                     socket_.close();
                                     reconnect();
                                 } else {
                                     LL("%s: connected", nickname_.c_str());
                                     auto message = AddMessage( nickname_ );
                                     send_message( message );
                                     receive_input();
                                     receive_message_header();
                                 }
                             }
            );
    }
    void receive_input()
    {
        LL("%s: waiting for input...", nickname_.c_str());
        asio::async_read_until( input_, input_buffer_, '\n',
                                [this]( const asio::error_code ec, size_t size )
                                {
                                    LL("%s: got input", nickname_.c_str());
                                    process_input( ec, size );
                                }
            );
    }
    void receive_message_header()
    {
        LL("%s: receiving message header...", nickname_.c_str());
        asio::async_read( socket_,
                          asio::buffer( received_message_.data(), Message::HEADER_SIZE ),
                          [this]( std::error_code ec, size_t )
                          {
                              LL("%s: received message header", nickname_.c_str());
                              if( !ec && received_message_.get_header() ) {
                                  receive_message_body();
                              } else {
                                  LL("%s: receive header error: %s", nickname_.c_str(), ec.message().c_str());
                                  close();
                              }
                          }
            );
    }
    void receive_message_body()
    {
        LL("%s: receiving message body...", nickname_.c_str());
        asio::async_read( socket_,
                          asio::buffer( received_message_.body(), received_message_.body_size() ),
                          [this]( std::error_code ec, size_t )
                          {
                              LL("%s: received message body", nickname_.c_str());
                              if( !ec ) {
                                  process_received_message( received_message_ );
                              } else {
                                  LL("%s: receive body error: %s", nickname_.c_str(), ec.message().c_str());
                                  close();
                              }
                          }
            );
    }
    void send_message( const Message& message )
    {
        LL("%s: send message", nickname_.c_str());
        if( !message.serialized() ) {
            LL("%s: message is skipped", nickname_.c_str());
            io_service_.post(
                [this]()
                {
                    receive_input();
                }
                );
            return;
        }
        LL("%s: message to send (%s,%d)", nickname_.c_str(), message.data(), message.size());
        asio::async_write( socket_,
                           asio::buffer( message.data(), message.size() ),
                           [this]( std::error_code ec, size_t )
                           {
                               LL("%s: message sent", nickname_.c_str());
                               if( !ec ) {
                                   receive_input();
                               } else {
                                   LL("%s: send message error: %s", nickname_.c_str(), ec.message().c_str());
                                   close();
                               }
                           }
            );
    }
    void process_received_message( Message &message )
    {
        if( !message.parse() )
        {
            LL("%s: message parse error", nickname_.c_str());
            return;
        }
        std::string message_to_output;
        switch( message.type() )
        {
            case MessageBody::ADD:
                message_to_output = message.nickname() + " joined the chat\n";
                break;
            case MessageBody::INACTIVITY:
                message_to_output = message.nickname() + " left the chat due inactivity\n";
                break;
            case MessageBody::DUPLICATE:
                message_to_output = "nickname '" + message.nickname() + "' already exists\n";
                break;
            case MessageBody::DISCONNECTED:
                message_to_output = message.nickname() + " left the chat, connection lost\n";
                break;
            case MessageBody::TEXT:
                message_to_output = message.nickname() + ": " + message.text();
                break;
            default:
                assert(true);   // no way
        }
        LL("%s: output message", nickname_.c_str());
        asio::async_write( output_, asio::buffer( message_to_output.data(), message_to_output.size() ),
                           [this]( const asio::error_code &ec, size_t size )
                           {
                               if( !ec ) {
                                   receive_message_header();
                               } else {
                                   close();
                               }
                           }
            );
    }
    void process_input( const asio::error_code& ec, size_t size )
    {
        LL("%s: process input (%d,%d)", nickname_.c_str(), size, input_buffer_.size());
        if( !ec || ec == asio::error::not_found ) {
            char buffer[Message::MAX_BODY_SIZE];
            size = input_buffer_.sgetn( buffer, ec ? input_buffer_.size() : size );
            input_buffer_.consume( input_buffer_.size() );
            std::string text( buffer, size );
            if( ec )
                text += '\n';
            auto message = TextMessage ( nickname_, text );
            send_message( message );
        } else {
            LL("%s: process input error: %s", nickname_.c_str(), ec.message().c_str());
            close();
        }
    }
    void close()
    {
        LL("%s: close client", nickname_.c_str());
        socket_.close();
        input_.close();
        output_.close();
        io_service_.stop();
    }

    asio::io_service& io_service_;
    tcp::socket socket_;
    Message received_message_;
    std::string nickname_;
    asio::posix::stream_descriptor input_;
    asio::posix::stream_descriptor output_;
    asio::streambuf input_buffer_;
    asio::deadline_timer connection_timer_;
    asio::deadline_timer retry_timer_;
    tcp::resolver::iterator endpoint_iterator_;
};

int main( int argc, char *argv[] )
{
    CLI::App app("Chat client");
    std::string address;
    std::string port;
    std::string nickname;
    app.add_option("-a,--address", address, "address to connect")->required();
    app.add_option("-p,--port", port, "port to connect")->required();
    app.add_option("-n,--nickname", nickname, "nickname")->required();
    CLI11_PARSE(app, argc, argv);

    try {
        asio::io_service io_service;
        Client client( io_service, address, port, nickname );
        io_service.run();
    }
    catch( std::exception& e ) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
#endif
