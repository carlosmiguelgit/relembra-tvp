// Copyright 2023 The Forgotten Server Authors and Alejandro Mujica for many specific source code changes, All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#include "otpch.h"

#include "configmanager.h"
#include "connection.h"
#include "outputmessage.h"
#include "protocol.h"
#include "scheduler.h"
#include "server.h"
#include "ban.h"
#include "logger.h"

extern ConfigManager g_config;
extern Ban g_bans;

Connection_ptr ConnectionManager::createConnection(boost::asio::io_service& io_service, ConstServicePort_ptr servicePort)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	auto connection = std::make_shared<Connection>(io_service, servicePort);
	connections.insert(connection);
	return connection;
}

void ConnectionManager::releaseConnection(const Connection_ptr& connection)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	connections.erase(connection);
}

void ConnectionManager::closeAll()
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	for (const auto& connection : connections) {
		try {
			boost::system::error_code error;
			connection->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			connection->socket.close(error);
		} catch (boost::system::system_error&) {
		}
	}
	connections.clear();
}

// Connection

void Connection::close(bool force)
{
	//any thread
	ConnectionManager::getInstance().releaseConnection(shared_from_this());

	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	if (closed) {
		return;
	}
	closed = true;

	if (protocol) {
		g_dispatcher.addTask(
			createTask(std::bind(&Protocol::release, protocol)));
	}

	if (messageQueue.empty() || force) {
		closeSocket();
	} else {
		//will be closed by the destructor or onWriteOperation
	}
}

void Connection::closeSocket()
{
	if (socket.is_open()) {
		try {
			readTimer.cancel();
			writeTimer.cancel();
			boost::system::error_code error;
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			socket.close(error);
		} catch (boost::system::system_error& e) {
			std::cout << "[Network error - Connection::closeSocket] " << e.what() << std::endl;
		}
	}
}

Connection::~Connection()
{
	closeSocket();
}

void Connection::accept(Protocol_ptr protocol)
{
	this->protocol = protocol;
	g_dispatcher.addTask(createTask(std::bind(&Protocol::onConnect, protocol)));

	accept();
}

void Connection::accept()
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));

		// Read size of the first packet
		boost::asio::async_read(socket,
		                        boost::asio::buffer(msg.getBuffer(), NetworkMessage::HEADER_LENGTH),
		                        std::bind(&Connection::parseHeader, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
		std::cout << "[Network error - Connection::accept] " << e.what() << std::endl;
		close(FORCE_CLOSE);
	}
}

void Connection::parseHeader(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		close(FORCE_CLOSE);
		return;
	} else if (closed) {
		return;
	}

	uint32_t timePassed = std::max<uint32_t>(1, (time(nullptr) - timeConnected) + 1);
	if ((++packetsSent / timePassed) > static_cast<uint32_t>(g_config.getNumber(ConfigManager::MAX_PACKETS_PER_SECOND))) {
		g_logger.gameLog(spdlog::level::warn, fmt::format("{:s} disconnected for exceeding packet per second limit.", convertIPToString(getIP())));
		close();
		return;
	}

	if (timePassed > 2) {
		timeConnected = time(nullptr);
		packetsSent = 0;
	}

	uint16_t size = msg.getLengthHeader();
	// only the first packet may contain the proxy identification
	if (!receivedFirstHeader) {
		receivedFirstHeader = true;
		if (size == 0xFFFEu) {
			// OTCv8 proxy, 6 bytes packet
			// Starts from 2 bytes 0xFFFEu, then 4 bytes with IP uint32_t
			auto self(shared_from_this());
			readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
			readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));

			boost::asio::async_read(socket, boost::asio::buffer(msg.getBuffer(), 4), [&, self](const boost::system::error_code& errorCode, size_t) {
				std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
				readTimer.cancel();
				if (errorCode) {
					std::cout << "[Network error - Connection::parseHeader]: "
						<< "Error loading from OTCv8 proxy" << std::endl;
					close(FORCE_CLOSE);
					return;
				}
				uint8_t* msgBuffer = msg.getBuffer();
				realIP = *(uint32_t*)msgBuffer;
				otcProxy = true;
				if (!g_bans.acceptConnection(realIP)) {
					close(FORCE_CLOSE);
					return;
				}
				accept();
				});
			return;
		} else if (size == 0x0A0Du) {
			// HAProxy send-proxy-v2, 26 bytes packet
			// Starts from 2 bytes 0x0A0Du, IP uint32_t starts from 17th byte
			// https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt - Read from Section 2.2 (Binary header format version 2)
			auto self(shared_from_this());
			readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
			readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));

			boost::asio::async_read(socket, boost::asio::buffer(msg.getBuffer(), 26), [&, self](const boost::system::error_code& errorCode, size_t) {
				std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
				readTimer.cancel();
				if (errorCode) {
					std::cout << "[Network error - Connection::parseHeader]: "
						<< "Error loading from HAProxy" << std::endl;
					close(FORCE_CLOSE);
					return;
				}
				uint8_t* msgBuffer = msg.getBuffer();
				realIP = *(uint32_t*)&msgBuffer[14];
				haProxy = true;

				if (!g_bans.acceptConnection(getIP())) {
					close(FORCE_CLOSE);
					return;
				}

				accept();
				});
			return;
		} else {
			if (!g_bans.acceptConnection(getIP())) {
				close(FORCE_CLOSE);
				return;
			}
		}
	}

	if (size == 0 || size >= NETWORKMESSAGE_MAXSIZE - 16) {
		close(FORCE_CLOSE);
		return;
	}

	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()),
		                                    std::placeholders::_1));

		// Read packet content
		msg.setLength(size + NetworkMessage::HEADER_LENGTH);
		boost::asio::async_read(socket, boost::asio::buffer(msg.getBodyBuffer(), size),
		                        std::bind(&Connection::parsePacket, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
		std::cout << "[Network error - Connection::parseHeader] " << e.what() << std::endl;
		close(FORCE_CLOSE);
	}
}

void Connection::parsePacket(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		close(FORCE_CLOSE);
		return;
	} else if (closed) {
		return;
	}

	if (!receivedFirst) {
		// First message received
		receivedFirst = true;

		if (!protocol) {
			// Game protocol has already been created at this point
			protocol = service_port->make_protocol(msg, shared_from_this());
			if (!protocol) {
				close(FORCE_CLOSE);
				return;
			}
		} else {
			msg.skipBytes(1); // Skip protocol ID
		}

		protocol->onRecvFirstMessage(msg);
	} else {
		protocol->onRecvMessage(msg); // Send the packet to the current protocol
	}

	try {
		readTimer.expires_from_now(std::chrono::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()),
		                                    std::placeholders::_1));

		// Wait to the next packet
		boost::asio::async_read(socket,
		                        boost::asio::buffer(msg.getBuffer(), NetworkMessage::HEADER_LENGTH),
		                        std::bind(&Connection::parseHeader, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
		std::cout << "[Network error - Connection::parsePacket] " << e.what() << std::endl;
		close(FORCE_CLOSE);
	}
}

void Connection::send(const OutputMessage_ptr& msg)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	if (closed) {
		return;
	}

	bool noPendingWrite = messageQueue.empty();
	messageQueue.emplace_back(msg);
	if (noPendingWrite) {
		internalSend(msg);
	}
}

void Connection::internalSend(const OutputMessage_ptr& msg)
{
	protocol->onSendMessage(msg);
	try {
		writeTimer.expires_from_now(std::chrono::seconds(CONNECTION_WRITE_TIMEOUT));
		writeTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()),
		                                     std::placeholders::_1));

		boost::asio::async_write(socket,
		                         boost::asio::buffer(msg->getOutputBuffer(), msg->getLength()),
		                         std::bind(&Connection::onWriteOperation, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
		std::cout << "[Network error - Connection::internalSend] " << e.what() << std::endl;
		close(FORCE_CLOSE);
	}
}

uint32_t Connection::getIP()
{
	if (realIP > 0) {
		return realIP;
	}

	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);

	// IP-address is expressed in network byte order
	boost::system::error_code error;
	const boost::asio::ip::tcp::endpoint endpoint = socket.remote_endpoint(error);
	if (error) {
		return 0;
	}

	return htonl(endpoint.address().to_v4().to_ulong());
}

void Connection::onWriteOperation(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	writeTimer.cancel();
	messageQueue.pop_front();

	if (error) {
		messageQueue.clear();
		close(FORCE_CLOSE);
		return;
	}

	if (!messageQueue.empty()) {
		internalSend(messageQueue.front());
	} else if (closed) {
		closeSocket();
	}
}

void Connection::handleTimeout(ConnectionWeak_ptr connectionWeak, const boost::system::error_code& error)
{
	if (error == boost::asio::error::operation_aborted) {
		//The timer has been manually canceled
		return;
	}

	if (auto connection = connectionWeak.lock()) {
		connection->close(FORCE_CLOSE);
	}
}
