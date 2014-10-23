/*
* Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
* Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "WorldSocket.h"
#include "Opcodes.h"
#include "PacketLog.h"
#include "Player.h"
#include <memory>

using boost::asio::ip::tcp;

WorldSocket::WorldSocket(tcp::socket&& socket)
    : Socket(std::move(socket), sizeof(ClientPktHeader)), _worldSession(nullptr)
{
}

void WorldSocket::Start()
{
    AsyncReadHeader();
}

void WorldSocket::ReadHeaderHandler()
{
    ClientPktHeader* header = reinterpret_cast<ClientPktHeader*>(GetHeaderBuffer());

    if (!header->IsValid())
    {
        if (_worldSession)
        {
          //  Player* player = _worldSession->GetPlayer();
          //  TC_LOG_ERROR("network", "WorldSocket::ReadHeaderHandler(): client (account: %u, char [GUID: %u, name: %s]) sent malformed packet (size: %hu, cmd: %u)",
              //  _worldSession->GetAccountId(), player ? player->GetGUIDLow() : 0, player ? player->GetName().c_str() : "<none>", header->size, header->cmd);
        }
        else
            TC_LOG_ERROR("network", "WorldSocket::ReadHeaderHandler(): client %s sent malformed packet (size: %hu, cmd: %u)",
                GetRemoteIpAddress().to_string().c_str(), header->size, header->cmd);

        CloseSocket();
        return;
    }

	AsyncReadData(header->size - sizeof(ClientPktHeader));
}

void WorldSocket::ReadDataHandler()
{
    ClientPktHeader* header = reinterpret_cast<ClientPktHeader*>(GetHeaderBuffer());

    uint16 opcode = uint16(header->cmd);

    std::string opcodeName = GetOpcodeNameForLogging(opcode);

    WorldPacket packet(opcode, MoveData());

    if (sPacketLog->CanLogPacket())
        sPacketLog->LogPacket(packet, CLIENT_TO_SERVER, GetRemoteIpAddress(), GetRemotePort());

   // TC_LOG_TRACE("network.opcode", "C->S: %s %s", (_worldSession ? _worldSession->GetPlayerInfo() : GetRemoteIpAddress().to_string()).c_str(), opcodeName.c_str());
	
	packet.read_skip<uint32[2]>();

    switch (opcode)
    {
		case CMSG_PLAYER_LOGIN:
            if (_worldSession)
            {
            //    TC_LOG_ERROR("network", "WorldSocket::ProcessIncoming: received duplicate CMSG_AUTH_SESSION from %s", _worldSession->GetPlayerInfo().c_str());
                break;
            }

            AddSession(packet);
            break;
        default:
        {
            if (!_worldSession)
            {
                TC_LOG_ERROR("network.opcode", "ProcessIncoming: Client not authed opcode = %u", uint32(opcode));
                CloseSocket();
                return;
            }

            // Our Idle timer will reset on any non PING opcodes.
            // Catches people idling on the login screen and any lingering ingame connections.
            _worldSession->ResetTimeOutTime();

            // Copy the packet to the heap before enqueuing
            _worldSession->QueuePacket(new WorldPacket(std::move(packet)));
            break;
        }
    }

    AsyncReadHeader();
}

void WorldSocket::AsyncWrite(WorldPacket& packet)
{
    if (!IsOpen())
        return;

    if (sPacketLog->CanLogPacket())
        sPacketLog->LogPacket(packet, SERVER_TO_CLIENT, GetRemoteIpAddress(), GetRemotePort());

  //  TC_LOG_TRACE("network.opcode", "S->C: %s %s", (_worldSession ? _worldSession->GetPlayerInfo() : GetRemoteIpAddress().to_string()).c_str(), GetOpcodeNameForLogging(packet.GetOpcode()).c_str());

	uint32 Opcode = packet.GetOpcode();
	ServerPktHeader header(packet.size() + sizeof(Opcode), Opcode);

    std::lock_guard<std::mutex> guard(_writeLock);

    bool needsWriteStart = _writeQueue.empty();

    _writeQueue.emplace(header, packet);

    if (needsWriteStart)
        AsyncWrite(_writeQueue.front());
}

void WorldSocket::AddSession(WorldPacket& recvPacket)
{
	_worldSession = new WorldSession(recvPacket.peek<uint32>(20), shared_from_this());
	_worldSession->QueuePacket(new WorldPacket(std::move(recvPacket)));

	sWorld->AddSession(_worldSession);
}

void WorldSocket::CloseSocket()
{
    Socket::CloseSocket();
}
