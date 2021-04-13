/*
 portal
 Copyright (C) 2018 Will Townsend <will@townsend.io>

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

#include <cstdint>
#include <iostream>

#ifdef WIN32
#include <winsock2.h>
#endif

#include "Protocol.hpp"

namespace portal {

SimpleDataPacketProtocol::SimpleDataPacketProtocol()
{
	std::cout << "SimpleDataPacketProtocol created\n";
}

SimpleDataPacketProtocol::~SimpleDataPacketProtocol()
{
	buffer.clear();
	std::cout << "SimpleDataPacketProtocol destroyed\n";
}

std::vector<SimpleDataPacketProtocol::DataPacket>
SimpleDataPacketProtocol::processData(std::vector<char> data)
{
	if (data.size() > 0) {
		// Add data recieved to the end of buffer.
		buffer.insert(buffer.end(), data.data(),
			      data.data() + data.size());
	}

    size_t naluHeaderLength = 4;

	auto packets = std::vector<DataPacket>();

	while (buffer.size() >= naluHeaderLength) {

	    // whether startcode is 0x000001 or 0x00000001 (3 or 4 bytes)
	    uint32_t currentStartCodeSize = buffer[2] == 1 ? 3 : 4;
        uint32_t naluLength = 0;

	    // search for next startcode
	    for (uint32_t i = currentStartCodeSize; i+3 < buffer.size(); i++) {
            if (
                    buffer[i] == 0 &&
                    buffer[i+1] == 0 && (
                            buffer[i+2] == 1
                            ||
                            (
                                    buffer[i+2] == 0 &&
                                    buffer[i+3] == 1
                            )
                    )
            ) {
                // next startcode at i
                naluLength = i - currentStartCodeSize;
                break;
            }
	    }

	    if (naluLength == 0) {
            // We don't yet have all of the payload in the buffer yet.
            break;
        }

	    std::vector<char> payload;
	    payload.reserve(4 + naluLength);

	    // append startcode
        char startcode[] = {0, 0, 0, 1};
        payload.insert(payload.end(), startcode, startcode+4);

        // append payload
        auto first = buffer.begin() + currentStartCodeSize;
        auto last = buffer.begin() + currentStartCodeSize + naluLength;
        payload.insert(payload.end(), first, last);

		auto packet = DataPacket();
		packet.version = 1; // whatever
		packet.type = 101;  // video
		packet.tag = 0;     // whatever
		packet.data = payload;

		packets.push_back(packet);

		// Remove the data from buffer
		buffer.erase(buffer.begin(), buffer.begin() + currentStartCodeSize + naluLength);
	}

	return packets;
}

void SimpleDataPacketProtocol::reset()
{
	buffer.clear();
}

}
