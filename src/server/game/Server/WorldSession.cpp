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

/** \file
    \ingroup u2w
*/

#include "WorldSocket.h"
#include "Config.h"
#include "Common.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"

/// WorldSession constructor
WorldSession::WorldSession(uint32 id, std::shared_ptr<WorldSocket> sock):
    _Socket(sock),
    _accountId(id),
	_player(nullptr),
	_expireTime(60000), // 1 min after socket loss, session is deleted
	_forceExit(false)
{
    if (sock)
    {
        _Address = sock->GetRemoteIpAddress().to_string();
		ResetTimeOutTime();
    }
}

/// WorldSession destructor
WorldSession::~WorldSession()
{
	/// not login game
	if (!_player)
	{
		SendLoginError(3);
	}
    /// - If have unclosed socket, close it
    if (_Socket)
    {
        _Socket->CloseSocket();
        _Socket = nullptr;
    }

    ///- empty incoming packet queue
    WorldPacket* packet = NULL;
    while (_recvQueue.next(packet))
        delete packet;
}

/// Send a packet to the client
void WorldSession::SendPacket(WorldPacket* packet)
{
    if (!_Socket)
        return;

#ifdef TRINITY_DEBUG
    // Code for network use statistic
    static uint64 sendPacketCount = 0;
    static uint64 sendPacketBytes = 0;

    static time_t firstTime = time(NULL);
    static time_t lastTime = firstTime;                     // next 60 secs start time

    static uint64 sendLastPacketCount = 0;
    static uint64 sendLastPacketBytes = 0;

    time_t cur_time = time(NULL);

    if ((cur_time - lastTime) < 60)
    {
        sendPacketCount+=1;
        sendPacketBytes+=packet->size();

        sendLastPacketCount+=1;
        sendLastPacketBytes+=packet->size();
    }
    else
    {
        uint64 minTime = uint64(cur_time - lastTime);
        uint64 fullTime = uint64(lastTime - firstTime);
        TC_LOG_INFO("misc", "Send all time packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f time: %u", sendPacketCount, sendPacketBytes, float(sendPacketCount)/fullTime, float(sendPacketBytes)/fullTime, uint32(fullTime));
        TC_LOG_INFO("misc", "Send last min packets count: " UI64FMTD " bytes: " UI64FMTD " avr.count/sec: %f avr.bytes/sec: %f", sendLastPacketCount, sendLastPacketBytes, float(sendLastPacketCount)/minTime, float(sendLastPacketBytes)/minTime);

        lastTime = cur_time;
        sendLastPacketCount = 1;
        sendLastPacketBytes = packet->wpos();               // wpos is real written size
    }
#endif                                                      // !TRINITY_DEBUG

    _Socket->AsyncWrite(*packet);
}

/// Add an incoming packet to the queue
void WorldSession::QueuePacket(WorldPacket* new_packet)
{
    _recvQueue.add(new_packet);
}

/// Update the WorldSession (triggered by World update)

bool WorldSession::Update(uint32 diff)
{   
	/// Update Timeout timer.
	UpdateTimeOutTime(diff);

	if (IsTimeOutTime())
		_Socket->CloseSocket();

    WorldPacket* packet = NULL;

    uint32 processedPackets = 0;

	while (_Socket && !_recvQueue.empty() && _recvQueue.next(packet))
    {
		OpcodeHandler& opHandle = opcodeTable[packet->GetOpcode()];

		(this->*opHandle.handler)(*packet);

#define MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE 100
        processedPackets++;

        if (processedPackets > MAX_PROCESSED_PACKETS_IN_SAME_WORLDSESSION_UPDATE)
            break;
    }
        ///- Cleanup socket pointer if need
        if (_Socket && !_Socket->IsOpen())
        {
            _expireTime -= _expireTime > diff ? diff : _expireTime;
            if (_expireTime < diff || _forceExit /*|| !GetPlayer()*/)
            {
                _Socket = nullptr;
            }
        }

        if (!_Socket)
            return false;                                   
  
    return true;
}

/// Kick a player out of the World
void WorldSession::KickPlayer()
{
	if (_Socket)
	{
		_Socket->CloseSocket();
		_forceExit = true;
	}
}

void WorldSession::Handle_NULL(WorldPacket& recvPacket)
{
	TC_LOG_ERROR("network.opcode", "Received unhandled opcode %s from "
		, GetOpcodeNameForLogging(recvPacket.GetOpcode()).c_str()/*, GetPlayerInfo().c_str()*/);
}

void WorldSession::SendLoginError(uint8 code)
{
	WorldPacket packet(CMSG_PLAYER_LOGIN, 4);
	packet << uint32(0);
	packet << uint32(0);
	packet << uint32(code);

	SendPacket(&packet);
}

void WorldSession::HandlePlayerLogin(WorldPacket& recvPacket)
{
	uint32 roomid, SameRoom;
	PlayerInfo pInfo;

	recvPacket >> roomid >> SameRoom;
}