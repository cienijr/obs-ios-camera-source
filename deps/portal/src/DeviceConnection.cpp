/*
 portal
 Copyright (C) 2020 Will Townsend <will@townsend.io>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along
 with this program. If not, see <https://www.gnu.org/licenses/>
 */

#include "DeviceConnection.hpp"

namespace portal {

#include <iostream>
#include <type_traits>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

template <typename T>
std::ostream& operator<<(typename std::enable_if<std::is_enum<T>::value, std::ostream>::type& stream, const T& e)
{
    return stream << static_cast<typename std::underlying_type<T>::type>(e);
}

DeviceConnection::DeviceConnection(std::string host, int port)
{
    this->host = host;
    this->port = port;
    this->_state = State::Disconnected;
}

DeviceConnection::~DeviceConnection()
{
    portal_log("%s: Deallocating\n", __func__);
}

int create_socket(std::string host, int port)
{
    struct sockaddr_in local = {
            .sin_family = AF_INET,
            .sin_port = htons(port),
    };

    if (port < 1 || port > 65535) {
        std::cout << "Invalid port: " << port << std::endl;
        return -EINVAL;
    }

    if (inet_pton(AF_INET, host.c_str(), &local.sin_addr) <= 0) {
        std::cout << "Invalid host: " << host << std::endl;
        return -EINVAL;
    }

    auto fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cout << "Failed to create socket: " << errno << std::endl;
        return -errno;
    }

    int val = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) < 0) {
        auto _errno = errno;
        std::cout << "Failed to set SO_REUSEADDR: " << _errno << std::endl;
        close(fd);
        return -_errno;
    }

    if (connect(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0) {
        auto _errno = errno;
        std::cout << "Failed to connect to " << host << ":" << port << " - " << _errno << std::endl;
        close(fd);
        return -_errno;
    }

    return fd;
}

bool DeviceConnection::connect()
{
	auto state = getState();

	if (state == State::Connecting) {
		return false;
	}

	setState(State::Connecting);

	auto connectTimeoutMs = 200;
	int retval = 0;
	auto deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(connectTimeoutMs);

	while (std::chrono::steady_clock::now() < deadline) {
        int socketHandle = create_socket(host, port);
		if (socketHandle >= 0) {
			std::cout << "got connection: " << socketHandle
				  << std::endl;
			channel = std::make_shared<Channel>(port, socketHandle);
			channel->setDelegate(shared_from_this());
			channel->start();
			return false;
		}

		retval = socketHandle;

		if (socketHandle == -EINVAL) {
            setState(State::ImpossibleToConnect);
            return true;
		}
	}

	setState(State::FailedToConnect);

	return true;
}

bool DeviceConnection::disconnect() 
{
    if (getState() != State::Connected) {
        return false;
    }

    auto ret = channel->close();
    if (ret == 0) {
        channel = nullptr; // Dealloc the channel
    }

    setState(State::Disconnected);

    return ret;
}

bool DeviceConnection::send(std::vector<char> data)
{
    return channel->send(data);
}

void DeviceConnection::channelDidChangeState(Channel::State state)
{
    if (state == Channel::State::Disconnected) {
        setState(State::Disconnected);
	    //channel->close();
	    //channel = std::shared_ptr<Channel>(new Channel(0, 0));
    }

    else if (state == Channel::State::Errored) {
	    setState(State::Errored);
	    //channel->close();
	    //channel = std::shared_ptr<Channel>(new Channel(0, 0));
    }

    else if (getState() == State::Connecting && state == Channel::State::Connected) {
        setState(State::Connected);
    }
}

void DeviceConnection::channelDidReceiveData(std::vector<char> data) 
{
    if (auto spt = delegate.lock()) {
        spt->connectionDidRecieveData(shared_from_this(), data);
    }
}


void DeviceConnection::channelDidReceivePacket(std::vector<char> packet, int type, int tag)
{

}

void DeviceConnection::channelDidStop()
{

}

void DeviceConnection::setState(State state)
{
    if (getState() == state) {
        return;
    }

    std::cout << "DeviceConnection::setState: " << state << std::endl;

    _state = state;

    if (auto spt = delegate.lock()) {
        spt->connectionDidChangeState(shared_from_this(), state);
    }
}

} // namespace portal
