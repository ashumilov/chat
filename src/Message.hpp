#pragma once

#include "Log.hpp"

struct Message {
    Message() = default;
    Message( const std::string& nickname, MessageBody::Type type )
    {
        message_body_.set_nickname( nickname );
        message_body_.set_type( type );
    }
    enum { HEADER_SIZE = 3, MAX_BODY_SIZE = 256 };
    const char* data() const { return data_; }
    char* data() { return data_; }
    char* body() { return data_ + HEADER_SIZE; }
    size_t body_size() const { return body_size_; }
    size_t size() const { return HEADER_SIZE + body_size_; }
    bool get_header()
    {
        sscanf( data_, "%3lu", &body_size_ );
        if( body_size_ > MAX_BODY_SIZE ) {
            body_size_ = 0;
            return false;
        }
        return true;
    }
    bool serialize()
    {
        LL("serializing message...");
        std::string buffer;
        if( !message_body_.SerializeToString( &buffer ) ) {
            LL("serialize error");
            return false;
        }
        if( buffer.size() > MAX_BODY_SIZE ) {
            LL("message too big");
            return false;
        }
        body_size_ = buffer.size();
        sprintf( data(), "%3lu", body_size_ );
        memcpy( body(), buffer.data(), buffer.size() );
        return true;
    }
    bool parse()
    {
        LL("parsing message...");
        return (serialized_ = message_body_.ParseFromString( std::string( body(), body_size() ) ) );
    }
    MessageBody::Type type() const
    {
        return message_body_.type();
    }
    std::string nickname() const
    {
        return message_body_.nickname();
    }
    std::string text() const
    {
        return message_body_.text();
    }
    bool serialized() const { return serialized_; }
protected:
    char data_[HEADER_SIZE + MAX_BODY_SIZE];
    size_t body_size_;
    MessageBody message_body_;
    bool serialized_ = false;
};

struct AddMessage : public Message {
    AddMessage( const std::string& nickname )
        : Message( nickname, MessageBody::ADD )
    {
        serialized_ = serialize();
    }
};

struct RemoveInactivityMessage : public Message {
    RemoveInactivityMessage( const std::string& nickname )
        : Message( nickname, MessageBody::INACTIVITY )
    {
        serialized_ = serialize();
    }
};

struct RemoveDuplicateMessage : public Message {
    RemoveDuplicateMessage( const std::string& nickname )
        : Message( nickname, MessageBody::DUPLICATE )
    {
        serialized_ = serialize();
    }
};

struct RemoveDisconnectedMessage : public Message {
    RemoveDisconnectedMessage( const std::string& nickname )
        : Message( nickname, MessageBody::DISCONNECTED )
    {
        serialized_ = serialize();
    }
};

struct TextMessage : public Message {
    TextMessage( const std::string& nickname, const std::string& text )
        : Message( nickname, MessageBody::TEXT )
    {
        message_body_.set_text( text );
        serialized_ = serialize();
    }
};
